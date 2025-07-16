//qnix Store Database.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <limits.h>
#include <dirent.h>
#include "nix_store.h" // For NIX_STORE_PATH definition
#include "nix_store_db.h"
#include "sha256.h" // For SHA256 functions

// Define the database file paths
#define DB_PATH NIX_STORE_PATH "/.nix-db/db"
#define ROOTS_PATH NIX_STORE_PATH "/.nix-db/roots"
#define TEMP_SUFFIX ".tmp" // Suffix for temporary files

// Structure for database entries
typedef struct {
    char path[PATH_MAX];
    char references[10][PATH_MAX];  // Up to 10 references per entry
    int ref_count;
    time_t creation_time;
    char hash[SHA256_DIGEST_STRING_LENGTH];  // Add hash field
} DBEntry;

// Helper function to ensure the DB directory exists
static int ensure_db_dir_exists(void) {
    struct stat st = {0};
    char dir[PATH_MAX];
    snprintf(dir, PATH_MAX, NIX_STORE_PATH "/.nix-db");

    if (stat(dir, &st) == -1) {
        if (mkdir(dir, 0755) == -1 && errno != EEXIST) {
            fprintf(stderr, "Failed to create database directory %s: %s\n", dir, strerror(errno));
            return -1;
        }
    } else if (!S_ISDIR(st.st_mode)) {
         fprintf(stderr, "Error: %s exists but is not a directory.\n", dir);
         return -1;
    }
    return 0;
}

// Open the database file
static FILE* open_db(const char* mode) {
    if (ensure_db_dir_exists() != 0) {
        return NULL;
    }
    return fopen(DB_PATH, mode);
}

// Register a new path in the database
int db_register_path(const char* path, const char** references) {
    // Validate path
    if (!path) {
        fprintf(stderr, "Cannot register NULL path\n");
        return -1;
    }

    // First check if database exists
    FILE* db = open_db("a+");
    if (!db) {
        fprintf(stderr, "Failed to open database for registration\n");
        return -1;
    }

    // Check if path already exists
    rewind(db);
    DBEntry existing;
    while (fread(&existing, sizeof(existing), 1, db) == 1) {
        if (strcmp(existing.path, path) == 0) {
            // Only update references if provided
            if (references) {
                // Update references
                memset(existing.references, 0, sizeof(existing.references));
                existing.ref_count = 0;
                for (int i = 0; references[i] != NULL && i < 10; i++) {
                    strncpy(existing.references[i], references[i], PATH_MAX - 1);
                    existing.references[i][PATH_MAX - 1] = '\0';
                    existing.ref_count++;
                }
                
                // Seek back and write updated entry
                fseek(db, -(long)sizeof(existing), SEEK_CUR);
                if (fwrite(&existing, sizeof(existing), 1, db) != 1) {
                    fprintf(stderr, "Failed to update references for %s\n", path);
                    fclose(db);
                    return -1;
                }
            }
            fclose(db);
            return 0; // Path already registered
        }
    }

    // Create new entry
    DBEntry entry;
    memset(&entry, 0, sizeof(entry)); // Zero everything including hash
    
    // Copy path
    strncpy(entry.path, path, PATH_MAX - 1);
    entry.path[PATH_MAX - 1] = '\0';

    // Add references if provided
    entry.ref_count = 0;
    if (references) {
        for (int i = 0; references[i] != NULL && i < 10; i++) {
            strncpy(entry.references[i], references[i], PATH_MAX - 1);
            entry.references[i][PATH_MAX - 1] = '\0';
            entry.ref_count++;
        }
    }

    // Set creation time
    entry.creation_time = time(NULL);

    // Write the new entry at end of file
    fseek(db, 0, SEEK_END);
    if (fwrite(&entry, sizeof(entry), 1, db) != 1) {
        fprintf(stderr, "Failed to write entry for %s to database\n", path);
        fclose(db);
        return -1;
    }

    // Ensure write is flushed to disk
    fflush(db);
    fclose(db);

    printf("Successfully registered %s in database\n", path);
    return 0;
}

