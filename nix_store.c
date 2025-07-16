//qnix_store core functions
#include "nix_store.h" // already includes stdio, stdlib, string, sys/stat, sys/types, unistd, errno
#include "sha256.h"
#include "qnix_config.h"
#include <limits.h>
#include <fcntl.h>
#include <dirent.h>
#include <nix_store_db.h>
#include <ctype.h>
#include <sys/stat.h> // For mkdir, chmod
#include <unistd.h>   // For symlink, execvp, chdir, unlink
#include <libgen.h>   // For basename
#include <sys/param.h> // For MAXPATHLEN if PATH_MAX is not defined

#ifndef PATH_MAX
#define PATH_MAX MAXPATHLEN
#endif

#ifndef NIX_STORE_PATH
#define NIX_STORE_PATH "/data/nix/store"
#endif

// Add at the top with other static functions
static int path_exists(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

// Initialize the store directory structure
int store_init(void) {
    // Load configuration first
    config_load(NULL);
    QnixConfig* cfg = config_get();

    // Create the path hierarchy using configured store path
    const char* store_base = cfg->store.store_path;
    char profiles_path[PATH_MAX];
    snprintf(profiles_path, PATH_MAX, "%s/../profiles", store_base);

    const char* path_parts[] = {
        dirname(strdup(store_base)),  // /data/nix
        store_base,                   // /data/nix/store
        profiles_path                 // /data/nix/profiles
    };

    for (size_t i = 0; i < sizeof(path_parts) / sizeof(path_parts[0]); i++) {
        struct stat st = {0};
        if (stat(path_parts[i], &st) == -1) {
            if (mkdir(path_parts[i], 0755) == -1 && errno != EEXIST) {
                fprintf(stderr, "Failed to create directory %s: %s\n",
                        path_parts[i], strerror(errno));
                return -1;
            }
        } else if (!S_ISDIR(st.st_mode)) {
             fprintf(stderr, "Error: Path %s exists but is not a directory.\n", path_parts[i]);
             return -1;
        }
    }

    // Create database directory
    char db_path[PATH_MAX];
    snprintf(db_path, PATH_MAX, "%s/.nix-db", store_base);

    struct stat st_db = {0};
    if (stat(db_path, &st_db) == -1) {
        if (mkdir(db_path, 0755) == -1 && errno != EEXIST) {
            fprintf(stderr, "Failed to create database directory %s: %s\n",
                    db_path, strerror(errno));
            return -1;
        }
    } else if (!S_ISDIR(st_db.st_mode)) {
        fprintf(stderr, "Error: Path %s exists but is not a directory.\n", db_path);
        return -1;
    }

    return 0;
}

// Compute a store path based on name and input references
char* compute_store_path(const char* name, const char* hash, const char** references) {
    // Create a buffer to compute the final hash
    char hash_data[4096] = {0};
    char* hash_result;

    // Start with the provided hash or compute from name if NULL
    if (hash) {
        strncpy(hash_data, hash, sizeof(hash_data) - 1);
    } else {
        // Compute a hash from the name
        hash_result = sha256_hash_string((const uint8_t*)name, strlen(name));
        if (!hash_result) return NULL; // Handle hash computation failure
        strncpy(hash_data, hash_result, sizeof(hash_data) - 1);
        free(hash_result);
    }

    // Append references to the hash data
    if (references) {
        for (int i = 0; references[i] != NULL; i++) {
            strncat(hash_data, references[i],
                   sizeof(hash_data) - strlen(hash_data) - 1);
        }
    }

    // Compute the final hash
    hash_result = sha256_hash_string((const uint8_t*)hash_data, strlen(hash_data));
    if (!hash_result) return NULL; // Handle hash computation failure

    // Create the final path
    char* result = malloc(PATH_MAX);
    if (!result) {
        free(hash_result);
        return NULL;
    }

    int path_len = snprintf(result, PATH_MAX, "%s/%s-%s", NIX_STORE_PATH, hash_result, name);
    free(hash_result);

    if (path_len < 0 || path_len >= PATH_MAX) {
         fprintf(stderr, "Error: Computed store path exceeds PATH_MAX for name '%s'.\n", name);
         free(result);
         return NULL;
    }

    return result;
}


// Add a file or directory to the store with explicit dependencies
int add_to_store_with_deps(const char* source_path, const char* name, const char** deps, int deps_count) {
    struct stat st;
    if (stat(source_path, &st) == -1) {
        fprintf(stderr, "Source path does not exist: %s (%s)\n", source_path, strerror(errno));
        return -1;
    }

    // Ensure all dependencies are in the store first
    char** dep_store_paths = NULL;
    if (deps_count > 0) {
        dep_store_paths = malloc((deps_count + 1) * sizeof(char*));
        if (!dep_store_paths) {
            fprintf(stderr, "Memory allocation failed for dependency paths\n");
            return -1;
        }
        memset(dep_store_paths, 0, (deps_count + 1) * sizeof(char*)); // Initialize to NULL

        for (int i = 0; i < deps_count; i++) {
             struct stat dep_st;
             // Check if dependency is a valid, existing store path
             if (!deps[i] || stat(deps[i], &dep_st) != 0 || strncmp(deps[i], NIX_STORE_PATH, strlen(NIX_STORE_PATH)) != 0) {
                 fprintf(stderr, "Error: Dependency '%s' is not a valid store path. Aborting.\n", deps[i] ? deps[i] : "(null)");
                 // Clean up already allocated store paths
                 for (int j = 0; j < i; j++) free(dep_store_paths[j]);
                 free(dep_store_paths);
                 return -1;
             } else {
                // If dependency exists and seems like a store path, just copy the path string
                dep_store_paths[i] = strdup(deps[i]);
                if (!dep_store_paths[i]) {
                     fprintf(stderr, "Memory allocation failed for dependency path string\n");
                     // Handle allocation failure
                     for (int j = 0; j < i; j++) free(dep_store_paths[j]);
                     free(dep_store_paths);
                     return -1;
                }
            }
        }
        dep_store_paths[deps_count] = NULL; // NULL-terminate
    }

    // Now compute the store path for this item, including references to dependencies
    char* store_path = compute_store_path(name, NULL, (const char**)dep_store_paths);
    if (!store_path) {
        if (dep_store_paths) {
            for (int i = 0; i < deps_count; i++) {
                if(dep_store_paths[i]) free(dep_store_paths[i]);
            }
            free(dep_store_paths);
        }
        fprintf(stderr, "Failed to compute store path\n");
        return -1;
    }

    // Check if the path already exists in the store
    struct stat store_st;
    if (stat(store_path, &store_st) == 0) {
        printf("Path %s already exists in store.\n", store_path);

        // Path exists but make sure it has a hash
        char* existing_hash = db_get_hash(store_path);
        if (!existing_hash) {
            // No hash stored, compute and store it
            SHA256_CTX ctx;
            sha256_init(&ctx);
            char hash_str[SHA256_DIGEST_STRING_LENGTH];
            
            // Compute hash for existing store path
            if (S_ISDIR(store_st.st_mode)) {
                // ... existing directory hash code ...
            } else {
                char bin_path[PATH_MAX];
                snprintf(bin_path, PATH_MAX, "%s/bin/%s", store_path, basename((char*)source_path));
                FILE* f = fopen(bin_path, "rb");
                if (f) {
                    // Hash contents
                    uint8_t buffer[4096];
                    size_t bytes;
                    while ((bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
                        sha256_update(&ctx, buffer, bytes);
                    }
                    uint8_t hash[SHA256_BLOCK_SIZE];
                    sha256_final(&ctx, hash);
                    for (int i = 0; i < SHA256_BLOCK_SIZE; i++) {
                        sprintf(hash_str + (i * 2), "%02x", hash[i]);
                    }
                    hash_str[SHA256_DIGEST_STRING_LENGTH - 1] = 0;
                    db_store_hash(store_path, hash_str);
                    fclose(f);
                }
            }
        } else {
            free(existing_hash);
        }

        // Just register dependencies for existing path
        if (deps_count > 0 && dep_store_paths != NULL) {
            db_register_path(store_path, (const char**)dep_store_paths);
        }

        free(store_path);
        if (dep_store_paths) {
            for (int i = 0; i < deps_count; i++) {
                if(dep_store_paths[i]) free(dep_store_paths[i]);
            }
            free(dep_store_paths);
        }
        return 0; // Return success, path existed
    }

    // Create the directory for the store item
    if (mkdir(store_path, 0755) == -1) {
         if (errno != EEXIST) {
            fprintf(stderr, "Failed to create store directory %s: %s\n", store_path, strerror(errno));
            free(store_path);
            if (dep_store_paths) {
                for (int i = 0; i < deps_count; i++) if(dep_store_paths[i]) free(dep_store_paths[i]);
                free(dep_store_paths);
            }
            return -1;
         }
    }


    // Copy the file/directory to the store
    //char dest_path_in_store[PATH_MAX]; // Used for single file copy
    int copy_len;

    if (S_ISDIR(st.st_mode)) {
        // Copy the directory content *into* the store path
        char cmd[PATH_MAX * 3];
        // Use cp -rP to copy recursively, preserving symlinks. Copy contents using trailing /.
        // Check command length before executing
        copy_len = snprintf(cmd, sizeof(cmd), "cp -rP %s/. %s/", source_path, store_path);
        if(copy_len < 0 || copy_len >= sizeof(cmd)) {
             fprintf(stderr, "Error: Copy command exceeds buffer size for source %s\n", source_path);
             // Attempt cleanup before returning
             rmdir(store_path); // May fail if not empty
             free(store_path);
             if (dep_store_paths) {
                for (int i = 0; i < deps_count; i++) if(dep_store_paths[i]) free(dep_store_paths[i]);
                free(dep_store_paths);
             }
             return -1;
        }

        printf("Executing: %s\n", cmd);
        int ret = system(cmd);
        if (ret != 0) {
            fprintf(stderr, "Failed to copy directory contents %s to %s (system returned %d)\n", source_path, store_path, ret);
            // Consider recursive remove on failure
            char rm_cmd[PATH_MAX + 10];
            snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf %s", store_path);
            system(rm_cmd); // Attempt cleanup
            free(store_path);
            if (dep_store_paths) {
                 for (int i = 0; i < deps_count; i++) if(dep_store_paths[i]) free(dep_store_paths[i]);
                 free(dep_store_paths);
            }
            return -1;
        }
    } else if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
        // Modified file handling
        char bin_dir[PATH_MAX];
        snprintf(bin_dir, PATH_MAX, "%s/bin", store_path);
        mkdir(bin_dir, 0755);  // Create bin directory
        
        char dest_path[PATH_MAX];
        int ret_val = snprintf(dest_path, PATH_MAX, "%s/bin/%s", store_path, basename((char*)source_path));
        if (ret_val < 0 || ret_val >= PATH_MAX) {
            fprintf(stderr, "Error: Destination path too long for %s\n", source_path);
            rmdir(bin_dir);
            rmdir(store_path);
            free(store_path);
            if (dep_store_paths) {
                for (int i = 0; i < deps_count; i++) if(dep_store_paths[i]) free(dep_store_paths[i]);
                free(dep_store_paths);
            }
            return -1;
        }

        // Special handling for files from /proc/boot
        char cmd[PATH_MAX * 3];
        if (strncmp(source_path, "/proc/boot/", 11) == 0) {
            // Use dd for boot files to preserve all attributes
            copy_len = snprintf(cmd, sizeof(cmd), 
                    "dd if=%s of=%s bs=4096 conv=sync,noerror 2>/dev/null && chmod 755 %s", 
                    source_path, dest_path, dest_path);
        } else {
            // Normal file copy for other files
            copy_len = snprintf(cmd, sizeof(cmd), "cp -P %s %s", source_path, dest_path);
        }

        if(copy_len < 0 || copy_len >= sizeof(cmd)) {
            fprintf(stderr, "Error: Copy command exceeds buffer size for source %s\n", source_path);
            rmdir(store_path);
            free(store_path);
            if (dep_store_paths) {
                for (int i = 0; i < deps_count; i++) if(dep_store_paths[i]) free(dep_store_paths[i]);
                free(dep_store_paths);
            }
            return -1;
        }

        printf("Executing: %s\n", cmd);
        int ret = system(cmd);
        if (ret != 0) {
            fprintf(stderr, "Failed to copy file %s to %s (system returned %d)\n", 
                    source_path, dest_path, ret);
            char rm_cmd[PATH_MAX + 10];
            snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf %s", store_path);
            system(rm_cmd);
            free(store_path);
            if (dep_store_paths) {
                for (int i = 0; i < deps_count; i++) if(dep_store_paths[i]) free(dep_store_paths[i]);
                free(dep_store_paths);
            }
            return -1;
        }

        // Verify the copied file exists and is executable
        struct stat st_copy;
        if (stat(dest_path, &st_copy) != 0 || !S_ISREG(st_copy.st_mode)) {
            fprintf(stderr, "Failed to verify copied file %s\n", dest_path);
            rmdir(store_path);
            free(store_path);
            if (dep_store_paths) {
                for (int i = 0; i < deps_count; i++) if(dep_store_paths[i]) free(dep_store_paths[i]);
                free(dep_store_paths);
            }
            return -1;
        }

        // Always ensure the file is executable
        chmod(dest_path, 0755);
    } else {
         fprintf(stderr, "Unsupported file type for source path: %s\n", source_path);
         rmdir(store_path); // Simple cleanup attempt
         free(store_path);
         if (dep_store_paths) {
            for (int i = 0; i < deps_count; i++) if(dep_store_paths[i]) free(dep_store_paths[i]);
            free(dep_store_paths);
         }
         return -1;
    }

    // Make the store path read-only first
    make_store_path_read_only(store_path);

    // Compute and store hash
    SHA256_CTX ctx;
    sha256_init(&ctx);
    char hash_str[SHA256_DIGEST_STRING_LENGTH];
    int hash_success = 0;

    // For regular files, we need to hash bin/<name>
    if (S_ISREG(st.st_mode)) {
        char bin_path[PATH_MAX];
        snprintf(bin_path, PATH_MAX, "%s/bin/%s", store_path, basename((char*)source_path));
        FILE* f = fopen(bin_path, "rb");
        if (f) {
            // First hash the relative path for consistency
            const char* rel_path = "bin/";
            sha256_update(&ctx, (uint8_t*)rel_path, strlen(rel_path));
            rel_path = basename((char*)source_path);
            sha256_update(&ctx, (uint8_t*)rel_path, strlen(rel_path));

            // Then hash file contents
            uint8_t buffer[4096];
            size_t bytes;
            while ((bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
                sha256_update(&ctx, buffer, bytes);
            }
            uint8_t hash[SHA256_BLOCK_SIZE];
            sha256_final(&ctx, hash);
            for (int i = 0; i < SHA256_BLOCK_SIZE; i++) {
                sprintf(hash_str + (i * 2), "%02x", hash[i]);
            }
            hash_str[SHA256_DIGEST_STRING_LENGTH - 1] = 0;
            hash_success = 1;
            fclose(f);
        }
    } else if (S_ISDIR(st.st_mode)) {
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
                    file_list[file_count++] = strdup(full_path + strlen(store_path) + 1);
                }
            }
            closedir(dir);
        }
        
        // Get all file paths
        collect_files(store_path);
        
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
            // Add relative path to hash
            sha256_update(&ctx, (uint8_t*)file_list[i], strlen(file_list[i]));
            
            // Add file contents
            char full_path[PATH_MAX];
            snprintf(full_path, PATH_MAX, "%s/%s", store_path, file_list[i]);
            
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
        for (int i = 0; i < SHA256_BLOCK_SIZE; i++) {
            sprintf(hash_str + (i * 2), "%02x", hash[i]);
        }
        hash_str[SHA256_DIGEST_STRING_LENGTH - 1] = 0;
        hash_success = 1;
    }

    // Register in database and store hash in one atomic operation
    if (hash_success) {
        printf("Registering path and storing hash for %s: %s\n", store_path, hash_str);
        
        // First register the path
        if (db_register_path(store_path, (const char**)dep_store_paths) != 0) {
            fprintf(stderr, "Failed to register %s in database\n", store_path);
            return -1;
        }

        // Then immediately store its hash
        if (db_store_hash(store_path, hash_str) != 0) {
            fprintf(stderr, "Failed to store hash for %s\n", store_path);
            return -1;
        }
    }

    printf("Added %s to store (%s) with %d dependencies\n", name, store_path, deps_count);

    // Clean up
    free(store_path);
    if (dep_store_paths) {
        for (int i = 0; i < deps_count; i++) {
            if(dep_store_paths[i]) free(dep_store_paths[i]);
        }
        free(dep_store_paths);
    }

    return 0;
}


