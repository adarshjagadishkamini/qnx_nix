/*
 * nix_store.c - Core implementation of Nix-like store for QNX
 */
#include "nix_store.h" // Includes stdio, stdlib, string, sys/stat, sys/types, unistd, errno
#include "sha256.h"
#include <limits.h>
#include <fcntl.h>
#include <dirent.h> // <-- Added this in previous step for add_boot_libraries
#include "nix_store_db.h"
#include <ctype.h>

// Define NIX_STORE_PATH if not defined in nix_store.h
#ifndef NIX_STORE_PATH
#define NIX_STORE_PATH "/data/nix/store"
#endif

// Initialize the store directory structure
int store_init(void) {
    // Create the path hierarchy
    const char* path_parts[] = {"/data", "/data/nix", "/data/nix/store"};

    for (int i = 0; i < 3; i++) {
        struct stat st = {0};
        if (stat(path_parts[i], &st) == -1) {
            if (mkdir(path_parts[i], 0755) == -1) {
                fprintf(stderr, "Failed to create directory %s: %s\n",
                        path_parts[i], strerror(errno));
                return -1;
            }
        }
    }

    // Create database directory
    char db_path[PATH_MAX];
    snprintf(db_path, PATH_MAX, "%s/.nix-db", NIX_STORE_PATH);

    struct stat st = {0};
    if (stat(db_path, &st) == -1) {
        if (mkdir(db_path, 0755) == -1) {
            fprintf(stderr, "Failed to create database directory: %s\n",
                    strerror(errno));
            return -1;
        }
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

    snprintf(result, PATH_MAX, "%s/%s-%s", NIX_STORE_PATH, hash_result, name);
    free(hash_result);

    return result;
}

// *** THIS FUNCTION IS DEPRECATED BY scan_dependencies ***
// Kept for reference or potential future use if needed.
char** get_elf_dependencies(const char* path, int* deps_count) {
    fprintf(stderr, "Warning: get_elf_dependencies is deprecated, use scan_dependencies.\n");
    *deps_count = 0;
    return NULL;
}
// *** END DEPRECATED FUNCTION ***


// Add a file or directory to the store with explicit dependencies
// (Code from file content_fetcher.fetch: qnx_nix_experimental/nix_store.c)
int add_to_store_with_deps(const char* source_path, const char* name, const char** deps, int deps_count) {
    struct stat st;
    if (stat(source_path, &st) == -1) {
        fprintf(stderr, "Source path does not exist: %s\n", strerror(errno));
        return -1;
    }

    // Ensure all dependencies are in the store first
    char** dep_store_paths = NULL;
    if (deps_count > 0) {
        dep_store_paths = malloc((deps_count + 1) * sizeof(char*));
        if (!dep_store_paths) {
            return -1;
        }

        for (int i = 0; i < deps_count; i++) {
            // First check if the dependency already exists in store
            // This check is simplified - a real implementation would query the DB more robustly
            // Assuming deps[i] is already a store path or needs to be added
             struct stat dep_st;
             if (stat(deps[i], &dep_st) != 0 || strncmp(deps[i], NIX_STORE_PATH, strlen(NIX_STORE_PATH)) != 0) {
                 // If not a valid store path, attempt to add it (this might need refinement)
                 char dep_name[PATH_MAX];
                 char* base_name = strrchr(deps[i], '/');
                 if (base_name) {
                    strncpy(dep_name, base_name + 1, PATH_MAX - 1);
                 } else {
                    strncpy(dep_name, deps[i], PATH_MAX - 1);
                 }
                 dep_name[PATH_MAX - 1] = '\0'; // Ensure null termination

                 printf("Attempting to add missing dependency: %s as %s\n", deps[i], dep_name);

                 if (add_to_store(deps[i], dep_name, 0) != 0) {
                     fprintf(stderr, "Failed to add dependency: %s\n", deps[i]);
                     // Clean up already allocated store paths
                     for (int j = 0; j < i; j++) free(dep_store_paths[j]);
                     free(dep_store_paths);
                     return -1;
                 }
                 // Now compute the store path for the newly added dependency
                 dep_store_paths[i] = compute_store_path(dep_name, NULL, NULL);
                 if (!dep_store_paths[i]) {
                     // Handle allocation failure
                     for (int j = 0; j < i; j++) free(dep_store_paths[j]);
                     free(dep_store_paths);
                     return -1;
                 }

             } else {
                // If dependency already exists and seems like a store path, just copy the path
                dep_store_paths[i] = strdup(deps[i]);
                if (!dep_store_paths[i]) {
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
                free(dep_store_paths[i]);
            }
            free(dep_store_paths);
        }
        return -1;
    }

    // Check if the path already exists in the store
    struct stat store_st;
    if (stat(store_path, &store_st) == 0) {
        // Path already exists in the store
        printf("Path %s already exists in store.\n", store_path);

        // Register dependencies even if path exists (idempotency)
        // DO THIS *BEFORE* freeing store_path
        if (deps_count > 0 && dep_store_paths != NULL) { // Ensure deps_count > 0 and paths allocated
             db_register_path(store_path, (const char**)dep_store_paths);
             // Note: db_register_path should ideally handle potential duplicate registrations gracefully.
        }

        // Now free the resources
        free(store_path); // Free store_path AFTER potentially using it in db_register_path
        if (dep_store_paths) {
            for (int i = 0; i < deps_count; i++) {
                if (dep_store_paths[i]) { // Check pointer before freeing
                   free(dep_store_paths[i]);
                }
            }
            free(dep_store_paths);
        }
        return 0; // Return success, path existed
    }

    // Create the directory for the store item
    if (mkdir(store_path, 0755) == -1) {
         // Check if it already exists (race condition?)
         if (errno != EEXIST) {
            fprintf(stderr, "Failed to create store directory %s: %s\n", store_path, strerror(errno));
            free(store_path);
            if (dep_store_paths) {
                for (int i = 0; i < deps_count; i++) free(dep_store_paths[i]);
                free(dep_store_paths);
            }
            return -1;
         }
    }


    // Copy the file/directory to the store (same as in add_to_store)
    // Determine the final destination path inside the store directory
    char dest_path_in_store[PATH_MAX];
    snprintf(dest_path_in_store, PATH_MAX, "%s/%s", store_path, name);


    if (S_ISDIR(st.st_mode)) {
        // Implement recursive directory copy using QNX functions
        char cmd[PATH_MAX * 3];
        // Copy the directory itself into the store path, not just its contents
        snprintf(cmd, sizeof(cmd), "cp -rP %s %s/", source_path, store_path); // Use -P for no symlink following, -r recursive
        printf("Executing: %s\n", cmd);
        int ret = system(cmd);
        if (ret != 0) {
            fprintf(stderr, "Failed to copy directory %s to %s (system returned %d)\n", source_path, store_path, ret);
            // Consider removing the partially created store path on failure
            rmdir(store_path); // Simple rmdir, might need recursive remove
            free(store_path);
            if (dep_store_paths) {
                 for (int i = 0; i < deps_count; i++) free(dep_store_paths[i]);
                 free(dep_store_paths);
            }
            return -1;
        }
    } else if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) { // Handle regular files and symlinks
        // Copy a regular file or symlink
         char cmd[PATH_MAX * 3];
         // Use cp -P to preserve symlinks if source is a symlink
         snprintf(cmd, sizeof(cmd), "cp -P %s %s", source_path, dest_path_in_store);
         printf("Executing: %s\n", cmd);
         int ret = system(cmd);
         if (ret != 0) {
            fprintf(stderr, "Failed to copy file/link %s to %s (system returned %d)\n", source_path, dest_path_in_store, ret);
            // Consider removing the partially created store path on failure
            remove(dest_path_in_store); // remove works for files/symlinks
            rmdir(store_path);
            free(store_path);
            if (dep_store_paths) {
                 for (int i = 0; i < deps_count; i++) free(dep_store_paths[i]);
                 free(dep_store_paths);
            }
            return -1;
         }
    } else {
         fprintf(stderr, "Unsupported file type for source path: %s\n", source_path);
         rmdir(store_path);
         free(store_path);
         if (dep_store_paths) {
            for (int i = 0; i < deps_count; i++) free(dep_store_paths[i]);
            free(dep_store_paths);
         }
         return -1;
    }


    // Make the store path read-only (apply recursively if directory)
    // This requires a more complex implementation for recursion
    // For simplicity, making just the top-level read-only for now
    make_store_path_read_only(store_path);

    // Register this path in the database with dependencies
    db_register_path(store_path, (const char**)dep_store_paths);

    printf("Added %s to store (%s) with %d dependencies\n", name, store_path, deps_count);

    // Clean up
    free(store_path);
    if (dep_store_paths) {
        for (int i = 0; i < deps_count; i++) {
            free(dep_store_paths[i]);
        }
        free(dep_store_paths);
    }

    return 0;
}


// Add a file or directory to the store
// (Code from file content_fetcher.fetch: qnx_nix_experimental/nix_store.c)
int add_to_store(const char* source_path, const char* name, int recursive) {
    struct stat st;
    if (stat(source_path, &st) == -1) {
        // If source is a broken symlink, stat might fail. Handle this?
        // For now, treat as error.
        fprintf(stderr, "Source path does not exist or is inaccessible: %s (%s)\n", source_path, strerror(errno));
        return -1;
    }

    // Compute a store path for this item
    char* store_path = compute_store_path(name, NULL, NULL);
    if (!store_path) {
        return -1;
    }

    // Check if the path already exists in the store
    struct stat store_st;
    if (stat(store_path, &store_st) == 0) {
        // Path already exists in the store
        printf("Path %s already exists in store.\n", store_path);
        free(store_path);
        return 0; // Indicate success, path already present
    }

    // Create the directory for the store item
    if (mkdir(store_path, 0755) == -1) {
         // Check if it already exists (race condition?)
         if (errno != EEXIST) {
            fprintf(stderr, "Failed to create store directory %s: %s\n", store_path, strerror(errno));
            free(store_path);
            return -1;
         }
    }

    // Determine the final destination path inside the store directory
    char dest_path_in_store[PATH_MAX];
    snprintf(dest_path_in_store, PATH_MAX, "%s/%s", store_path, name);


    // Copy the file/directory to the store
    if (S_ISDIR(st.st_mode)) {
        if (!recursive) {
            fprintf(stderr, "Source %s is a directory, but recursive copy not requested.\n", source_path);
            rmdir(store_path); // Clean up created directory
            free(store_path);
            return -1;
        }
        // Implement recursive directory copy using QNX functions
        char cmd[PATH_MAX * 3];
         // Copy the directory itself into the store path, not just its contents
        snprintf(cmd, sizeof(cmd), "cp -rP %s %s/", source_path, store_path); // Use -P for no symlink following, -r recursive
        printf("Executing: %s\n", cmd);
        int ret = system(cmd);
        if (ret != 0) {
            fprintf(stderr, "Failed to copy directory %s to %s (system returned %d)\n", source_path, store_path, ret);
             // Consider removing the partially created store path on failure
             rmdir(store_path); // Simple rmdir, might need recursive remove
             free(store_path);
             return -1;
        }

    } else if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) { // Handle regular files and symlinks
         // Copy a regular file or symlink
         char cmd[PATH_MAX * 3];
         // Use cp -P to preserve symlinks if source is a symlink
         snprintf(cmd, sizeof(cmd), "cp -P %s %s", source_path, dest_path_in_store);
         printf("Executing: %s\n", cmd);
         int ret = system(cmd);
         if (ret != 0) {
            fprintf(stderr, "Failed to copy file/link %s to %s (system returned %d)\n", source_path, dest_path_in_store, ret);
            // Consider removing the partially created store path on failure
            remove(dest_path_in_store); // remove works for files/symlinks
            rmdir(store_path);
            free(store_path);
            return -1;
         }
    } else {
         fprintf(stderr, "Unsupported file type for source path: %s\n", source_path);
         rmdir(store_path);
         free(store_path);
         return -1;
    }


    // Make the store path read-only (apply recursively if directory)
    // This requires a more complex implementation for recursion
    make_store_path_read_only(store_path);


    // Register this path in the database
    const char* references[] = {NULL}; // No explicit references when using simple add_to_store
    db_register_path(store_path, references);

    printf("Added %s to store (%s)\n", name, store_path);
    free(store_path);
    return 0;
}