// Check if a path exists in the database
int db_path_exists(const char* path) {
    FILE* db = open_db("r");
    if (!db) {
        return 0;  // Database doesn't exist or can't be opened
    }

    DBEntry entry;
    while (fread(&entry, sizeof(entry), 1, db) == 1) {
        if (strcmp(entry.path, path) == 0) {
            fclose(db);
            return 1;
        }
    }

    fclose(db);
    return 0;
}

// Get all references for a path
char** db_get_references(const char* path) {
    FILE* db = open_db("r");
    if (!db) {
        return NULL;
    }

    DBEntry entry;
    while (fread(&entry, sizeof(entry), 1, db) == 1) {
        if (strcmp(entry.path, path) == 0) {
            // Found the entry, create a return array
            if (entry.ref_count <= 0) { // Handle case with 0 references
                 fclose(db);
                 char** refs = malloc(sizeof(char*)); // Allocate space for NULL terminator
                 if (refs) refs[0] = NULL;
                 return refs;
            }

            char** refs = malloc((entry.ref_count + 1) * sizeof(char*));
            if (!refs) {
                fclose(db);
                return NULL; // Allocation failure
            }

            int allocated_count = 0;
            for (int i = 0; i < entry.ref_count; i++) {
                refs[i] = strdup(entry.references[i]);
                if (!refs[i]) {
                    // Memory allocation failed, free allocated memory so far
                    fprintf(stderr, "Memory allocation failed for reference string\n");
                    for (int j = 0; j < i; j++) {
                        free(refs[j]);
                    }
                    free(refs);
                    fclose(db);
                    return NULL;
                }
                 allocated_count++;
            }
            refs[allocated_count] = NULL;  // Null-terminate the array

            fclose(db);
            return refs;
        }
    }

    fclose(db);
    return NULL; // Path not found
}

// Helper function for removing lines from text files (like roots)
static int remove_line_from_file(const char* filepath, const char* line_to_remove) {
    char temp_path[PATH_MAX];
    snprintf(temp_path, PATH_MAX, "%s%s", filepath, TEMP_SUFFIX);

    FILE* original = fopen(filepath, "r");
    // If file doesn't exist, there's nothing to remove - return success
    if (!original) {
        return 0; 
    }

    FILE* temp = fopen(temp_path, "w");
    if (!temp) {
        fprintf(stderr, "Failed to open temporary file %s: %s\n", temp_path, strerror(errno));
        if (original) fclose(original);
        return -1;
    }

    int found = 0;
    if (original) { // Only read if original file was opened
        char line[PATH_MAX];
        while (fgets(line, PATH_MAX, original)) {
            // Remove newline if present for comparison
            size_t len = strlen(line);
            if (len > 0 && line[len-1] == '\n') {
                line[len-1] = '\0';
            }

            if (strcmp(line, line_to_remove) == 0) {
                found = 1; // Found the line, don't write it to temp
            } else {
                 // Write back the original line with newline
                 line[len-1] = '\n'; // Put newline back if removed
                 if (fputs(line, temp) == EOF) {
                     fprintf(stderr, "Failed to write to temporary file %s\n", temp_path);
                     fclose(original);
                     fclose(temp);
                     remove(temp_path); // Attempt cleanup
                     return -1;
                 }
            }
        }
        fclose(original);
    }

    if (fclose(temp) != 0) {
        fprintf(stderr, "Failed to close temporary file %s: %s\n", temp_path, strerror(errno));
        remove(temp_path);
        return -1;
    }

    // Replace the old file with the new one
    if (rename(temp_path, filepath) == -1) {
        fprintf(stderr, "Failed to rename %s to %s: %s\n", temp_path, filepath, strerror(errno));
        remove(temp_path); // Attempt cleanup
        return -1;
    }

    // Return 1 if removed, 0 if not found (but no error)
    return found ? 1 : 0;
}