// Add a file or directory to the store (simple version, no deps)
int add_to_store(const char* source_path, const char* name, int recursive) {
     const char** no_deps = NULL;
     // Call the dependency-aware version with no dependencies
     return add_to_store_with_deps(source_path, name, no_deps, 0);
}


// Make a store path read-only (recursively)
int make_store_path_read_only(const char* path) {
    // Use system command `chmod -R a-w,a+rX` for simplicity and correctness
    char cmd[PATH_MAX + 50];
    int cmd_len = snprintf(cmd, sizeof(cmd), "chmod -R a-w,a+rX %s", path);
     if (cmd_len < 0 || cmd_len >= sizeof(cmd)) {
        fprintf(stderr, "Warning: Chmod command exceeds buffer size for path %s\n", path);
        return 0; // Continue, but log warning
    }
    printf("Executing: %s\n", cmd);
    int ret = system(cmd);
    if (ret != 0) {
         fprintf(stderr, "Warning: Failed to make path read-only %s (system returned %d)\n", path, ret);
    }
    return 0; // Indicate success even on chmod warning for now
}

// Verify a store path
int verify_store_path(const char* path) {
    // Check if path exists and is in store
    struct stat st;
    if (stat(path, &st) == -1) {
        fprintf(stderr, "Verify failed: Path %s does not exist or is inaccessible (%s).\n", 
                path, strerror(errno));
        return -1;
    }

    // Check if path is inside store and safe
    if (strncmp(path, NIX_STORE_PATH, strlen(NIX_STORE_PATH)) != 0 || 
        strstr(path, "..") != NULL) {
        fprintf(stderr, "Verify failed: Path %s is not within store or contains '..'.\n", path);
        return -1;
    }

    // Check if registered in database
    if (!db_path_exists(path)) {
        fprintf(stderr, "Verify failed: Path %s not registered in database.\n", path);
        return -1;
    }

    // Verify path contents match stored hash
    if (db_verify_path_hash(path) != 0) {
        fprintf(stderr, "Verify failed: Path %s contents do not match stored hash.\n", path);
        return -1;
    }

    printf("Path %s verified successfully.\n", path);
    return 0;
}


