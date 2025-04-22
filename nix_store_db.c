/*
 * nix_store_db.c - Database functionality for Nix-like store
 */
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
    FILE* db = open_db("a+"); // Use "a+" to append or create/read
    if (!db) {
        // Error already printed by open_db -> ensure_db_dir_exists
        return -1;
    }

    // Check if the path already exists by trying to read
    rewind(db); // Go to start for reading
    DBEntry check_entry;
    int exists = 0;
    while (fread(&check_entry, sizeof(check_entry), 1, db) == 1) {
        if (strcmp(check_entry.path, path) == 0) {
            exists = 1;
            break;
        }
    }

    if (exists) {
        fclose(db);
        printf("Path %s already registered in database.\n", path);
        return 0;
    }

    // If we reached here, path doesn't exist, file pointer is at EOF (or start if empty)
    // Create a new entry
    DBEntry entry;
    memset(&entry, 0, sizeof(entry));
    strncpy(entry.path, path, PATH_MAX - 1);
    entry.path[PATH_MAX - 1] = '\0'; // Ensure null termination

    // Add references
    entry.ref_count = 0;
    if (references) {
        for (int i = 0; references[i] != NULL && i < 10; i++) {
            strncpy(entry.references[i], references[i], PATH_MAX - 1);
            entry.references[i][PATH_MAX - 1] = '\0'; // Ensure null termination
            entry.ref_count++;
        }
    }

    // Set creation time
    entry.creation_time = time(NULL);

    // Write the entry to the database (append)
    if (fwrite(&entry, sizeof(entry), 1, db) != 1) {
        fprintf(stderr, "Failed to write entry for %s to database: %s\n", path, strerror(errno));
        fclose(db);
        return -1;
    }

    fclose(db);

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
    // It's okay if the original file doesn't exist yet when removing.
    // if (!original) return -1; // Or maybe return 0 if file not existing is ok?

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
    // --- Remove from main DB file ---
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

    // --- Also remove from roots file ---
    // This uses the helper function
    remove_line_from_file(ROOTS_PATH, path);
    // Ignore return value for roots removal? If path wasn't a root, that's ok.

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
    FILE* db = open_db("r+");
    if (!db) return -1;

    DBEntry entry;
    int found = 0;
    long pos = 0;

    while (fread(&entry, sizeof(entry), 1, db) == 1) {
        if (strcmp(entry.path, path) == 0) {
            found = 1;
            break;
        }
        pos = ftell(db);
    }

    if (found) {
        strncpy(entry.hash, hash, SHA256_DIGEST_STRING_LENGTH - 1);
        entry.hash[SHA256_DIGEST_STRING_LENGTH - 1] = '\0';
        
        fseek(db, pos, SEEK_SET);
        if (fwrite(&entry, sizeof(entry), 1, db) != 1) {
            fprintf(stderr, "Failed to update hash for %s\n", path);
            fclose(db);
            return -1;
        }
    }

    fclose(db);
    return found ? 0 : -1;
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
        if (S_ISDIR(st.st_mode)) {
            // For directories, hash the directory listing
            DIR* dir = opendir(path);
            if (dir) {
                char content[4096] = {0};
                struct dirent* entry;
                while ((entry = readdir(dir)) != NULL) {
                    if (strcmp(entry->d_name, ".") == 0 || 
                        strcmp(entry->d_name, "..") == 0)
                        continue;
                    strcat(content, entry->d_name);
                }
                closedir(dir);
                current_hash = sha256_hash_string((uint8_t*)content, strlen(content));
            }
        } else {
            // For files, hash the file content
            FILE* f = fopen(path, "rb");
            if (f) {
                uint8_t buffer[4096];
                size_t bytes;
                SHA256_CTX ctx;
                sha256_init(&ctx);
                while ((bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
                    sha256_update(&ctx, buffer, bytes);
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
                fclose(f);
            }
        }
    }

    if (!current_hash) {
        free(stored_hash);
        fprintf(stderr, "Failed to compute current hash for %s\n", path);
        return -1;
    }

    int result = strcmp(stored_hash, current_hash);
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