// Remove a path from the database (called by GC)
int db_remove_path(const char* path) {
    // Check if the path exists in the database
    FILE* db = open_db("r");
    // If db doesn't exist, nothing to remove
    if (!db) return 0;

    char temp_db_path[PATH_MAX];
    snprintf(temp_db_path, PATH_MAX, "%s%s", DB_PATH, TEMP_SUFFIX);

    FILE* temp_db = fopen(temp_db_path, "w");
    if (!temp_db) {
        fprintf(stderr, "Failed to open temporary db file %s: %s\n", temp_db_path, strerror(errno));
        fclose(db);
        return -1;
    }

    // Copy all entries except the one to remove
    DBEntry entry;
    int found_in_db = 0;
    while (fread(&entry, sizeof(entry), 1, db) == 1) {
        if (strcmp(entry.path, path) != 0) {
            if (fwrite(&entry, sizeof(entry), 1, temp_db) != 1) {
                fprintf(stderr, "Failed to write to temporary db file %s\n", temp_db_path);
                 fclose(db);
                 fclose(temp_db);
                 remove(temp_db_path);
                 return -1;
            }
        } else {
            found_in_db = 1;
        }
    }

    fclose(db);
    if (fclose(temp_db) != 0) {
         fprintf(stderr, "Failed to close temporary db file %s: %s\n", temp_db_path, strerror(errno));
         remove(temp_db_path);
         return -1;
    }

    // Replace the old database with the new one only if entry was found
    if (found_in_db) {
        if (rename(temp_db_path, DB_PATH) == -1) {
            fprintf(stderr, "Failed to update database %s: %s\n", DB_PATH, strerror(errno));
            remove(temp_db_path); // Attempt cleanup
            return -1;
        }
    } else {
        remove(temp_db_path); // No change needed, remove temp file
    }

    // Also remove from roots file 
    // This uses the helper function
    remove_line_from_file(ROOTS_PATH, path);
   

    return 0; // Success
}

// Add a GC Root
int db_add_root(const char* path) {
    // 1. Verify the path exists in the store database first
    if (!db_path_exists(path)) {
        fprintf(stderr, "Error: Cannot add root for path '%s' because it is not registered in the store database.\n", path);
        return -1;
    }

     // 2. Ensure the DB directory exists
    if (ensure_db_dir_exists() != 0) {
        return -1;
    }

    // 3. Check if root already exists in the roots file
    FILE* roots_read = fopen(ROOTS_PATH, "r");
    int already_exists = 0;
    if (roots_read) {
        char line[PATH_MAX];
        while (fgets(line, PATH_MAX, roots_read)) {
             // Remove newline if present
            size_t len = strlen(line);
            if (len > 0 && line[len-1] == '\n') {
                line[len-1] = '\0';
            }
            if (strcmp(line, path) == 0) {
                already_exists = 1;
                break;
            }
        }
        fclose(roots_read);
    }

    if (already_exists) {
        printf("Path %s is already a GC root.\n", path);
        return 0; // Not an error, already done
    }

    // 4. Append the path to the roots file
    FILE* roots_append = fopen(ROOTS_PATH, "a");
    if (!roots_append) {
        fprintf(stderr, "Failed to open roots file %s for appending: %s\n", ROOTS_PATH, strerror(errno));
        return -1;
    }

    if (fprintf(roots_append, "%s\n", path) < 0) {
        fprintf(stderr, "Failed to write root path %s to %s: %s\n", path, ROOTS_PATH, strerror(errno));
        fclose(roots_append);
        return -1;
    }

    if (fclose(roots_append) != 0) {
         fprintf(stderr, "Failed to close roots file %s after appending: %s\n", ROOTS_PATH, strerror(errno));
         // Data might be written, but report potential issue
         return -1;
    }

    printf("Added GC root: %s\n", path);
    return 0;
}

// Remove a GC Root
int db_remove_root(const char* path) {
    // We just need to remove the line from the roots file
    // The helper function does the work.
    // Return value: 1 if removed, 0 if not found, -1 on error.
    int result = remove_line_from_file(ROOTS_PATH, path);

    if (result == 1) {
        printf("Removed GC root: %s\n", path);
        return 0; // Success (removed)
    } else if (result == 0) {
         printf("Path %s was not found in GC roots file.\n", path);
         return 0; // Success (wasn't a root anyway)
    } else {
         fprintf(stderr, "Error occurred while trying to remove root: %s\n", path);
         return -1; // Error during file operations
    }
}