// Helper function to find store path for a boot or system library
static char* find_store_path_for_boot_lib(const char* lib_path) {
   const char* lib_name = strrchr(lib_path, '/');
   if (!lib_name) return NULL;
   lib_name++; // Skip the '/'

   // Check if this is a boot or system library
   int is_boot_lib = (strncmp(lib_path, "/proc/boot/", 11) == 0);
   int is_system_lib = (strncmp(lib_path, "/system/lib/", 12) == 0);
   
   if (!is_boot_lib && !is_system_lib) {
       printf("  Not a boot or system library: %s\n", lib_path);
       return NULL;
   }

   printf("  Looking for library in store: %s\n", lib_name);

   DIR* dir = opendir(NIX_STORE_PATH);
   if (!dir) return NULL;

   char* result = NULL;
   struct dirent* entry;
   while ((entry = readdir(dir)) != NULL) {
       if (entry->d_name[0] == '.') continue;

       // Look for entries containing the library name
       if (strstr(entry->d_name, lib_name) != NULL) {
           char full_path[PATH_MAX];
           int path_len = snprintf(full_path, sizeof(full_path), "%s/%s", NIX_STORE_PATH, entry->d_name);
           if (path_len < 0 || path_len >= sizeof(full_path)) {
               fprintf(stderr, "  Warning: Store path too long for %s\n", entry->d_name);
               continue;
           }
           
           // Check both lib/ and bin/ directories for the library
           char lib_check_path[PATH_MAX];
           char* check_dirs[] = {"lib", "bin", NULL};
           
           for (char** dir = check_dirs; *dir; dir++) {
               path_len = snprintf(lib_check_path, sizeof(lib_check_path), "%s/%s/%s", full_path, *dir, lib_name);
               if (path_len < 0 || path_len >= sizeof(lib_check_path)) {
                   fprintf(stderr, "  Warning: Library path too long: %s/%s/%s\n", full_path, *dir, lib_name);
                   continue;
               }
               
               struct stat st;
               if (stat(lib_check_path, &st) == 0 && S_ISREG(st.st_mode)) {
                   printf("  Found library in store: %s\n", lib_check_path);
                   result = strdup(full_path);
                   goto found;  // Break out of both loops
               }
           }
       }
   }
found:
   closedir(dir);
   
   if (!result) {
       printf("  Failed to find library in store: %s\n", lib_name);
       // If the library isn't in the store, try to add it
       if (add_to_store(lib_path, lib_name, 0) == 0) {
           // Retry finding the library after adding it
           printf("  Added library to store, retrying lookup...\n");
           return find_store_path_for_boot_lib(lib_path);
       }
   }
   
   return result;
}

