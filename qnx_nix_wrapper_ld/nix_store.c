/*
 * nix_store.c - Core implementation of Nix-like store for QNX
 */

 #include "nix_store.h" // already includes stdio, stdlib, string, sys/stat, sys/types, unistd, errno
 #include "sha256.h"
 #include <limits.h>
 #include <fcntl.h>
 #include <dirent.h>
 #include "nix_store_db.h"
 #include <ctype.h>
 #include <sys/stat.h> // For mkdir, chmod
 #include <unistd.h>   // For symlink, execvp, chdir, unlink
 #include <libgen.h>   // For basename
 #include <sys/param.h> // For MAXPATHLEN if PATH_MAX is not defined
 
 #ifndef PATH_MAX
 #define PATH_MAX MAXPATHLEN
 #endif
 
 
 // Define NIX_STORE_PATH if not defined in nix_store.h
 #ifndef NIX_STORE_PATH
 #define NIX_STORE_PATH "/data/nix/store"
 #endif
 
 // Initialize the store directory structure
 int store_init(void) {
     // Create the path hierarchy
     const char* path_parts[] = {"/data", "/data/nix", "/data/nix/store", "/data/nix/profiles"};
 
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
     snprintf(db_path, PATH_MAX, "%s/.nix-db", NIX_STORE_PATH);
 
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
 
         // Register dependencies even if path exists (idempotency)
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

         char cmd[PATH_MAX * 3];
         copy_len = snprintf(cmd, sizeof(cmd), "cp -P %s %s", source_path, dest_path);
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
             fprintf(stderr, "Failed to copy file/link %s to %s (system returned %d)\n", source_path, store_path, ret);
             char rm_cmd[PATH_MAX + 10];
             snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf %s", store_path); // Cleanup created dir
             system(rm_cmd);
             free(store_path);
             if (dep_store_paths) {
                  for (int i = 0; i < deps_count; i++) if(dep_store_paths[i]) free(dep_store_paths[i]);
                  free(dep_store_paths);
             }
             return -1;
         }
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
 
 
     // Make the store path read-only
     make_store_path_read_only(store_path);
 
     // Register this path in the database with dependencies
     db_register_path(store_path, (const char**)dep_store_paths);
 
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
     // Basic verification - check if the path exists and is in the store
     struct stat st;
     if (stat(path, &st) == -1) {
          fprintf(stderr, "Verify failed: Path %s does not exist or is inaccessible (%s).\n", path, strerror(errno));
         return -1;
     }
 
     // Check if the path is inside the store and safe
     if (strncmp(path, NIX_STORE_PATH, strlen(NIX_STORE_PATH)) != 0 || strstr(path, "..") != NULL) {
          fprintf(stderr, "Verify failed: Path %s is not within the store directory %s or contains '..'.\n", path, NIX_STORE_PATH);
         return -1;
     }
 
     // Check if the path is registered in the database
     if (!db_path_exists(path)) {
          fprintf(stderr, "Verify failed: Path %s is not registered in the database.\n", path);
         return -1;
     }
 
     // TODO: Add hash verification here if hashes are stored and computed during add
 
     printf("Path %s verified successfully.\n", path);
     return 0;
 }
 
 
 // =========================================================================
 // MODIFIED FUNCTION: scan_dependencies (Using ldd)
 // =========================================================================
 
// Helper function to find store path for a boot library (improved version)
static char* find_store_path_for_boot_lib(const char* boot_path) {
    const char* lib_name = strrchr(boot_path, '/');
    if (!lib_name) return NULL;
    lib_name++; // Skip the '/'

    printf("  Looking for library: %s\n", lib_name);

    DIR* dir = opendir(NIX_STORE_PATH);
    if (!dir) return NULL;

    char* result = NULL;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        // Check if this entry has our library name in it
        if (strstr(entry->d_name, lib_name) != NULL) {
            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", NIX_STORE_PATH, entry->d_name);

            // Verify it exists and is a regular file
            struct stat st;
            if (stat(full_path, &st) == 0) {
                printf("  Found library store path: %s\n", full_path);
                result = strdup(full_path);
                break;
            }
        }
    }

    closedir(dir);
    if (!result) {
        printf("  Failed to find library in store: %s\n", lib_name);
    }
    return result;
}