// Store hash for path
int db_store_hash(const char* path, const char* hash) {
    // Validate inputs
    if (!path || !hash) {
        fprintf(stderr, "Invalid path or hash (NULL)\n");
        return -1;
    }

    // First check if database exists
    FILE* db = open_db("r+");
    if (!db) {
        // Database doesn't exist or can't be opened for update
        fprintf(stderr, "Failed to open database for updating hash\n");
        return -1;
    }

    DBEntry entry;
    int found = 0;
    long pos = 0;
    
    // Search for existing entry
    rewind(db);
    while (fread(&entry, sizeof(entry), 1, db) == 1) {
        if (strcmp(entry.path, path) == 0) {
            found = 1;
            break;
        }
        pos = ftell(db);
    }

    if (!found) {
        fprintf(stderr, "Path %s not found in database for hash update\n", path);
        fclose(db);
        return -1;
    }

    // Update the hash
    strncpy(entry.hash, hash, SHA256_DIGEST_STRING_LENGTH - 1);
    entry.hash[SHA256_DIGEST_STRING_LENGTH - 1] = '\0';
    
    // Seek back and write updated entry
    if (fseek(db, pos, SEEK_SET) != 0) {
        fprintf(stderr, "Failed to seek in database for hash update\n");
        fclose(db);
        return -1;
    }

    size_t written = fwrite(&entry, sizeof(entry), 1, db);
    if (written != 1) {
        fprintf(stderr, "Failed to write updated hash to database: %s\n", strerror(errno));
        fclose(db);
        return -1;
    }

    // Ensure changes are written to disk
    if (fflush(db) != 0) {
        fprintf(stderr, "Failed to flush database changes: %s\n", strerror(errno));
        fclose(db);
        return -1;
    }

    fclose(db);
    printf("Successfully stored hash for %s\n", path);
    return 0;
}

// Get stored hash for path
char* db_get_hash(const char* path) {
    FILE* db = open_db("r");
    if (!db) return NULL;

    DBEntry entry;
    while (fread(&entry, sizeof(entry), 1, db) == 1) {
        if (strcmp(entry.path, path) == 0) {
            char* hash = strdup(entry.hash);
            fclose(db);
            return hash;
        }
    }

    fclose(db);
    return NULL;
}