int scan_dependencies(const char* exec_path, char*** deps_out) {
   FILE* pipe;
   char cmd[PATH_MAX + 4];
   char buffer[1024];
   char** deps = NULL;
   int dep_count = 0;
   int deps_capacity = 0;

   int cmd_len = snprintf(cmd, sizeof(cmd), "ldd %s", exec_path);
   if(cmd_len < 0 || cmd_len >= sizeof(cmd)) {
       fprintf(stderr, "Error: ldd command exceeds buffer size for path %s\n", exec_path);
       *deps_out = NULL;
       return -1;
   }

   pipe = popen(cmd, "r");
   if (!pipe) {
       fprintf(stderr, "Failed to execute dependency scan command '%s': %s\n", cmd, strerror(errno));
       *deps_out = NULL;
       return -1;
   }

   printf("Scanning dependencies for %s using: %s\n", exec_path, cmd);

   while (fgets(buffer, sizeof(buffer), pipe)) {
       char* arrow = strstr(buffer, "=>");
       if (arrow) {
           char* lib_path_start = arrow + 2;
           char extracted_path[PATH_MAX];

           // Skip whitespace
           while (*lib_path_start != '\0' && isspace((unsigned char)*lib_path_start)) {
               lib_path_start++;
           }

           // Find end of path (before address/whitespace)
           char* lib_path_end = lib_path_start;
           while (*lib_path_end != '\0' && !isspace((unsigned char)*lib_path_end) && *lib_path_end != '(') {
               lib_path_end++;
           }

           size_t path_len = lib_path_end - lib_path_start;
           if (path_len > 0 && path_len < PATH_MAX) {
               strncpy(extracted_path, lib_path_start, path_len);
               extracted_path[path_len] = '\0';

               // Handle only absolute paths
               if (extracted_path[0] == '/') {
                   struct stat st;
                   if (stat(extracted_path, &st) == 0 && S_ISREG(st.st_mode)) {
                       char* store_path = NULL;

                       if (strncmp(extracted_path, NIX_STORE_PATH, strlen(NIX_STORE_PATH)) == 0) {
                           // Direct store path
                           store_path = strdup(extracted_path);
                       } else if (strncmp(extracted_path, "/proc/boot/", 11) == 0 ||
                                strncmp(extracted_path, "/system/lib/", 12) == 0) {
                           // Boot or system library - find its store path
                           store_path = find_store_path_for_boot_lib(extracted_path);
                       }

                       if (store_path) {
                           printf("  Found dependency mapping:\n");
                           printf("    From: %s\n", extracted_path);
                           printf("    To:   %s\n", store_path);

                           // Add to dependencies array
                           if (dep_count >= deps_capacity) {
                               deps_capacity = (deps_capacity == 0) ? 8 : deps_capacity * 2;
                               char** new_deps = realloc(deps, (deps_capacity + 1) * sizeof(char*));
                               if (!new_deps) {
                                   free(store_path);
                                   fprintf(stderr, "Memory allocation failed during dependency scan\n");
                                   for (int k = 0; k < dep_count; k++) free(deps[k]);
                                   free(deps);
                                   pclose(pipe);
                                   *deps_out = NULL;
                                   return -1;
                               }
                               deps = new_deps;
                           }
                           deps[dep_count] = store_path;
                           printf("  Found store dependency: %s\n", deps[dep_count]);
                           dep_count++;
                       } else {
                           printf("  Library not found in store, it will be used from system: %s\n", extracted_path);
                       }
                   }
               }
           }
       }
   }

   int status = pclose(pipe);
   if (status == -1) {
       fprintf(stderr, "Error closing pipe for ldd command: %s\n", strerror(errno));
   } else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
       fprintf(stderr, "Warning: ldd command exited with status %d\n", WEXITSTATUS(status));
   }

   // Add NULL terminator to the deps array
   if (deps) {
       deps[dep_count] = NULL;
   } else {
       // Ensure deps_out is NULL if no dependencies were found/added
       deps = malloc(sizeof(char*));
       if(deps) deps[0] = NULL;
       else {
           fprintf(stderr, "Memory allocation failed for empty dependency array\n");
           *deps_out = NULL;
           return -1;
       }
   }

   *deps_out = deps;
   return dep_count;
}



// Add /proc/boot and system libraries and binaries to store
int add_boot_libraries(void) {
    const char* system_paths[] = {"/proc/boot", "/system/lib", NULL};
    const char* bin_paths[] = {"/system/bin", "/proc/boot", NULL};
    int total_count = 0;

    // First pass: Add all libraries from /proc/boot and /system/lib
    printf("First pass: Adding libraries...\n");
    for (const char** sys_path = system_paths; *sys_path != NULL; sys_path++) {
        DIR* dir = opendir(*sys_path);
        if (!dir) {
            fprintf(stderr, "Failed to open %s: %s\n", *sys_path, strerror(errno));
            continue;
        }

        int path_count = 0;
        struct dirent* entry;
        char path[PATH_MAX];

        printf("Scanning %s for libraries...\n", *sys_path);
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            // Only process libraries in first pass
            if (strstr(entry->d_name, ".so") != NULL) {
                int path_len = snprintf(path, PATH_MAX, "%s/%s", *sys_path, entry->d_name);
                if (path_len < 0 || path_len >= PATH_MAX) {
                    fprintf(stderr, "  Skipping, path too long: %s\n", entry->d_name);
                    continue;
                }

                printf("  Adding library: %s\n", path);
                if (add_to_store(path, entry->d_name, 0) == 0) {
                    path_count++;
                } else {
                    fprintf(stderr, "  Failed to add %s to store.\n", path);
                }
            }
        }
        closedir(dir);
        printf("Added %d libraries from %s to the store.\n", path_count, *sys_path);
        total_count += path_count;
    }

    // Second pass: Add all binaries with proper dependency mapping
    printf("\nSecond pass: Adding all binaries...\n");
    for (const char** bin_path = bin_paths; *bin_path != NULL; bin_path++) {
        DIR* dir = opendir(*bin_path);
        if (!dir) {
            fprintf(stderr, "Failed to open %s: %s\n", *bin_path, strerror(errno));
            continue;
        }

        int bin_count = 0;
        struct dirent* entry;
        
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            // Skip libraries, we only want binaries in this pass
            if (strstr(entry->d_name, ".so") != NULL) {
                continue;
            }

            char bin_path_full[PATH_MAX];
            int path_len = snprintf(bin_path_full, PATH_MAX, "%s/%s", *bin_path, entry->d_name);
            if (path_len < 0 || path_len >= PATH_MAX) {
                fprintf(stderr, "  Skipping, path too long: %s\n", entry->d_name);
                continue;
            }

            // Only process executable files
            struct stat st;
            if (stat(bin_path_full, &st) != 0 || !S_ISREG(st.st_mode) || !(st.st_mode & S_IXUSR)) {
                continue;
            }

            printf("  Processing binary: %s\n", entry->d_name);

            // Scan dependencies first
            char** deps = NULL;
            int deps_count = scan_dependencies(bin_path_full, &deps);
            
            if (deps_count >= 0) {
                printf("  Found %d dependencies for %s\n", deps_count, entry->d_name);
                // Add with dependencies
                if (add_to_store_with_deps(bin_path_full, entry->d_name, (const char**)deps, deps_count) == 0) {
                    bin_count++;
                    printf("  Successfully added %s with dependencies\n", entry->d_name);
                } else {
                    fprintf(stderr, "  Failed to add %s with dependencies\n", entry->d_name);
                }
                
                // Clean up deps
                if (deps) {
                    for (int i = 0; i < deps_count; i++) {
                        if (deps[i]) free(deps[i]);
                    }
                    free(deps);
                }
            } else {
                fprintf(stderr, "  Failed to scan dependencies for %s\n", entry->d_name);
            }
        }
        closedir(dir);
        printf("Added %d binaries from %s to the store.\n", bin_count, *bin_path);
        total_count += bin_count;
    }

    printf("Added total %d items to the store.\n", total_count);
    return total_count;
}

