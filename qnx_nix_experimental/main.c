/*
 * main.c - Main entry point for the Nix-like store on QNX
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "nix_store.h"
#include "nix_store_db.h"

void print_usage(void) {
    printf("Nix-like store for QNX\n");
    printf("Usage:\n");
    printf("  nix-store --init                  Initialize the store\n");
    printf("  nix-store --add <path> <name>     Add a file to the store\n");
    printf("  nix-store --add-recursively <path> <name> Add a directory recursively\n");
    printf("  nix-store --add-with-deps <path> <name>  Add with auto-detected dependencies\n");
    printf("  nix-store --add-with-explicit-deps <path> <name> <dep1> <dep2>...  Add with specified dependencies\n");
    printf("  nix-store --add-boot-libs         Add all libraries from /proc/boot to store\n");
    printf("  nix-store --verify <path>         Verify a store path\n");
    printf("  nix-store --gc                    Run garbage collection\n");
    printf("  nix-store --query-references <path> Show references of a path\n");
    printf("  nix-store --daemon                Start the resource manager service\n");
}

extern int init_resource_manager(void);  // Declaration from nix_store_mgr.c

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    
    if (strcmp(argv[1], "--init") == 0) {
        if (store_init() == 0) {
            printf("Store initialized successfully.\n");
            return 0;
        }
        return 1;
    }
    else if (strcmp(argv[1], "--add") == 0) {
        if (argc < 4) {
            printf("Error: Missing arguments for --add\n");
            return 1;
        }
        
        if (add_to_store(argv[2], argv[3], 0) == 0) {
            printf("Added %s to the store.\n", argv[2]);
            return 0;
        }
        return 1;
    }
    else if (strcmp(argv[1], "--add-recursively") == 0) {
        if (argc < 4) {
            printf("Error: Missing arguments for --add-recursively\n");
            return 1;
        }
        
        if (add_to_store(argv[2], argv[3], 1) == 0) {
            printf("Added %s to the store recursively.\n", argv[2]);
            return 0;
        }
        return 1;
    }
    else if (strcmp(argv[1], "--add-with-deps") == 0) {
        if (argc < 4) {
            printf("Error: Missing arguments for --add-with-deps\n");
            return 1;
        }
        
        // Auto-detect dependencies
        char** deps = NULL;
        int deps_count = scan_dependencies(argv[2], &deps);
        
        if (deps_count < 0) {
            printf("Error scanning dependencies.\n");
            return 1;
        }
        
        printf("Found %d dependencies for %s\n", deps_count, argv[2]);
        
        // Add to store with detected dependencies
        int result = add_to_store_with_deps(argv[2], argv[3], (const char**)deps, deps_count);
        
        // Free memory
        if (deps) {
            for (int i = 0; i < deps_count; i++) {
                free(deps[i]);
            }
            free(deps);
        }
        
        if (result == 0) {
            printf("Added %s to the store with %d auto-detected dependencies.\n", argv[2], deps_count);
            return 0;
        }
        return 1;
    }
    else if (strcmp(argv[1], "--add-with-explicit-deps") == 0) {
        if (argc < 4) {
            printf("Error: Missing arguments for --add-with-explicit-deps\n");
            return 1;
        }
        
        // Count explicit dependencies passed as arguments
        int deps_count = argc - 4;
        const char** deps = NULL;
        
        if (deps_count > 0) {
            deps = (const char**)&argv[4];
        }
        
        if (add_to_store_with_deps(argv[2], argv[3], deps, deps_count) == 0) {
            printf("Added %s to the store with %d explicit dependencies.\n", argv[2], deps_count);
            return 0;
        }
        return 1;
    }
    else if (strcmp(argv[1], "--add-boot-libs") == 0) {
        int count = add_boot_libraries();
        if (count >= 0) {
            printf("Added %d libraries from /proc/boot to the store.\n", count);
            return 0;
        }
        return 1;
    }
    else if (strcmp(argv[1], "--verify") == 0) {
        if (argc < 3) {
            printf("Error: Missing path for --verify\n");
            return 1;
        }
        
        if (verify_store_path(argv[2]) == 0) {
            printf("Store path is valid.\n");
            return 0;
        }
        printf("Store path is invalid.\n");
        return 1;
    }
    else if (strcmp(argv[1], "--gc") == 0) {
        if (gc_collect_garbage() == 0) {
            printf("Garbage collection completed.\n");
            return 0;
        }
        return 1;
    }
    else if (strcmp(argv[1], "--query-references") == 0) {
        if (argc < 3) {
            printf("Error: Missing path for --query-references\n");
            return 1;
        }
        
        char** refs = db_get_references(argv[2]);
        if (refs) {
            printf("References for %s:\n", argv[2]);
            for (int i = 0; refs[i] != NULL; i++) {
                printf("  %s\n", refs[i]);
                free(refs[i]);
            }
            free(refs);
            return 0;
        }
        
        printf("No references found or path does not exist.\n");
        return 1;
    }
    else if (strcmp(argv[1], "--daemon") == 0) {
        printf("Starting Nix store resource manager...\n");
        return init_resource_manager();
    }
    else {
        printf("Unknown command: %s\n", argv[1]);
        print_usage();
        return 1;
    }
}