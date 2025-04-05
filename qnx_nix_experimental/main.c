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
    printf("  nix-store --init                         Initialize the store\n");
    printf("  nix-store --add <path> <name>            Add a file/dir (non-recursive) to the store\n");
    printf("  nix-store --add-recursively <path> <name> Add a directory recursively\n");
    printf("  nix-store --add-with-deps <path> <name>  Add file with auto-detected dependencies\n");
    printf("  nix-store --add-with-explicit-deps <path> <name> <dep1> <dep2>...  Add file with specified dependencies\n");
    printf("  nix-store --add-boot-libs                Add all libraries from /proc/boot to store\n");
    printf("  nix-store --verify <store_path>          Verify a store path\n");
    printf("  nix-store --gc                           Run garbage collection (removes paths not reachable from roots)\n");
    printf("  nix-store --query-references <store_path> Show references (dependencies) of a store path\n");
    printf("  nix-store --add-root <store_path>        Register a store path as a GC root (prevents GC)\n");
    printf("  nix-store --remove-root <store_path>     Unregister a store path as a GC root (allows GC)\n");
    printf("  nix-store --daemon                       Start the resource manager service\n"); // Assuming this exists
}

extern int init_resource_manager(void);  // Declaration from nix_store_mgr.c (if used)

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
        if (argc < 4) { printf("Error: Missing arguments for --add\n"); return 1; }
        if (add_to_store(argv[2], argv[3], 0) == 0) return 0; // Success message in function
        return 1;
    }
    else if (strcmp(argv[1], "--add-recursively") == 0) {
         if (argc < 4) { printf("Error: Missing arguments for --add-recursively\n"); return 1; }
         if (add_to_store(argv[2], argv[3], 1) == 0) return 0; // Success message in function
         return 1;
    }
     else if (strcmp(argv[1], "--add-with-deps") == 0) {
        if (argc < 4) { printf("Error: Missing arguments for --add-with-deps\n"); return 1; }

        char** deps = NULL;
        int deps_count = scan_dependencies(argv[2], &deps);

        if (deps_count < 0) { printf("Error scanning dependencies.\n"); return 1; }
        printf("Found %d dependencies for %s\n", deps_count, argv[3]); // Use name argv[3]

        // Add to store with detected dependencies
        int result = add_to_store_with_deps(argv[2], argv[3], (const char**)deps, deps_count);

        // Free memory from scan_dependencies
        if (deps) {
            for (int i = 0; i < deps_count; i++) { if (deps[i]) free(deps[i]); }
            free(deps);
        }

        // Success/failure message printed within add_to_store_with_deps
        return (result == 0) ? 0 : 1;
    }
    else if (strcmp(argv[1], "--add-with-explicit-deps") == 0) {
        if (argc < 4) { printf("Error: Missing arguments for --add-with-explicit-deps\n"); return 1; }

        int deps_count = argc - 4;
        const char** deps = NULL;
        if (deps_count > 0) { deps = (const char**)&argv[4]; }

        if (add_to_store_with_deps(argv[2], argv[3], deps, deps_count) == 0) return 0; // Success msg in function
        return 1;
    }
     else if (strcmp(argv[1], "--add-boot-libs") == 0) {
        int count = add_boot_libraries();
        // Message printed within function
        return (count >= 0) ? 0 : 1;
    }
    else if (strcmp(argv[1], "--verify") == 0) {
         if (argc < 3) { printf("Error: Missing path for --verify\n"); return 1; }
         int result = verify_store_path(argv[2]);
         // Message printed within function
         return (result == 0) ? 0 : 1;
    }
    else if (strcmp(argv[1], "--gc") == 0) {
        // Message printed within function
        return (gc_collect_garbage() == 0) ? 0 : 1;
    }
    else if (strcmp(argv[1], "--query-references") == 0) {
        if (argc < 3) { printf("Error: Missing path for --query-references\n"); return 1; }

        char** refs = db_get_references(argv[2]);
        if (refs) {
            printf("References for %s:\n", argv[2]);
            int i = 0;
            if (refs[0] == NULL) {
                 printf("  (No references)\n");
            } else {
                for (i = 0; refs[i] != NULL; i++) {
                    printf("  %s\n", refs[i]);
                    free(refs[i]); // Free each string
                }
            }
            free(refs); // Free the array itself
            return 0;
        }
        // db_get_references returning NULL likely means path not found or error
        printf("Path %s not found in database or error retrieving references.\n", argv[2]);
        return 1;
    }
    // ---- NEW COMMANDS ----
    else if (strcmp(argv[1], "--add-root") == 0) {
        if (argc < 3) { printf("Error: Missing store path for --add-root\n"); return 1; }
        // Message printed within function
        return (db_add_root(argv[2]) == 0) ? 0 : 1;
    }
    else if (strcmp(argv[1], "--remove-root") == 0) {
         if (argc < 3) { printf("Error: Missing store path for --remove-root\n"); return 1; }
         // Message printed within function
         return (db_remove_root(argv[2]) == 0) ? 0 : 1;
    }
    // ---- END NEW COMMANDS ----
    else if (strcmp(argv[1], "--daemon") == 0) {
        printf("Starting Nix store resource manager...\n");
        // Check if init_resource_manager is implemented/needed
        // return init_resource_manager();
        printf("Daemon functionality not fully implemented in example.\n");
        return 1;
    }
    else {
        printf("Unknown command: %s\n", argv[1]);
        print_usage();
        return 1;
    }
}