// Helper function to create a wrapper script
static int create_wrapper_script(const char* script_path, const char* target_executable, const char* store_path) {
    // Extract the profile path from script_path
    char profile_path[PATH_MAX];
    char* bin_pos = strstr(script_path, "/bin/");
    if (!bin_pos) {
        fprintf(stderr, "Failed to determine profile path from script path: %s\n", script_path);
        return -1;
    }
    size_t profile_len = bin_pos - script_path;
    if (profile_len >= PATH_MAX) {
        fprintf(stderr, "Profile path too long\n");
        return -1;
    }
    strncpy(profile_path, script_path, profile_len);
    profile_path[profile_len] = '\0';

    FILE* f = fopen(script_path, "w");
    if (!f) {
        fprintf(stderr,"Failed to open wrapper script %s for writing: %s\n", script_path, strerror(errno));
        return -1;
    }

    // Write shebang and header for Nix store bash
    fprintf(f, "#!/data/nix/store/c0ea1e8f1446cfa89963b8c6f507a2048768cf5d786f25166e969018f198ba22-bash/bin/bash\n");
    fprintf(f, "# Wrapper for '%s'\n\n", target_executable);
    fprintf(f, "export PATH=\"%s/bin\"\n", profile_path);
    fprintf(f, "export LD_LIBRARY_PATH=\"/data/nix/store/186e6f5af0a93da0a6e23978adefded62488bcde51f20c8a5e1012781ac6c25c-libncursesw.so.1:/data/nix/store/da7c0bc28f9c338b77f7ab0a9a1c12d64d0e37b7d8ca1b0ddf7092754d1c7028-libintl.so.1:/data/nix/store/132445306ab076fde62c7e5ae9d395563b11867d640d53b829e8a034ce5e9b20-libiconv.so.1:/data/nix/store/9f0c5e501bed08687a2d2d1244b3b9336e5e76227db113bacf50cc5c4d404e60-libc.so.6:/data/nix/store/7cd20568963b07497789a9ba47635bcb21cce11476c3d9d67163c7748fb3a6f9-libregex.so.1:/data/nix/store/92cc1c04c0b5f1af885e0294b36189e1fafc551f913038f78970158ca198c89b-libgcc_s.so.1\"\n");
    fprintf(f, "exec \"%s\" \"$@\"\n", target_executable);

    if (fclose(f) != 0) {
        fprintf(stderr,"Failed to close wrapper script %s: %s\n", script_path, strerror(errno));
        remove(script_path);
        return -1;
    }

    if (chmod(script_path, 0755) == -1) {
        fprintf(stderr,"Failed to make wrapper script %s executable: %s\n", script_path, strerror(errno));
        remove(script_path);
        return -1;
    }

    return 0;
}

// Helper function to create library symlinks
static int create_library_symlinks(const char* store_path, const char* profile_lib_dir) {
    printf("Creating library symlinks...\n");
    printf("  Store path: %s\n", store_path);
    printf("  Profile lib dir: %s\n", profile_lib_dir);

    // Get all dependencies
    char** all_deps = db_get_references(store_path);
    if (!all_deps) {
        printf("  No dependencies found for %s\n", store_path);
        return 0;
    }

    int result = 0;
    const char* search_dirs[] = {"lib", "bin", NULL};

    // First, create symlinks for the libraries in the store path itself
    for (const char** dir_type = search_dirs; *dir_type != NULL; dir_type++) {
        char src_lib_dir[PATH_MAX];
        snprintf(src_lib_dir, PATH_MAX, "%s/%s", store_path, *dir_type);
        printf("  Looking for libraries in package: %s\n", src_lib_dir);
        
        DIR* dir = opendir(src_lib_dir);
        if (!dir) continue;

        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            
            if (strstr(entry->d_name, ".so") == NULL)
                continue;

            char lib_src[PATH_MAX], lib_dest[PATH_MAX];
            snprintf(lib_src, PATH_MAX, "%s/%s/%s", store_path, *dir_type, entry->d_name);
            snprintf(lib_dest, PATH_MAX, "%s/%s", profile_lib_dir, entry->d_name);
            
            printf("  Processing package library: %s\n", entry->d_name);
            printf("    Source: %s\n", lib_src);
            printf("    Dest: %s\n", lib_dest);
            
            struct stat st;
            if (stat(lib_src, &st) != 0) continue;
            
            if (unlink(lib_dest) == -1 && errno != ENOENT) {
                printf("    Warning: Failed to remove existing link: %s\n", strerror(errno));
            }
            
            if (symlink(lib_src, lib_dest) == -1) {
                fprintf(stderr, "    Failed to create symlink for %s: %s\n", 
                        entry->d_name, strerror(errno));
                result = -1;
            } else {
                printf("    Created library symlink: %s -> %s\n", lib_dest, lib_src);
            }
        }
        closedir(dir);
    }

    // Then process dependencies
    for (int i = 0; all_deps[i] != NULL; i++) {
        printf("  Processing dependency: %s\n", all_deps[i]);
        
        for (const char** dir_type = search_dirs; *dir_type != NULL; dir_type++) {
            char dep_lib_dir[PATH_MAX];
            snprintf(dep_lib_dir, PATH_MAX, "%s/%s", all_deps[i], *dir_type);
            printf("  Looking for libraries in: %s\n", dep_lib_dir);
            
            DIR* dir = opendir(dep_lib_dir);
            if (!dir) continue;

            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                    continue;
                    
                if (strstr(entry->d_name, ".so") == NULL)
                    continue;

                char lib_src[PATH_MAX], lib_dest[PATH_MAX];
                snprintf(lib_src, PATH_MAX, "%s/%s/%s", all_deps[i], *dir_type, entry->d_name);
                snprintf(lib_dest, PATH_MAX, "%s/%s", profile_lib_dir, entry->d_name);
                
                printf("  Processing library: %s\n", entry->d_name);
                printf("    Source: %s\n", lib_src);
                printf("    Dest: %s\n", lib_dest);
                
                struct stat st;
                if (stat(lib_src, &st) != 0) continue;
                
                if (unlink(lib_dest) == -1 && errno != ENOENT) {
                    printf("    Warning: Failed to remove existing link: %s\n", strerror(errno));
                }
                
                if (symlink(lib_src, lib_dest) == -1) {
                    fprintf(stderr, "    Failed to create symlink for %s: %s\n", 
                            entry->d_name, strerror(errno));
                    result = -1;
                } else {
                    printf("    Created library symlink: %s -> %s\n", lib_dest, lib_src);
                }
            }
            closedir(dir);
        }
    }

    // Clean up
    for (int i = 0; all_deps[i] != NULL; i++) {
        free(all_deps[i]);
    }
    free(all_deps);

    return result;
}

