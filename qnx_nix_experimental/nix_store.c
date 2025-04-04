/*
 * nix_store.c - Core implementation of Nix-like store for QNX
 */
#include "nix_store.h"
#include "sha256.h"
#include <limits.h>
#include <fcntl.h>
#include "nix_store_db.h"

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

// Check ELF file for library dependencies
char** get_elf_dependencies(const char* path, int* deps_count) {
    char cmd[PATH_MAX * 2];
    char buffer[4096];
    
    // Use 'ldd' equivalent in QNX to list dependencies
    // Adjust this command based on QNX tools availability
    snprintf(cmd, sizeof(cmd), "use %s", path);
    
    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        *deps_count = 0;
        return NULL;
    }
    
    // Count dependencies
    int count = 0;
    char** deps = NULL;
    char lib_path[PATH_MAX];
    
    // Assuming output format like "libname.so => /path/to/libname.so"
    while (fgets(buffer, sizeof(buffer), pipe)) {
        if (strstr(buffer, "=>") && sscanf(buffer, "%*s => %s", lib_path) == 1) {
            count++;
        }
    }
    
    // If no dependencies found
    if (count == 0) {
        pclose(pipe);
        *deps_count = 0;
        return NULL;
    }
    
    // Allocate memory for dependencies
    deps = (char**)malloc((count + 1) * sizeof(char*));
    if (!deps) {
        pclose(pipe);
        *deps_count = 0;
        return NULL;
    }
    
    // Reset pipe and read again to store paths
    pclose(pipe);
    pipe = popen(cmd, "r");
    
    int i = 0;
    while (fgets(buffer, sizeof(buffer), pipe) && i < count) {
        if (strstr(buffer, "=>") && sscanf(buffer, "%*s => %s", lib_path) == 1) {
            deps[i] = strdup(lib_path);
            i++;
        }
    }
    deps[i] = NULL; // NULL terminate the array
    
    pclose(pipe);
    *deps_count = i;
    return deps;
}

