/*
 * nix_gc.c - Garbage collector for Nix-like store
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "nix_store.h"
#include "nix_store_db.h"

// Structure to track path references
typedef struct PathRef {
    char path[PATH_MAX];
    int mark;  // For mark-and-sweep
    struct PathRef* next;
} PathRef;

// Add a path to the reference list
static PathRef* add_path_ref(PathRef* list, const char* path) {
    PathRef* ref = malloc(sizeof(PathRef));
    if (!ref) return list;
    
    strncpy(ref->path, path, PATH_MAX - 1);
    ref->mark = 0;
    ref->next = list;
    
    return ref;
}

// Mark a path and all its references
static void mark_path(PathRef* list, const char* path) {
    // Find the path in our list
    PathRef* current = list;
    while (current) {
        if (strcmp(current->path, path) == 0) {
            if (current->mark) return;  // Already marked
            
            current->mark = 1;
            
            // Mark all references
            char** refs = db_get_references(path);
            if (refs) {
                for (int i = 0; refs[i] != NULL; i++) {
                    mark_path(list, refs[i]);
                    free(refs[i]);
                }
                free(refs);
            }
            
            return;
        }
        current = current->next;
    }
}

// Collect garbage in the Nix store
int gc_collect_garbage(void) {
    // Build a list of all paths in the store
    PathRef* paths = NULL;
    
    DIR* dir = opendir(NIX_STORE_PATH);
    if (!dir) {
        fprintf(stderr, "Failed to open store directory: %s\n", strerror(errno));
        return -1;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".." entries
        if (strcmp(entry->d_name, ".") == 0 || 
            strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, ".nix-db") == 0) {
            continue;
        }
        
        char full_path[PATH_MAX];
        snprintf(full_path, PATH_MAX, "%s/%s", NIX_STORE_PATH, entry->d_name);
        
        paths = add_path_ref(paths, full_path);
    }
    
    closedir(dir);
    
    // Mark all roots and their dependencies
    FILE* roots_file = fopen(NIX_STORE_PATH "/.nix-db/roots", "r");
    if (roots_file) {
        char root[PATH_MAX];
        while (fgets(root, PATH_MAX, roots_file)) {
            // Remove newline if present
            size_t len = strlen(root);
            if (len > 0 && root[len-1] == '\n') {
                root[len-1] = '\0';
            }
            
            mark_path(paths, root);
        }
        fclose(roots_file);
    }
    
    // Delete unmarked paths
    PathRef* current = paths;
    while (current) {
        if (!current->mark) {
            printf("Removing unused path: %s\n", current->path);
            
            // Recursive removal of the directory
            char cmd[PATH_MAX + 10];
            snprintf(cmd, sizeof(cmd), "rm -rf %s", current->path);
            system(cmd);
            
            // Remove from database
            db_remove_path(current->path);
        }
        
        PathRef* next = current->next;
        free(current);
        current = next;
    }
    
    return 0;
}