// Install a store path into a profile
int install_to_profile(const char* store_path, const char* profile_name) {
    printf("Installing %s into profile '%s'\n", store_path, profile_name);
    int ret_val;
    struct timespec ts;
    time_t current_time;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        current_time = time(NULL);
    } else {
        current_time = ts.tv_sec;
    }

    // 1. Always backup current profile as a new generation before modifying
    char profile_path[PATH_MAX];
    char backup_path[PATH_MAX];
    ret_val = snprintf(profile_path, PATH_MAX, "/data/nix/profiles/%s", profile_name);
    if (ret_val < 0 || ret_val >= PATH_MAX) {
        fprintf(stderr, "Error: Profile path too long\n");
        return -1;
    }
    ret_val = snprintf(backup_path, PATH_MAX, "/data/nix/profiles/%s-%ld", profile_name, current_time);
    if (ret_val < 0 || ret_val >= PATH_MAX) {
        fprintf(stderr, "Error: Backup path too long\n");
        return -1;
    }
    struct stat st;
    if (stat(profile_path, &st) == 0) {
        // Profile exists, recursively copy to backup
        char cp_cmd[PATH_MAX * 3];
        snprintf(cp_cmd, sizeof(cp_cmd), "cp -rP %s/. %s/", profile_path, backup_path);
        if (mkdir(backup_path, 0755) == -1 && errno != EEXIST) {
            fprintf(stderr, "Failed to create backup directory: %s\n", strerror(errno));
            return -1;
        }
        if (system(cp_cmd) != 0) {
            fprintf(stderr, "Failed to create backup of profile: %s\n", strerror(errno));
            return -1;
        }
        printf("Created generation: %s\n", backup_path);
    }

    // Create new profile directory if it doesn't exist
    if (mkdir(profile_path, 0755) == -1 && errno != EEXIST) {
        fprintf(stderr, "Failed to create profile directory %s: %s\n", profile_path, strerror(errno));
        return -1;
    }

    // Create standard profile subdirectories
    const char* subdirs[] = {"bin", "lib", "share", "etc", NULL};
    for (int i = 0; subdirs[i] != NULL; i++) {
        char subdir_path[PATH_MAX];
        ret_val = snprintf(subdir_path, PATH_MAX, "%s/%s", profile_path, subdirs[i]);
        if (ret_val < 0 || ret_val >= PATH_MAX) continue;
        
        if (mkdir(subdir_path, 0755) == -1 && errno != EEXIST) {
            fprintf(stderr, "Warning: Failed to create %s directory: %s\n", subdir_path, strerror(errno));
        }
    }

    // Create lib directory and symlinks first
    char profile_lib_dir[PATH_MAX];
    ret_val = snprintf(profile_lib_dir, PATH_MAX, "%s/lib", profile_path);
    if (ret_val < 0 || ret_val >= PATH_MAX) {
        fprintf(stderr, "Error: Profile lib directory path too long\n");
        return -1;
    }

    // Create library symlinks
    if (create_library_symlinks(store_path, profile_lib_dir) != 0) {
        fprintf(stderr, "Warning: Some library symlinks could not be created\n");
    }

    // Process each subdirectory
    for (int i = 0; subdirs[i] != NULL; i++) {
        char source_subdir_path[PATH_MAX];
        char profile_subdir_path[PATH_MAX];

        ret_val = snprintf(source_subdir_path, PATH_MAX, "%s/%s", store_path, subdirs[i]);
        if (ret_val < 0 || ret_val >= PATH_MAX) continue;

        ret_val = snprintf(profile_subdir_path, PATH_MAX, "%s/%s", profile_path, subdirs[i]);
        if (ret_val < 0 || ret_val >= PATH_MAX) continue;

        DIR* dir = opendir(source_subdir_path);
        if (!dir) continue;

        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

            char source_item_path[PATH_MAX];
            char profile_item_path[PATH_MAX];

            ret_val = snprintf(source_item_path, PATH_MAX, "%s/%s", source_subdir_path, entry->d_name);
            if (ret_val < 0 || ret_val >= PATH_MAX) continue;

            ret_val = snprintf(profile_item_path, PATH_MAX, "%s/%s", profile_subdir_path, entry->d_name);
            if (ret_val < 0 || ret_val >= PATH_MAX) continue;

            // Remove existing item if it exists
            if (unlink(profile_item_path) == -1 && errno != ENOENT) {
                fprintf(stderr, "Warning: could not remove existing item %s: %s\n", profile_item_path, strerror(errno));
            }

            struct stat item_st;
            if (stat(source_item_path, &item_st) != 0) continue;

            if (strcmp(subdirs[i], "bin") == 0 && S_ISREG(item_st.st_mode)) {
                // Create wrapper script for binaries
                if (create_wrapper_script(profile_item_path, source_item_path, store_path) != 0) {
                    fprintf(stderr, "Failed to create wrapper script for %s\n", entry->d_name);
                    continue;
                }
                printf("Created wrapper script for %s\n", entry->d_name);
            } else {
                // For non-binaries, create direct symlinks
                if (symlink(source_item_path, profile_item_path) == -1) {
                    fprintf(stderr, "Failed to create symlink for %s: %s\n", entry->d_name, strerror(errno));
                }
            }
        }
        closedir(dir);
    }

    // Ensure /bin symlink exists in the profile root
    char bin_symlink[PATH_MAX];
    snprintf(bin_symlink, sizeof(bin_symlink), "%s/bin", profile_path);
    struct stat st_binlink;
    if (lstat("/bin", &st_binlink) == -1 || !S_ISLNK(st_binlink.st_mode)) {
        // Remove if a file/dir exists at /bin in the chroot
        unlink("/bin");
        symlink("bin", "/bin");
    }

    // Handle generations
    QnixConfig* cfg = config_get();
    if (cfg->profiles.max_generations > 0) {
        cleanup_old_generations(profile_name);
    }

    printf("Installation to profile '%s' complete.\n", profile_name);

    // Mark the installed package as a GC root to prevent GC
    db_add_root(store_path);

    // After modification, save the new state as a generation
    struct timespec ts_post;
    time_t post_time;
    if (clock_gettime(CLOCK_REALTIME, &ts_post) == -1) {
        post_time = time(NULL);
    } else {
        post_time = ts_post.tv_sec;
    }
    char postgen_path[PATH_MAX];
    snprintf(postgen_path, PATH_MAX, "/data/nix/profiles/%s-%ld", profile_name, post_time);
    char cp_cmd[PATH_MAX * 3];
    snprintf(cp_cmd, sizeof(cp_cmd), "cp -rP %s/. %s/", profile_path, postgen_path);
    if (mkdir(postgen_path, 0755) == -1 && errno != EEXIST) {
        fprintf(stderr, "Failed to create post-modification generation directory: %s\n", strerror(errno));
        // Not fatal, continue
    } else {
        if (system(cp_cmd) != 0) {
            fprintf(stderr, "Failed to create post-modification generation: %s\n", strerror(errno));
        } else {
            printf("Created generation (after modification): %s\n", postgen_path);
        }
    }

    return 0;
}

// Helper function to cleanup old generations
void cleanup_old_generations(const char* profile_name) {
    DIR* dir = opendir("/data/nix/profiles");
    if (!dir) {
        fprintf(stderr, "Warning: Could not open profiles directory to clean old generations\n");
        return;
    }

    struct dirent* entry;
    time_t* timestamps = NULL;
    int count = 0;
    int capacity = 8;

    // Allocate initial array
    timestamps = malloc(capacity * sizeof(time_t));
    if (!timestamps) {
        fprintf(stderr, "Memory allocation failed for timestamps array\n");
        closedir(dir);
        return;
    }

    // Collect all generation timestamps
    size_t prefix_len = strlen(profile_name);
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, profile_name, prefix_len) == 0 && 
            entry->d_name[prefix_len] == '-') {
            
            // Ensure array has space
            if (count >= capacity) {
                capacity *= 2;
                time_t* new_timestamps = realloc(timestamps, capacity * sizeof(time_t));
                if (!new_timestamps) {
                    fprintf(stderr, "Memory reallocation failed for timestamps array\n");
                    free(timestamps);
                    closedir(dir);
                    return;
                }
                timestamps = new_timestamps;
            }

            // Extract and store timestamp
            timestamps[count++] = atol(entry->d_name + prefix_len + 1);
        }
    }
    closedir(dir);

    if (count == 0) {
        free(timestamps);
        return;  // No generations to clean up
    }

    // Sort timestamps in descending order (newest first)
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (timestamps[j] > timestamps[i]) {
                time_t temp = timestamps[i];
                timestamps[i] = timestamps[j];
                timestamps[j] = temp;
            }
        }
    }

    // Get max_generations from config
    QnixConfig* cfg = config_get();
    int max_gens = cfg->profiles.max_generations;
    
    if (max_gens <= 0 || count <= max_gens) {
        free(timestamps);
        return;  // Nothing to clean up
    }

    // Remove excess generations
    printf("Cleaning up old generations for profile '%s'...\n", profile_name);
    for (int i = max_gens; i < count; i++) {
        char gen_path[PATH_MAX];
        snprintf(gen_path, PATH_MAX, "/data/nix/profiles/%s-%ld", profile_name, timestamps[i]);
        
        printf("  Removing old generation: %s\n", gen_path);
        char rm_cmd[PATH_MAX + 10];
        snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf %s", gen_path);
        
        if (system(rm_cmd) != 0) {
            fprintf(stderr, "Warning: Failed to remove old generation: %s\n", gen_path);
        }
    }

    free(timestamps);
    printf("Cleanup complete. Kept %d most recent generations.\n", max_gens);
}

