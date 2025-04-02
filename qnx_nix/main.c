/*
 * main.c - Main entry point for the Nix-like store on QNX
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "nix_store.h"

void print_usage(void) {
    printf("Nix-like store for QNX\n");
    printf("Usage:\n");
    printf("  nix-store --init                  Initialize the store\n");
    printf("  nix-store --add <path> <name>     Add a file to the store\n");
    printf("  nix-store --add-recursively <path> <name> Add a directory recursively\n");
    printf("  nix-store --verify <path>         Verify a store path\n");
    printf("  nix-store --gc                    Run garbage collection\n");
    printf("  nix-store --query-references <path> Show references of a path\n");
}

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
    else {
        printf("Unknown command: %s\n", argv[1]);
        print_usage();
        return 1;
    }
}