// Add a file or directory to the store with explicit dependencies
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
            if (!db_path_exists(deps[i])) {
                // If not found, add it as a dependency
                char dep_name[PATH_MAX];
                char* base_name = strrchr(deps[i], '/');
                if (base_name) {
                    strncpy(dep_name, base_name + 1, PATH_MAX - 1);
                } else {
                    strncpy(dep_name, deps[i], PATH_MAX - 1);
                }
                
                printf("Adding dependency: %s as %s\n", deps[i], dep_name);
                
                if (add_to_store(deps[i], dep_name, 0) != 0) {
                    fprintf(stderr, "Failed to add dependency: %s\n", deps[i]);
                    free(dep_store_paths);
                    return -1;
                }
                
                // Get the store path for this dependency
                dep_store_paths[i] = compute_store_path(dep_name, NULL, NULL);
            } else {
                // If dependency already exists, get its path
                // This is a simplified approach - in real code you'd query the DB
                char dep_name[PATH_MAX];
                char* base_name = strrchr(deps[i], '/');
                if (base_name) {
                    strncpy(dep_name, base_name + 1, PATH_MAX - 1);
                } else {
                    strncpy(dep_name, deps[i], PATH_MAX - 1);
                }
                dep_store_paths[i] = compute_store_path(dep_name, NULL, NULL);
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
    if (stat(store_path, &st) == 0) {
        // Path already exists in the store
        free(store_path);
        if (dep_store_paths) {
            for (int i = 0; i < deps_count; i++) {
                free(dep_store_paths[i]);
            }
            free(dep_store_paths);
        }
        return 0;
    }
    
    // Create the directory structure
    if (mkdir(store_path, 0755) == -1) {
        fprintf(stderr, "Failed to create store directory: %s\n", strerror(errno));
        free(store_path);
        if (dep_store_paths) {
            for (int i = 0; i < deps_count; i++) {
                free(dep_store_paths[i]);
            }
            free(dep_store_paths);
        }
        return -1;
    }
    
    // Copy the file/directory to the store (same as in add_to_store)
    if (S_ISDIR(st.st_mode)) {
        // Implement recursive directory copy using QNX functions
        char cmd[PATH_MAX * 2];
        snprintf(cmd, sizeof(cmd), "cp -r %s/* %s/", source_path, store_path);
        system(cmd);
    } else if (S_ISREG(st.st_mode)) {
        // Copy a regular file
        int src_fd = open(source_path, O_RDONLY);
        if (src_fd == -1) {
            fprintf(stderr, "Failed to open source file: %s\n", strerror(errno));
            free(store_path);
            if (dep_store_paths) {
                for (int i = 0; i < deps_count; i++) {
                    free(dep_store_paths[i]);
                }
                free(dep_store_paths);
            }
            return -1;
        }
        
        char dest_path[PATH_MAX];
        snprintf(dest_path, PATH_MAX, "%s/%s", store_path, name);
        
        int dest_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (dest_fd == -1) {
            fprintf(stderr, "Failed to create destination file: %s\n", strerror(errno));
            close(src_fd);
            free(store_path);
            if (dep_store_paths) {
                for (int i = 0; i < deps_count; i++) {
                    free(dep_store_paths[i]);
                }
                free(dep_store_paths);
            }
            return -1;
        }
        
        // Use QNX's sendfile or equivalent for efficient copying
        char buffer[8192];
        ssize_t bytes_read;
        
        while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0) {
            if (write(dest_fd, buffer, bytes_read) != bytes_read) {
                fprintf(stderr, "Write error: %s\n", strerror(errno));
                close(src_fd);
                close(dest_fd);
                free(store_path);
                if (dep_store_paths) {
                    for (int i = 0; i < deps_count; i++) {
                        free(dep_store_paths[i]);
                    }
                    free(dep_store_paths);
                }
                return -1;
            }
        }
        
        close(src_fd);
        close(dest_fd);
    }
    
    // Make the store path read-only
    make_store_path_read_only(store_path);
    
    // Register this path in the database with dependencies
    db_register_path(store_path, (const char**)dep_store_paths);
    
    printf("Added %s to store with %d dependencies\n", name, deps_count);
    
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
int add_to_store(const char* source_path, const char* name, int recursive) {
    struct stat st;
    if (stat(source_path, &st) == -1) {
        fprintf(stderr, "Source path does not exist: %s\n", strerror(errno));
        return -1;
    }
    
    // Compute a store path for this item
    char* store_path = compute_store_path(name, NULL, NULL);
    if (!store_path) {
        return -1;
    }
    
    // Check if the path already exists in the store
    if (stat(store_path, &st) == 0) {
        // Path already exists in the store
        free(store_path);
        return 0;
    }
    
    // Create the directory structure
    if (mkdir(store_path, 0755) == -1) {
        fprintf(stderr, "Failed to create store directory: %s\n", strerror(errno));
        free(store_path);
        return -1;
    }
    
    // Copy the file/directory to the store
    if (S_ISDIR(st.st_mode) && recursive) {
        // Implement recursive directory copy using QNX functions
        // For brevity, this is a simplified version
        char cmd[PATH_MAX * 2];
        snprintf(cmd, sizeof(cmd), "cp -r %s/* %s/", source_path, store_path);
        system(cmd);
    } else if (S_ISREG(st.st_mode)) {
        // Copy a regular file
        int src_fd = open(source_path, O_RDONLY);
        if (src_fd == -1) {
            fprintf(stderr, "Failed to open source file: %s\n", strerror(errno));
            free(store_path);
            return -1;
        }
        
        char dest_path[PATH_MAX];
        snprintf(dest_path, PATH_MAX, "%s/%s", store_path, name);
        
        int dest_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (dest_fd == -1) {
            fprintf(stderr, "Failed to create destination file: %s\n", strerror(errno));
            close(src_fd);
            free(store_path);
            return -1;
        }
        
        // Use QNX's sendfile or equivalent for efficient copying
        char buffer[8192];
        ssize_t bytes_read;
        
        while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0) {
            if (write(dest_fd, buffer, bytes_read) != bytes_read) {
                fprintf(stderr, "Write error: %s\n", strerror(errno));
                close(src_fd);
                close(dest_fd);
                free(store_path);
                return -1;
            }
        }
        
        close(src_fd);
        close(dest_fd);
    }
    
    // Make the store path read-only
    make_store_path_read_only(store_path);
    
    // Register this path in the database
    const char* references[] = {NULL}; // No references for now
    db_register_path(store_path, references);
    
    free(store_path);
    return 0;
}