// Create a new profile
int create_profile(const char* profile_name) {
   if (!profile_name || strlen(profile_name) == 0) {
       fprintf(stderr, "Invalid profile name\n");
       return -1;
   }

   // First, create a store path for the profile
   char* store_path = compute_store_path(profile_name, NULL, NULL);
   if (!store_path) {
       fprintf(stderr, "Failed to compute store path for profile\n");
       return -1;
   }

   // Create the profile directory in the store
   if (mkdir(store_path, 0755) == -1 && errno != EEXIST) {
       fprintf(stderr, "Failed to create profile directory in store: %s\n", strerror(errno));
       free(store_path);
       return -1;
   }

   // Create standard profile subdirectories
   const char* subdirs[] = {"bin", "lib", "share", "etc", NULL};
   for (int i = 0; subdirs[i]; i++) {
       char subdir_path[PATH_MAX];
       int ret = snprintf(subdir_path, PATH_MAX, "%s/%s", store_path, subdirs[i]);
       if (ret < 0 || ret >= PATH_MAX) {
           fprintf(stderr, "Warning: Subdirectory path too long for %s/%s\n", store_path, subdirs[i]);
           continue;
       }
       if (mkdir(subdir_path, 0755) == -1 && errno != EEXIST) {
           fprintf(stderr, "Warning: Could not create %s\n", subdir_path);
       }
   }

   // Essential utils that need to be available in every profile
   const char* essential_utils[] = {
       "/data/nix/store/c0ea1e8f1446cfa89963b8c6f507a2048768cf5d786f25166e969018f198ba22-bash/bin/bash",
       "/data/nix/store/3b49910435edf96139956b29ac57e4b36eeab94eea7ec18abb4deb4473f12645-sh/bin/sh",
       "/data/nix/store/91dee820abb49d9963d6e03d897fcce20bdbda09672364a47828683d27bd8c47-ls/bin/ls",
       "/data/nix/store/46f168a2c838c963b76e838ac616bde08f45a5d2934ffbfcbfd4b5a06028b820-pwd/bin/pwd",
       "/data/nix/store/05522cef98bf1130ca2ee50d6791ddd4ff8ba75f5a247c3e35bf2aa1661f3a04-cp/bin/cp",
       "/data/nix/store/209992074ba6caccee689fd209f95b2821cf8bfae6cacef1a1c8e252fb85ccf2-mkdir/bin/mkdir",
       "/data/nix/store/7979fba36f732e23f41e76c7d2689ecc70853b0b63a7032d173c0e9488328e58-rm/bin/rm",
       "/data/nix/store/a3b539c603434fadaa1f58bc31f28da5d7e28c9076670d042f7d4dcb3c90aa7e-cat/bin/cat",
       "/data/nix/store/9c18257a6e51b183a471fe5600aaf9a4088a1b70f8c0a4a5337b5240581cb0aa-which/bin/which",
       "/data/nix/store/6373d1492ad9e22588c3b012af924d8deb0d5ce38bc1a7aec3556fcdab7bce7a-echo/bin/echo",
       "/data/nix/store/76d7d6c525e363e7d4b62a7e183dd449f857cc1f7a2ff1006f4aa6fe1ba4a7e4-dirname/bin/dirname",
       "/data/nix/store/befb801214e16a84a5ccf99fb23eb13f4a0942744e9a7cdafb3bed013d110fd3-ldd/bin/ldd",
       "/data/nix/store/171732c88c2ec49790c25841ee62ea1b394751dd9fa0139b4f8309c70f37958c-env/bin/env",
       NULL
   };

   // Add essential utils to store and install them in the profile
   printf("Adding essential utilities from Nix store to profile...\n");
   for (int i = 0; essential_utils[i] != NULL; i++) {
       const char* util_path = essential_utils[i];
       if (!path_exists(util_path)) {
           fprintf(stderr, "Warning: Essential utility not found in Nix store: %s\n", util_path);
           continue;
       }
       // Copy utility to store (if not already present)
       add_to_store_with_deps(util_path, basename((char*)util_path), NULL, 0);

       // Always install the utility into the profile
       // Find the store path root (strip /bin/...) for install_to_profile
       char* util_store_path = strdup(util_path);
       if (util_store_path) {
           char* bin_pos = strstr(util_store_path, "/bin/");
           if (bin_pos) *bin_pos = '\0';
           install_to_profile(util_store_path, profile_name);
           // Mark the base store path as a GC root
           db_add_root(util_store_path);
           free(util_store_path);
       }
   }

   // Register the store path in the database
   db_register_path(store_path, NULL);

   // Create the profile symlink
   char profile_path[PATH_MAX];
   int ret = snprintf(profile_path, PATH_MAX, "/data/nix/profiles/%s", profile_name);
   if (ret < 0 || ret >= PATH_MAX) {
       fprintf(stderr, "Profile path too long\n");
       free(store_path);
       return -1;
   }

   // Remove existing profile if it exists
   unlink(profile_path); // Ignore errors

   // Create the symlink
   if (symlink(store_path, profile_path) == -1) {
       fprintf(stderr, "%s\n", strerror(errno));
       free(store_path);
       return -1;
   }

   // Register as root after symlink is created
   ret = db_add_root(store_path);
   free(store_path);

   return ret;
}

// Switch to a different profile
int switch_profile(const char* profile_name) {
   char profile_path[PATH_MAX];
   char current_link[PATH_MAX];
   
   // Construct paths
   snprintf(profile_path, PATH_MAX, "/data/nix/profiles/%s", profile_name);
   snprintf(current_link, PATH_MAX, "/data/nix/profiles/current");

   // Verify target exists BEFORE making any changes
   struct stat st;
   if (stat(profile_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
       fprintf(stderr, "Profile '%s' does not exist or is not a directory\n", profile_name);
       return -1;
   }

   // The atomicity comes from these two operations:
   // 1. First remove the old link (if it exists)
   unlink(current_link);  // Even if this fails, we're still safe
   
   // 2. Create the new link - this is atomic on Unix systems
   if (symlink(profile_path, current_link) == -1) {
       // If this fails, no partial state exists
       fprintf(stderr, "Failed to create new current profile link: %s\n", strerror(errno));
       return -1;
   }

   return 0;
}

// List all available profiles
ProfileInfo* list_profiles(int* count) {
   DIR* dir;
   struct dirent* entry;
   ProfileInfo* profiles = NULL;
   int capacity = 0;
   *count = 0;

   dir = opendir("/data/nix/profiles");
   if (!dir) {
       fprintf(stderr, "Failed to open profiles directory: %s\n", strerror(errno));
       return NULL;
   }

   while ((entry = readdir(dir)) != NULL) {
       if (entry->d_name[0] == '.' || strcmp(entry->d_name, "current") == 0) {
           continue;
       }

       char full_path[PATH_MAX];
       snprintf(full_path, PATH_MAX, "/data/nix/profiles/%s", entry->d_name);

       struct stat st;
       if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
           if (*count >= capacity) {
               capacity = (capacity == 0) ? 8 : capacity * 2;
               ProfileInfo* new_profiles = realloc(profiles, capacity * sizeof(ProfileInfo));
               if (!new_profiles) {
                   fprintf(stderr, "Memory allocation failed\n");
                   free_profile_info(profiles, *count);
                   closedir(dir);
                   return NULL;
               }
               profiles = new_profiles;
           }

           profiles[*count].name = strdup(entry->d_name);
           strncpy(profiles[*count].path, full_path, PATH_MAX - 1);
           profiles[*count].path[PATH_MAX - 1] = '\0';
           profiles[*count].timestamp = st.st_mtime;
           (*count)++;
       }
   }

   closedir(dir);
   return profiles;
}

void free_profile_info(ProfileInfo* profiles, int count) {
   if (profiles) {
       for (int i = 0; i < count; i++) {
           free(profiles[i].name);
       }
       free(profiles);
   }
}