// Verify path hash matches stored hash
int db_verify_path_hash(const char* path) {
    char* stored_hash = db_get_hash(path);
    if (!stored_hash) {
        fprintf(stderr, "No stored hash found for %s\n", path);
        return -1;
    }

    // Compute current hash of path contents
    char* current_hash = NULL;
    struct stat st;

    if (stat(path, &st) == 0) {
        SHA256_CTX ctx;
        sha256_init(&ctx);

        if (S_ISDIR(st.st_mode)) {
            // For directories, first get sorted list of all files
            char* file_list[1024];
            int file_count = 0;
            
            // Helper to collect files recursively 
            void collect_files(const char* dir_path) {
                DIR* dir = opendir(dir_path);
                if (!dir) return;
                
                struct dirent* entry;
                while ((entry = readdir(dir)) != NULL && file_count < 1024) {
                    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                        continue;
                        
                    char full_path[PATH_MAX];
                    snprintf(full_path, PATH_MAX, "%s/%s", dir_path, entry->d_name);
                    
                    struct stat st;
                    if (stat(full_path, &st) != 0) continue;
                    
                    if (S_ISDIR(st.st_mode)) {
                        collect_files(full_path);
                    } else if (S_ISREG(st.st_mode)) {
                        // Store relative path for consistent hashing
                        file_list[file_count++] = strdup(full_path + strlen(path) + 1);
                    }
                }
                closedir(dir);
            }
            
            // Get all file paths
            collect_files(path);
            
            // Sort paths for consistent hash
            for (int i = 0; i < file_count - 1; i++) {
                for (int j = i + 1; j < file_count; j++) {
                    if (strcmp(file_list[i], file_list[j]) > 0) {
                        char* temp = file_list[i];
                        file_list[i] = file_list[j];
                        file_list[j] = temp;
                    }
                }
            }
            
            // Hash each file in sorted order
            for (int i = 0; i < file_count; i++) {
                // Add relative path to hash first
                sha256_update(&ctx, (uint8_t*)file_list[i], strlen(file_list[i]));
                
                // Then hash file contents
                char full_path[PATH_MAX];
                snprintf(full_path, PATH_MAX, "%s/%s", path, file_list[i]);
                
                FILE* f = fopen(full_path, "rb");
                if (f) {
                    uint8_t buffer[4096];
                    size_t bytes;
                    while ((bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
                        sha256_update(&ctx, buffer, bytes);
                    }
                    fclose(f);
                }
                free(file_list[i]);
            }
            
            // Finalize hash
            uint8_t hash[SHA256_BLOCK_SIZE];
            sha256_final(&ctx, hash);
            current_hash = malloc(SHA256_DIGEST_STRING_LENGTH);
            if (current_hash) {
                for (int i = 0; i < SHA256_BLOCK_SIZE; i++) {
                    sprintf(current_hash + (i * 2), "%02x", hash[i]);
                }
                current_hash[SHA256_DIGEST_STRING_LENGTH - 1] = 0;
            }
        } else {
            // For single files, we need to:
            // 1. Check if it's in bin/ directory
            // 2. Hash both path and contents like storage does
            char bin_path[PATH_MAX];
            char* base = strrchr(path, '/');
            if (base) {
                base++; // Skip the /
                snprintf(bin_path, PATH_MAX, "%s/bin/%s", path, base);
                if (access(bin_path, F_OK) == 0) {
                    // It's a binary in bin/, hash path first
                    const char* rel_path = "bin/";
                    sha256_update(&ctx, (uint8_t*)rel_path, strlen(rel_path));
                    sha256_update(&ctx, (uint8_t*)base, strlen(base));

                    // Then hash contents
                    FILE* f = fopen(bin_path, "rb");
                    if (f) {
                        uint8_t buffer[4096];
                        size_t bytes;
                        while ((bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
                            sha256_update(&ctx, buffer, bytes);
                        }
                        fclose(f);
                    }
                } else {
                    // Regular file, just hash contents
                    FILE* f = fopen(path, "rb");
                    if (f) {
                        uint8_t buffer[4096];
                        size_t bytes;
                        while ((bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
                            sha256_update(&ctx, buffer, bytes);
                        }
                        fclose(f);
                    }
                }
            }
            
            uint8_t hash[SHA256_BLOCK_SIZE];
            sha256_final(&ctx, hash);
            current_hash = malloc(SHA256_DIGEST_STRING_LENGTH);
            if (current_hash) {
                for (int i = 0; i < SHA256_BLOCK_SIZE; i++) {
                    sprintf(current_hash + (i * 2), "%02x", hash[i]);
                }
                current_hash[SHA256_DIGEST_STRING_LENGTH - 1] = 0;
            }
        }
    }

    if (!current_hash) {
        free(stored_hash);
        fprintf(stderr, "Failed to compute current hash for %s\n", path);
        return -1;
    }

    int result = strcmp(stored_hash, current_hash);
    printf("Stored hash:  %s\n", stored_hash);
    printf("Current hash: %s\n", current_hash);
    free(stored_hash);
    free(current_hash);

    return result == 0 ? 0 : -1;
}

int db_register_profile(const char* profile_name, const char* path) {
    char profile_path[PATH_MAX];
    snprintf(profile_path, PATH_MAX, "/data/nix/profiles/%s", profile_name);

    // Add profile directory as a GC root
    return db_add_root(profile_path);
}

int db_remove_profile(const char* profile_name) {
    char profile_path[PATH_MAX];
    snprintf(profile_path, PATH_MAX, "/data/nix/profiles/%s", profile_name);

    // Remove profile directory from GC roots
    return db_remove_root(profile_path);
}

char* db_get_profile_path(const char* profile_name) {
    char* path = malloc(PATH_MAX);
    if (!path) return NULL;

    snprintf(path, PATH_MAX, "/data/nix/profiles/%s", profile_name);
    
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return path;
    }

    free(path);
    return NULL;
}