int scan_dependencies(const char* exec_path, char*** deps_out) {
    FILE* pipe;
    char cmd[PATH_MAX + 4]; // +4 for "ldd " + null terminator
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
 
             while (*lib_path_start != '\0' && isspace((unsigned char)*lib_path_start)) {
                 lib_path_start++;
             }
 
             char* lib_path_end = lib_path_start;
             while (*lib_path_end != '\0' &&
                    !isspace((unsigned char)*lib_path_end) &&
                    *lib_path_end != '(') {
                 lib_path_end++;
             }
 
             size_t path_len = lib_path_end - lib_path_start;
 
             if (path_len > 0 && path_len < PATH_MAX) {
                 strncpy(extracted_path, lib_path_start, path_len);
                 extracted_path[path_len] = '\0';
 
                 // Ensure it's an absolute path and exists
                 if (extracted_path[0] == '/') {
                     struct stat st;
                     if (stat(extracted_path, &st) == 0 && S_ISREG(st.st_mode)) {
                         char* store_path = NULL;
                         
                         if (strncmp(extracted_path, NIX_STORE_PATH, strlen(NIX_STORE_PATH)) == 0) {
                             // Direct store path
                             store_path = strdup(extracted_path);
                         } else if (strncmp(extracted_path, "/proc/boot/", 11) == 0) {
                             // Boot library - find its store path
                             store_path = find_store_path_for_boot_lib(extracted_path);
                         }

                         // Add debug output
                         if (store_path) {
                             printf("  Found dependency mapping:\n");
                             printf("    From: %s\n", extracted_path);
                             printf("    To:   %s\n", store_path);
                         }

                         if (store_path) {
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
                             printf("  Skipping non-store dependency: %s\n", extracted_path);
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
         else { // Handle malloc failure
              fprintf(stderr, "Memory allocation failed for empty dependency array\n");
              *deps_out = NULL;
              return -1; // Indicate error
         }
     }
 
     *deps_out = deps;
     return dep_count;
 }
 // =========================================================================
 
 
 // Add /proc/boot libraries to store
 int add_boot_libraries(void) {
     DIR* dir = opendir("/proc/boot");
     if (!dir) {
         fprintf(stderr, "Failed to open /proc/boot: %s\n", strerror(errno));
         return -1;
     }
 
     int count = 0;
     struct dirent* entry;
     char path[PATH_MAX];
 
     printf("Scanning /proc/boot for libraries...\n");
     while ((entry = readdir(dir)) != NULL) {
         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
             continue;
         }
 
         if (strstr(entry->d_name, ".so") != NULL) {
              int path_len = snprintf(path, PATH_MAX, "/proc/boot/%s", entry->d_name);
              if (path_len < 0 || path_len >= PATH_MAX) {
                  fprintf(stderr, "  Skipping boot library, path too long: %s\n", entry->d_name);
                  continue;
              }
 
             printf("  Attempting to add: %s\n", path);
             // Use add_to_store (which now calls add_to_store_with_deps internally)
             if (add_to_store(path, entry->d_name, 0) == 0) {
                 count++;
             } else {
                  fprintf(stderr, "  Failed to add %s to store.\n", path);
             }
         }
     }
 
     closedir(dir);
     printf("Added %d boot libraries to the store.\n", count);
     return count;
 }
 
 // =========================================================================
 // ---- NEW PROFILE FUNCTIONS ----
 // =========================================================================
 
 // Helper to create directory path recursively
 static int mkpath(const char *path, mode_t mode) {
     char *pp;
     char *sp;
     int status;
     char *copypath = strdup(path);
     if (!copypath) return -1;
 
     status = 0;
     pp = copypath;
     while (status == 0 && (sp = strchr(pp, '/')) != 0) {
         if (sp != pp) {
             /* Neither root nor double slash in path */
             *sp = '\0';
             struct stat st;
             if (stat(copypath, &st) != 0) {
                 status = mkdir(copypath, mode);
                 if (status != 0 && errno != EEXIST) {
                     fprintf(stderr, "Warning: Failed to create directory %s: %s\n", copypath, strerror(errno));
                 } else {
                      status = 0; // Reset status if mkdir succeeded or EEXIST
                 }
             } else if (!S_ISDIR(st.st_mode)) {
                 fprintf(stderr, "Error: Path component %s is not a directory\n", copypath);
                 status = -1;
                 errno = ENOTDIR;
             }
             *sp = '/';
         }
         pp = sp + 1;
     }
 
     // Handle the last component
     struct stat st_last;
     if (status == 0) {
          if (stat(path, &st_last) != 0) {
             status = mkdir(path, mode);
             if (status != 0 && errno != EEXIST) {
                  fprintf(stderr, "Warning: Failed to create directory %s: %s\n", path, strerror(errno));
             } else {
                  status = 0; // Reset status if mkdir succeeded or EEXIST
             }
          } else if (!S_ISDIR(st_last.st_mode)) {
              fprintf(stderr, "Error: Path %s is not a directory\n", path);
              status = -1;
              errno = ENOTDIR;
          }
     }
 
     free(copypath);
     return (status);
 }
 
 // Helper function to create a wrapper script
 static int create_wrapper_script(const char* script_path, const char* target_executable, const char* store_path) {
     FILE* f = fopen(script_path, "w");
     if (!f) {
         fprintf(stderr,"Failed to open wrapper script %s for writing: %s\n", script_path, strerror(errno));
         return -1;
     }
 
     fprintf(f, "#!/bin/sh\n");
     fprintf(f, "# Wrapper for %s\n\n", target_executable);
 
     // --- Set LD_LIBRARY_PATH ---
     fprintf(f, "export LD_LIBRARY_PATH=\""); // Start with empty path
 
     // Add dependency paths directly
     char** refs = db_get_references(store_path);
     if (refs) {
         int path_added = 0;
         for (int i = 0; refs[i] != NULL; i++) {
             if (path_added) fprintf(f, ":");
             fprintf(f, "%s", refs[i]); // Add each dependency path
             path_added = 1;
             free(refs[i]);
         }
         free(refs);
     }
     fprintf(f, "\"\n\n"); // Close quotes for LD_LIBRARY_PATH
 
     // --- Execute the target ---
     fprintf(f, "exec \"%s\" \"$@\"\n", target_executable);
 
     if (fclose(f) != 0) {
         fprintf(stderr,"Failed to close wrapper script %s: %s\n", script_path, strerror(errno));
          remove(script_path);
          return -1;
     };
 
     // Make the script executable
     if (chmod(script_path, 0755) == -1) {
         fprintf(stderr,"Failed to make wrapper script %s executable: %s\n", script_path, strerror(errno));
         remove(script_path);
         return -1;
     }
 
     printf("    Created wrapper: %s -> %s\n", script_path, target_executable);
     return 0;
 }
 
 
// Install a store path into a profile
int install_to_profile(const char* store_path, const char* profile_name) {
    printf("Installing %s into profile '%s'\n", store_path, profile_name);
    int ret_val;
    time_t current_time = time(NULL);

    // 1. First backup existing profile if it exists
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

    struct stat st_profile;
    if (stat(profile_path, &st_profile) == 0) {
        // Existing profile found, create a backup generation
        char cp_cmd[PATH_MAX * 3];
        snprintf(cp_cmd, sizeof(cp_cmd), "cp -rP %s/. %s/", profile_path, backup_path);
        if (system(cp_cmd) != 0) {
            fprintf(stderr, "Failed to create generation backup: %s\n", strerror(errno));
            return -1;
        }
        printf("Created new generation backup: %s\n", backup_path);

        // Remove contents of existing profile but keep directory
        char rm_cmd[PATH_MAX * 3];
        snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf %s/*", profile_path);
        if (system(rm_cmd) != 0) {
            fprintf(stderr, "Failed to clean existing profile: %s\n", strerror(errno));
            return -1;
        }
    }

    // 2. Create new profile directory
    if (mkdir(profile_path, 0755) == -1 && errno != EEXIST) {
        fprintf(stderr, "Failed to create profile directory %s: %s\n", profile_path, strerror(errno));
        // Try to restore backup if it exists
        if (stat(backup_path, &st_profile) == 0) {
            rename(backup_path, profile_path);
        }
        return -1;
    }

    // Create main profile directory and its subdirectories
    const char* subdirs[] = {"bin", "lib", "share", "include", "etc", NULL};
     
    // Create main profile directory first
    if (mkdir(profile_path, 0755) == -1 && errno != EEXIST) {
        fprintf(stderr, "Failed to create profile directory %s: %s\n", profile_path, strerror(errno));
        return -1;
    }
     
    // Create subdirectories
    for (int i = 0; subdirs[i] != NULL; i++) {
        char subdir_path[PATH_MAX];
        ret_val = snprintf(subdir_path, PATH_MAX, "%s/%s", profile_path, subdirs[i]);
        if (ret_val < 0 || ret_val >= PATH_MAX) continue;
         
        if (mkdir(subdir_path, 0755) == -1 && errno != EEXIST) {
            fprintf(stderr, "Warning: Failed to create %s directory: %s\n", subdir_path, strerror(errno));
            // Continue anyway
        }
    }
 
    // 3. Iterate through common subdirectories (bin, lib, share, etc.)
    for (int i = 0; subdirs[i] != NULL; i++) {
        char source_subdir_path[PATH_MAX];
        char profile_subdir_path[PATH_MAX];
 
        // Construct source subdir path and check length
        ret_val = snprintf(source_subdir_path, PATH_MAX, "%s/%s", store_path, subdirs[i]);
        if (ret_val < 0 || ret_val >= PATH_MAX) {
            fprintf(stderr, "Warning: Source subdirectory path exceeds PATH_MAX, skipping: %s/%s\n", store_path, subdirs[i]);
            continue;
        }
 
        // Construct profile subdir path and check length
        ret_val = snprintf(profile_subdir_path, PATH_MAX, "%s/%s", profile_path, subdirs[i]);
          if (ret_val < 0 || ret_val >= PATH_MAX) { // <<<<<<<<<< CHECK ADDED HERE (line ~629 in previous)
            fprintf(stderr, "Warning: Profile subdirectory path exceeds PATH_MAX, skipping: %s/%s\n", profile_path, subdirs[i]);
            continue;
        }
 
 
        struct stat st_subdir;
        if (stat(source_subdir_path, &st_subdir) == 0 && S_ISDIR(st_subdir.st_mode)) {
            printf("  Processing %s directory...\n", subdirs[i]);
 
            // Ensure profile subdirectory exists
            if (mkpath(profile_subdir_path, 0755) != 0 && errno != EEXIST) {
                  fprintf(stderr, "Error creating profile subdirectory %s: %s. Skipping.\n",
                          profile_subdir_path, strerror(errno));
                  continue;
             }
 
             // Scan the source subdirectory
             DIR* dir = opendir(source_subdir_path);
             if (!dir) {
                 fprintf(stderr, "Error opening source subdirectory %s: %s. Skipping.\n",
                         source_subdir_path, strerror(errno));
                 continue;
             }
 
             struct dirent* entry;
             while ((entry = readdir(dir)) != NULL) {
                 if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                     continue;
                 }
 
                 char source_item_path[PATH_MAX];
                 char profile_item_path[PATH_MAX];
 
                 // Construct source item path and check length
                 ret_val = snprintf(source_item_path, PATH_MAX, "%s/%s", source_subdir_path, entry->d_name);
                 if (ret_val < 0 || ret_val >= PATH_MAX) { // <<<<<<<<<< CHECK ADDED HERE (line ~658 in previous)
                      fprintf(stderr, "Warning: Source item path exceeds PATH_MAX, skipping: %s/%s\n", source_subdir_path, entry->d_name);
                      continue;
                 }
 
                 // Construct profile item path and check length
                 ret_val = snprintf(profile_item_path, PATH_MAX, "%s/%s", profile_subdir_path, entry->d_name);
                  if (ret_val < 0 || ret_val >= PATH_MAX) { // <<<<<<<<<< CHECK ADDED HERE (line ~659 in previous)
                      fprintf(stderr, "Warning: Profile item path exceeds PATH_MAX, skipping: %s/%s\n", profile_subdir_path, entry->d_name);
                      continue;
                 }
 
 
                 // Remove existing symlink/file in profile if it exists
                 if (unlink(profile_item_path) == -1 && errno != ENOENT) {
                      fprintf(stderr, "Warning: could not remove existing profile item %s: %s\n", profile_item_path, strerror(errno));
                 }
 
                 if (strcmp(subdirs[i], "bin") == 0) {
                     // Instead of creating wrapper script, modify RPATH
                     struct stat item_st;
                     if(stat(source_item_path, &item_st) == 0 && S_ISREG(item_st.st_mode)) {
                         // Create wrapper script instead of attempting RPATH modification
                         if (create_wrapper_script(profile_item_path, source_item_path, store_path) != 0) {
                             fprintf(stderr, "Failed to create wrapper for %s\n", source_item_path);
                             return -1;
                         }
                     }
                 } else {
                     // Create direct symlink for items in lib/, share/, etc.
                     if (symlink(source_item_path, profile_item_path) == -1) {
                          fprintf(stderr, "    Failed to create symlink for %s -> %s: %s. Skipping.\n", profile_item_path, source_item_path, strerror(errno));
                     } else {
                          printf("    Created symlink: %s -> %s\n", profile_item_path, source_item_path);
                     }
                 }
             }
             closedir(dir);
         }
     }
 
     printf("Installation to profile '%s' complete.\n", profile_name);
     return 0;
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

    // Create standard profile subdirectories in the store
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
            // Continue anyway
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
    unlink(profile_path); // Ignore errors, might not exist

    // Create the symlink
    if (symlink(store_path, profile_path) == -1) {
        fprintf(stderr, "Failed to create profile symlink: %s\n", strerror(errno));
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

    // Verify target profile exists
    struct stat st;
    if (stat(profile_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Profile '%s' does not exist or is not a directory\n", profile_name);
        return -1;
    }

    // Remove existing current link if it exists
    if (unlink(current_link) == -1 && errno != ENOENT) {
        fprintf(stderr, "Failed to remove existing current profile link: %s\n", strerror(errno));
        return -1;
    }

    // Create new symlink
    if (symlink(profile_path, current_link) == -1) {
        fprintf(stderr, "Failed to create new current profile link: %s\n", strerror(errno));
        return -1;
    }

    printf("Switched to profile: %s\n", profile_name);
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

    // Check if profile exists
    struct stat st;
    if (stat(profile_path, &st) != 0) {
        fprintf(stderr, "Profile '%s' does not exist\n", profile_name);
        return -1;
    }

    // Find previous generation by looking for profile-<timestamp> with highest timestamp
    DIR* dir = opendir("/data/nix/profiles");
    struct dirent* entry;
    time_t latest_time = 0;

    if (!dir) {
        fprintf(stderr, "Failed to open profiles directory\n");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, profile_name, strlen(profile_name)) == 0 && 
            entry->d_name[strlen(profile_name)] == '-') {
            // Extract timestamp
            time_t gen_time = atol(entry->d_name + strlen(profile_name) + 1);
            if (gen_time < current_time && gen_time > latest_time) {
                latest_time = gen_time;
                snprintf(latest_path, PATH_MAX, "/data/nix/profiles/%s", entry->d_name);
            }
        }
    }
    closedir(dir);

    if (latest_time == 0) {
        fprintf(stderr, "No previous generation found\n");
        return -1;
    }

    // Remove current broken profile
    char rm_cmd[PATH_MAX + 10];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf %s", profile_path);
    system(rm_cmd);

    // Copy previous generation back to main profile
    char cp_cmd[PATH_MAX * 3];
    snprintf(cp_cmd, sizeof(cp_cmd), "cp -rP %s %s", latest_path, profile_path);
    system(cp_cmd);

    printf("Rolled back profile '%s' to generation from %s", 
           profile_name, ctime(&latest_time));
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
    return 0;
}

int switch_profile_generation(const char* profile_name, time_t timestamp) {
    char profile_path[PATH_MAX];
    char gen_path[PATH_MAX];
    
    snprintf(profile_path, PATH_MAX, "/data/nix/profiles/%s", profile_name);
    snprintf(gen_path, PATH_MAX, "/data/nix/profiles/%s-%ld", profile_name, timestamp);

    // Verify generation exists
    struct stat st;
    if (stat(gen_path, &st) != 0) {
        fprintf(stderr, "Generation %ld does not exist\n", timestamp);
        return -1;
    }

    // Remove current profile link
    unlink(profile_path);

    // Create new link
    if (symlink(gen_path, profile_path) != 0) {
        fprintf(stderr, "Failed to switch to generation %ld\n", timestamp);
        return -1;
    }

    printf("Switched profile '%s' to generation from %s", 
           profile_name, ctime(&timestamp));
    return 0;
}