// Make a store path read-only
// TODO: Implement recursive read-only for directories
int make_store_path_read_only(const char* path) {
    // Change permissions to make it read-only (owner r-x, group r-x, other r-x)
    // Use 0555 for directories/executables, potentially 0444 for non-exec files
    // For simplicity, using 0555 for now.
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
             // Need recursive implementation here
             // For now, just set top-level directory
            if (chmod(path, 0555) == -1) {
                 fprintf(stderr, "Warning: Failed to make directory read-only %s: %s\n", path, strerror(errno));
                // Return success anyway? Or indicate partial failure?
            }
        } else {
            // For files, set read-only, preserve execute bit if already set
            mode_t current_mode = st.st_mode;
            mode_t new_mode = S_IRUSR | S_IRGRP | S_IROTH; // Read for all = 0444
             if (current_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
                 new_mode |= (S_IXUSR | S_IXGRP | S_IXOTH); // Add execute if it was set = 0555
             }
            if (chmod(path, new_mode) == -1) {
                 fprintf(stderr, "Warning: Failed to make file read-only %s: %s\n", path, strerror(errno));
                // Return success anyway?
            }
        }
    } else {
        fprintf(stderr, "Warning: Failed to stat path for read-only %s: %s\n", path, strerror(errno));
    }

    return 0; // Indicate success for now, even on warnings
}

