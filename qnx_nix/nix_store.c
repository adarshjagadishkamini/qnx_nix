/*
 * nix_store.c - Core implementation of Nix-like store for QNX
 */
#include "nix_store.h"
#include "sha256.h"
#include <limits.h>
#include <fcntl.h>

// Initialize the store directory structure
int store_init(void) {
    // Create the main store directory if it doesn't exist
    struct stat st = {0};
    
    if (stat(NIX_STORE_PATH, &st) == -1) {
        // Use QNX-specific permission bits if needed
        if (mkdir(NIX_STORE_PATH, 0755) == -1) {
            fprintf(stderr, "Failed to create store directory: %s\n", strerror(errno));
            return -1;
        }
        
        // Set special QNX attributes for the store directory
        if (chmod(NIX_STORE_PATH, 0755) == -1) {
            fprintf(stderr, "Failed to set permissions: %s\n", strerror(errno));
            return -1;
        }
    }
    
    // Create database directory for the store
    char db_path[PATH_MAX];
    snprintf(db_path, PATH_MAX, "%s/.nix-db", NIX_STORE_PATH);
    
    if (stat(db_path, &st) == -1) {
        if (mkdir(db_path, 0755) == -1) {
            fprintf(stderr, "Failed to create database directory: %s\n", strerror(errno));
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