// Make a store path read-only
int make_store_path_read_only(const char* path) {
    // Change permissions to make it read-only
    if (chmod(path, 0555) == -1) {
        fprintf(stderr, "Failed to make path read-only: %s\n", strerror(errno));
        return -1;
    }
    
    return 0;
}

// Verify a store path
int verify_store_path(const char* path) {
    // Basic verification - check if the path exists and is in the store
    struct stat st;
    if (stat(path, &st) == -1) {
        return -1;
    }
    
    // Check if the path is inside the store
    if (strncmp(path, NIX_STORE_PATH, strlen(NIX_STORE_PATH)) != 0) {
        return -1;
    }
    
    // Check if the path is registered in the database
    if (!db_path_exists(path)) {
        return -1;
    }
    
    return 0;
}

// Function to scan library dependencies for an executable
int scan_dependencies(const char* exec_path, char*** deps_out) {
    FILE* pipe;
    char cmd[PATH_MAX * 2];
    char buffer[4096];
    char dep_path[PATH_MAX];
    
    // Use 'use' QNX command to list shared library dependencies
    snprintf(cmd, sizeof(cmd), "use %s", exec_path);
    
    pipe = popen(cmd, "r");
    if (!pipe) {
        fprintf(stderr, "Failed to execute dependency scan command: %s\n", strerror(errno));
        return -1;
    }
    
    // First, count the dependencies
    int dep_count = 0;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        if (strstr(buffer, "/proc/boot/") != NULL) {
            dep_count++;
        }
    }
    pclose(pipe);
    
    if (dep_count == 0) {
        *deps_out = NULL;
        return 0;
    }
    
    // Allocate memory for dependencies
    char** deps = (char**)malloc((dep_count + 1) * sizeof(char*));
    if (!deps) {
        fprintf(stderr, "Memory allocation failed\n");
        return -1;
    }
    
    // Re-run command to collect paths
    pipe = popen(cmd, "r");
    if (!pipe) {
        fprintf(stderr, "Failed to execute dependency scan command: %s\n", strerror(errno));
        free(deps);
        return -1;
    }
    
    int i = 0;
    while (fgets(buffer, sizeof(buffer), pipe) && i < dep_count) {
        if (sscanf(buffer, "%*s %s", dep_path) == 1 && strstr(dep_path, "/proc/boot/") != NULL) {
            deps[i] = strdup(dep_path);
            if (!deps[i]) {
                fprintf(stderr, "Memory allocation failed for dependency path\n");
                for (int j = 0; j < i; j++) {
                    free(deps[j]);
                }
                free(deps);
                pclose(pipe);
                return -1;
            }
            i++;
        }
    }
    deps[i] = NULL; // NULL terminate the array
    
    pclose(pipe);
    *deps_out = deps;
    return i;
}

// Add /proc/boot libraries to store
int add_boot_libraries(void) {
    DIR* dir = opendir("/proc/boot");
    if (!dir) {
        fprintf(stderr, "Failed to open /proc/boot: %s\n", strerror(errno));
        return -1;
    }
    
    int count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".." entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Skip non-library files
        if (strstr(entry->d_name, ".so") == NULL && strstr(entry->d_name, ".a") == NULL) {
            continue;
        }
        
        char path[PATH_MAX];
        snprintf(path, PATH_MAX, "/proc/boot/%s", entry->d_name);
        
        // Add each library to the store
        if (add_to_store(path, entry->d_name, 0) == 0) {
            count++;
        }
    }
    
    closedir(dir);
    printf("Added %d boot libraries to the store\n", count);
    return count;
}