// Verify a store path
// (Code from file content_fetcher.fetch: qnx_nix_experimental/nix_store.c)
int verify_store_path(const char* path) {
    // Basic verification - check if the path exists and is in the store
    struct stat st;
    if (stat(path, &st) == -1) {
         fprintf(stderr, "Verify failed: Path %s does not exist or is inaccessible.\n", path);
        return -1;
    }

    // Check if the path is inside the store
    if (strncmp(path, NIX_STORE_PATH, strlen(NIX_STORE_PATH)) != 0) {
         fprintf(stderr, "Verify failed: Path %s is not within the store directory %s.\n", path, NIX_STORE_PATH);
        return -1;
    }

    // Check if the path is registered in the database
    if (!db_path_exists(path)) {
         fprintf(stderr, "Verify failed: Path %s is not registered in the database.\n", path);
        return -1;
    }

    // TODO: Add hash verification here if hashes are stored in DB

    printf("Path %s verified successfully.\n", path);
    return 0;
}


// =========================================================================
// MODIFIED FUNCTION: scan_dependencies
// Uses 'ldd' instead of 'use' and parses its output.
// =========================================================================
int scan_dependencies(const char* exec_path, char*** deps_out) {
    FILE* pipe;
    char cmd[PATH_MAX + 4]; // +4 for "ldd " + null terminator
    char buffer[1024];      // Buffer for reading lines from ldd output
    char** deps = NULL;      // Dynamically allocated array for dependency paths
    int dep_count = 0;       // Number of dependencies found
    int deps_capacity = 0;   // Current capacity of the deps array

    // Use 'ldd' QNX command to list shared library dependencies
    snprintf(cmd, sizeof(cmd), "ldd %s", exec_path);

    pipe = popen(cmd, "r");
    if (!pipe) {
        fprintf(stderr, "Failed to execute dependency scan command '%s': %s\n", cmd, strerror(errno));
        *deps_out = NULL;
        return -1; // Indicate error
    }

    printf("Scanning dependencies for %s using: %s\n", exec_path, cmd);

    while (fgets(buffer, sizeof(buffer), pipe)) {
        // Look for the "=>" separator indicating a linked library
        char* arrow = strstr(buffer, "=>");
        if (arrow) {
            char* lib_path_start = arrow + 2; // Point to characters after "=>"
            char extracted_path[PATH_MAX];

            // Skip leading whitespace after "=>"
            while (*lib_path_start != '\0' && isspace((unsigned char)*lib_path_start)) {
                lib_path_start++;
            }

            // Find the end of the path (stop at whitespace or '(' or end of string)
            char* lib_path_end = lib_path_start;
            while (*lib_path_end != '\0' &&
                   !isspace((unsigned char)*lib_path_end) &&
                   *lib_path_end != '(') {
                lib_path_end++;
            }

            // Calculate path length
            size_t path_len = lib_path_end - lib_path_start;

            if (path_len > 0 && path_len < PATH_MAX) {
                // Copy the extracted path string
                strncpy(extracted_path, lib_path_start, path_len);
                extracted_path[path_len] = '\0';

                // Basic validation: Check if the path looks like an absolute path
                // and is not the executable itself (ldd might list the exe).
                // Also check if it exists.
                if (extracted_path[0] == '/' && strcmp(extracted_path, exec_path) != 0) {
                    struct stat st;
                    if (stat(extracted_path, &st) == 0 && S_ISREG(st.st_mode)) {
                        // Grow the dependencies array if needed
                        if (dep_count >= deps_capacity) {
                            deps_capacity = (deps_capacity == 0) ? 8 : deps_capacity * 2;
                            char** new_deps = realloc(deps, (deps_capacity + 1) * sizeof(char*)); // +1 for NULL terminator
                            if (!new_deps) {
                                fprintf(stderr, "Memory allocation failed during dependency scan\n");
                                // Cleanup already allocated strings
                                for (int k = 0; k < dep_count; k++) free(deps[k]);
                                free(deps);
                                pclose(pipe);
                                *deps_out = NULL;
                                return -1; // Indicate error
                            }
                            deps = new_deps;
                        }

                        // Add the dependency path to the array
                        deps[dep_count] = strdup(extracted_path);
                        if (!deps[dep_count]) {
                            fprintf(stderr, "Memory allocation failed for dependency path string\n");
                             // Cleanup already allocated strings
                            for (int k = 0; k < dep_count; k++) free(deps[k]);
                            free(deps);
                            pclose(pipe);
                            *deps_out = NULL;
                            return -1; // Indicate error
                        }
                         printf("  Found dependency: %s\n", deps[dep_count]);
                        dep_count++;

                    } else {
                        // fprintf(stderr, "  Skipping non-existent or non-regular file: %s\n", extracted_path);
                    }
                } else {
                    // fprintf(stderr, "  Skipping non-absolute path or self-reference: %s\n", extracted_path);
                }
            }
        }
    }

    int status = pclose(pipe);
    if (status == -1) {
         fprintf(stderr, "Error closing pipe for ldd command: %s\n", strerror(errno));
         // Continue, but maybe dependencies are incomplete
    } else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
         fprintf(stderr, "Warning: ldd command exited with status %d\n", WEXITSTATUS(status));
         // Continue, ldd might partially succeed or fail on certain binaries
    }


    // Add NULL terminator to the deps array
    if (deps) {
        deps[dep_count] = NULL;
    }

    *deps_out = deps; // Assign the resulting array to the output parameter
    return dep_count; // Return the number of dependencies found
}
// =========================================================================
// END MODIFIED FUNCTION
// =========================================================================