// Add new rollback functions
int rollback_profile(const char* profile_name) {
   char profile_path[PATH_MAX];
   char latest_path[PATH_MAX] = {0};
   char state_file[PATH_MAX];
   time_t current_generation = 0;
   time_t current_time;
   struct timespec ts;
   
   // Get accurate system time
   if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
       current_time = time(NULL);
   } else {
       current_time = ts.tv_sec;
   }
   
   // Get profile paths
   snprintf(profile_path, PATH_MAX, "/data/nix/profiles/%s", profile_name);
   snprintf(state_file, PATH_MAX, "/data/nix/profiles/.%s.current", profile_name);

   // Check if profile exists
   struct stat st;
   if (stat(profile_path, &st) != 0) {
       fprintf(stderr, "Profile '%s' does not exist\n", profile_name);
       return -1;
   }

   // Read current generation from state file if it exists
   FILE* sf = fopen(state_file, "r");
   if (sf) {
       if (fscanf(sf, "%ld", &current_generation) != 1) {
           current_generation = 0;
       }
       fclose(sf);
   }

   // Find previous generation by looking for profile-<timestamp> with highest timestamp less than current
   DIR* dir = opendir("/data/nix/profiles");
   struct dirent* entry;
   time_t latest_time = 0;

   if (!dir) {
       fprintf(stderr, "Failed to open profiles directory\n");
       return -1;
   }

   // First pass: find the current generation's timestamp if we don't have it
   if (current_generation == 0) {
       while ((entry = readdir(dir)) != NULL) {
           if (strncmp(entry->d_name, profile_name, strlen(profile_name)) == 0 && 
               entry->d_name[strlen(profile_name)] == '-') {
               time_t gen_time = atol(entry->d_name + strlen(profile_name) + 1);
               if (gen_time > latest_time && gen_time < current_time) {
                   latest_time = gen_time;
               }
           }
       }
       current_generation = latest_time;
       rewinddir(dir);
       latest_time = 0;
   }

   // Second pass: find the next oldest generation
   while ((entry = readdir(dir)) != NULL) {
       if (strncmp(entry->d_name, profile_name, strlen(profile_name)) == 0 && 
           entry->d_name[strlen(profile_name)] == '-') {
           time_t gen_time = atol(entry->d_name + strlen(profile_name) + 1);
           if (gen_time < current_generation && gen_time > latest_time) {
               latest_time = gen_time;
               snprintf(latest_path, PATH_MAX, "/data/nix/profiles/%s", entry->d_name);
           }
       }
   }
   closedir(dir);

   if (latest_time == 0) {
       fprintf(stderr, "No previous generation found before %ld\n", current_generation);
       return -1;
   }

   // Format timestamp nicely
   char timestamp_str[32];
   struct tm *tm_info = localtime(&latest_time);
   strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%d %H:%M:%S", tm_info);

   // Remove current profile
   char rm_cmd[PATH_MAX + 10];
   snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf %s", profile_path);
   if (system(rm_cmd) != 0) {
       fprintf(stderr, "Failed to remove current profile\n");
       return -1;
   }

   // Copy previous generation to main profile
   char cp_cmd[PATH_MAX * 3];
   snprintf(cp_cmd, sizeof(cp_cmd), "cp -rP %s/. %s/", latest_path, profile_path);
   if (system(cp_cmd) != 0) {
       fprintf(stderr, "Failed to rollback to generation %s\n", latest_path);
       return -1;
   }

   // Update state file with new current generation
   sf = fopen(state_file, "w");
   if (sf) {
       fprintf(sf, "%ld", latest_time);
       fclose(sf);
   }

   printf("Profile '%s' rolled back to generation: %s\n", 
          profile_name, timestamp_str);

   // Show what's in the rollback
   DIR* content_dir = opendir(profile_path);
   if (content_dir) {
       printf("Profile now contains:\n");
       while ((entry = readdir(content_dir)) != NULL) {
           if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
               continue;
           printf("  %s\n", entry->d_name);
       }
       closedir(content_dir);
   }

   return 0;
}

int get_profile_generations(const char* profile_name, time_t** timestamps, int* count) {
   DIR* dir = opendir("/data/nix/profiles");
   if (!dir) {
       fprintf(stderr, "Failed to open profiles directory\n");
       return -1;
   }

   // First count generations
   struct dirent* entry;
   *count = 0;
   while ((entry = readdir(dir)) != NULL) {
       if (strncmp(entry->d_name, profile_name, strlen(profile_name)) == 0 && 
           entry->d_name[strlen(profile_name)] == '-') {
           (*count)++;
       }
   }

   // Allocate array
   *timestamps = malloc(sizeof(time_t) * (*count));
   if (!*timestamps) {
       closedir(dir);
       return -1;
   }

   // Reset directory and fill array
   rewinddir(dir);
   int i = 0;
   while ((entry = readdir(dir)) != NULL) {
       if (strncmp(entry->d_name, profile_name, strlen(profile_name)) == 0 && 
           entry->d_name[strlen(profile_name)] == '-') {
           (*timestamps)[i++] = atol(entry->d_name + strlen(profile_name) + 1);
       }
   }

   closedir(dir);

   // Sort timestamps in reverse order
   for (int i = 0; i < *count - 1; i++) {
       for (int j = i + 1; j < *count; j++) {
           if ((*timestamps)[i] < (*timestamps)[j]) {
               time_t temp = (*timestamps)[i];
               (*timestamps)[i] = (*timestamps)[j];
               (*timestamps)[j] = temp;
           }
       }
   }

   return 0;
}

int switch_profile_generation(const char* profile_name, time_t timestamp) {
   char profile_path[PATH_MAX];
   char gen_path[PATH_MAX];
   char backup_path[PATH_MAX];
   time_t current_time;
   struct timespec ts;
   
   // Get accurate system time
   if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
       current_time = time(NULL);
   } else {
       current_time = ts.tv_sec;
   }
   
   snprintf(profile_path, PATH_MAX, "/data/nix/profiles/%s", profile_name);
   snprintf(gen_path, PATH_MAX, "/data/nix/profiles/%s-%ld", profile_name, timestamp);
   snprintf(backup_path, PATH_MAX, "/data/nix/profiles/%s-%ld", profile_name, current_time);

   // Verify generation exists
   struct stat st;
   if (stat(gen_path, &st) != 0) {
       fprintf(stderr, "Generation %ld does not exist\n", timestamp);
       return -1;
   }

   // Create backup of current profile if it exists
   if (stat(profile_path, &st) == 0) {
       char cp_cmd[PATH_MAX * 3];
       snprintf(cp_cmd, sizeof(cp_cmd), "cp -rP %s/. %s/", profile_path, backup_path);
       if (system(cp_cmd) != 0) {
           fprintf(stderr, "Failed to create backup before switching generations\n");
           return -1;
       }
   }

   // Remove current profile
   char rm_cmd[PATH_MAX + 10];
   snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf %s", profile_path);
   if (system(rm_cmd) != 0) {
       fprintf(stderr, "Failed to remove current profile\n");
       return -1;
   }

   // Copy generation to profile path
   char cp_cmd[PATH_MAX * 3];
   snprintf(cp_cmd, sizeof(cp_cmd), "cp -rP %s/. %s/", gen_path, profile_path);
   if (system(cp_cmd) != 0) {
       fprintf(stderr, "Failed to switch to generation %ld\n", timestamp);
       // Try to restore backup
       if (stat(backup_path, &st) == 0) {
           snprintf(cp_cmd, sizeof(cp_cmd), "cp -rP %s/. %s/", backup_path, profile_path);
           system(cp_cmd);
       }
       return -1;
   }

   printf("Switched profile '%s' to generation from %s", profile_name, ctime(&timestamp));
   return 0;
}