// Add /proc/boot libraries to store
// (Code from file content_fetcher.fetch: qnx_nix_experimental/nix_store.c)
// Requires #include <dirent.h> at the top
int add_boot_libraries(void) {
    DIR* dir = opendir("/proc/boot");
    if (!dir) {
        fprintf(stderr, "Failed to open /proc/boot: %s\n", strerror(errno));
        return -1; // Return -1 to indicate error
    }

    int count = 0;
    struct dirent* entry;
    char path[PATH_MAX];

    printf("Scanning /proc/boot for libraries...\n");
    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".." entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Process only shared objects (.so) or potentially static archives (.a) if needed
        // Typically, only .so files are runtime dependencies needed in the store this way.
        if (strstr(entry->d_name, ".so") != NULL /* || strstr(entry->d_name, ".a") != NULL */) {

            snprintf(path, PATH_MAX, "/proc/boot/%s", entry->d_name);

            // Add each library to the store
            printf("  Attempting to add: %s\n", path);
            // Using add_to_store without recursion (0)
            if (add_to_store(path, entry->d_name, 0) == 0) {
                count++;
            } else {
                 fprintf(stderr, "  Failed to add %s to store.\n", path);
                 // Optionally stop on failure or just continue? Continuing for now.
            }
        }
    }

    closedir(dir);
    printf("Added %d boot libraries to the store.\n", count);
    return count; // Return number added (could be 0)
}