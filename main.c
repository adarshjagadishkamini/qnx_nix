#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "nix_store.h"
#include "nix_store_db.h"

// show help text
void print_usage(void) {
    printf("Nix-like store for QNX\n");
    printf("Usage:\n");
    printf("  nix-store --init                         Initialize the store and profile dirs\n");
    printf("  nix-store --add <path> <name>            Add a file/dir (non-recursive) to the store\n");
    printf("  nix-store --add-recursively <path> <name> Add a directory recursively\n");
    printf("  nix-store --add-with-deps <path> <name>  Add file/dir with auto-detected store dependencies\n");
    printf("  nix-store --add-with-explicit-deps <path> <name> <dep1> <dep2>...  Add file/dir with specified store dependencies\n");
    printf("  nix-store --add-boot-libs                Add all libraries from /proc/boot to store\n");
    printf("  nix-store --install <store_path> [<profile>] Install package from store into profile (default: 'default')\n");
    printf("                                              Creates wrappers and symlinks for the package\n");
    printf("  nix-store --verify <store_path>          Verify a store path\n");
    printf("  nix-store --gc                           Run garbage collection (removes paths not reachable from roots/profiles)\n");
    printf("  nix-store --query-references <store_path> Show references (dependencies) of a store path\n");
    printf("  nix-store --add-root <store_path>        Register a store path as a GC root (prevents GC)\n");
    printf("  nix-store --remove-root <store_path>     Unregister a store path as a GC root (allows GC)\n");
    printf("  nix-store --create-profile <name>       Create a new profile\n");
    printf("  nix-store --switch-profile <name>       Switch the current profile\n");
    printf("  nix-store --list-profiles               List available profiles\n");
    printf("  nix-store --rollback <profile>          Rollback to previous generation\n");
    printf("  nix-store --list-generations <profile>  List available generations\n");
    printf("  nix-store --switch-generation <profile> <timestamp> Switch to specific generation\n");
}

int main(int argc, char* argv[]) {
    // verify args
    if (argc < 2) {
        print_usage();
        return 1;
    }

    // handle commands
    if (strcmp(argv[1], "--init") == 0) {
        // init store dirs
        if (store_init() == 0) {
            printf("Store and profile directories initialized successfully under /data/nix/.\n");
            return 0;
        }
        fprintf(stderr,"Store initialization failed.\n");
        return 1;
    }
    else if (strcmp(argv[1], "--add") == 0) {
        // add single item
        if (argc < 4) { fprintf(stderr,"Error: Missing arguments for --add. Usage: --add <source_path> <base_name>\n"); return 1; }
        if (add_to_store(argv[2], argv[3], 0) == 0) return 0;
        fprintf(stderr,"Failed to add '%s' to store.\n", argv[3]);
        return 1;
    }
    else if (strcmp(argv[1], "--add-recursively") == 0) {
        // add dir recursively
        if (argc < 4) { fprintf(stderr,"Error: Missing arguments for --add-recursively. Usage: --add-recursively <source_dir> <base_name>\n"); return 1; }
        if (add_to_store(argv[2], argv[3], 1) == 0) return 0;
        fprintf(stderr,"Failed to add '%s' recursively to store.\n", argv[3]);
        return 1;
    }
    else if (strcmp(argv[1], "--add-with-deps") == 0) {
        // add with scanned deps
        if (argc < 4) { fprintf(stderr,"Error: Missing arguments for --add-with-deps. Usage: --add-with-deps <source_path> <base_name>\n"); return 1; }

        char** deps = NULL;
        int deps_count = scan_dependencies(argv[2], &deps);

        if (deps_count < 0) {
            fprintf(stderr,"Error scanning dependencies for %s.\n", argv[2]);
            if (deps) { for (int i = 0; deps[i] != NULL; i++) free(deps[i]); free(deps); }
            return 1;
        }
        printf("Found %d store dependencies for %s\n", deps_count, argv[2]);

        int result = add_to_store_with_deps(argv[2], argv[3], (const char**)deps, deps_count);

        if (deps) {
            for (int i = 0; i < deps_count; i++) { if (deps[i]) free(deps[i]); }
            free(deps);
        }

        if (result != 0) fprintf(stderr,"Failed to add '%s' with dependencies to store.\n", argv[3]);
        return (result == 0) ? 0 : 1;
    }
    else if (strcmp(argv[1], "--add-with-explicit-deps") == 0) {
        // add with manual deps
        if (argc < 4) { fprintf(stderr,"Error: Missing arguments for --add-with-explicit-deps. Usage: --add-with-explicit-deps <source_path> <base_name> [dep_store_path...]\n"); return 1; }

        int deps_count = argc - 4;
        const char** deps = NULL;
        if (deps_count > 0) {
            deps = (const char**)&argv[4];
            for(int i=0; i < deps_count; ++i) {
                if(!deps[i] || strncmp(deps[i], NIX_STORE_PATH, strlen(NIX_STORE_PATH)) != 0) {
                    fprintf(stderr, "Error: Explicit dependency '%s' is not a valid store path.\n", deps[i] ? deps[i] : "(null)");
                    return 1;
                }
            }
        }

        if (add_to_store_with_deps(argv[2], argv[3], deps, deps_count) != 0) {
            fprintf(stderr,"Failed to add '%s' with explicit dependencies to store.\n", argv[3]);
            return 1;
        }
        return 0;
    }
    else if (strcmp(argv[1], "--add-boot-libs") == 0) {
        // add qnx boot libs
        int count = add_boot_libraries();
        if (count < 0) {
            fprintf(stderr,"Failed to add boot libraries.\n");
            return 1;
        }
        return 0;
    }

    // profile management
    else if (strcmp(argv[1], "--install") == 0) {
        // install to profile
        if (argc < 3) { fprintf(stderr,"Error: Missing store path for --install\n"); print_usage(); return 1; }
        const char* store_path_to_install = argv[2];
        const char* profile_name = (argc > 3) ? argv[3] : "default";

        if (strncmp(store_path_to_install, NIX_STORE_PATH, strlen(NIX_STORE_PATH)) != 0 || strstr(store_path_to_install, "..") != NULL) {
            fprintf(stderr, "Error: '%s' does not look like a valid store path (must start with %s and not contain '..')\n",
                    store_path_to_install, NIX_STORE_PATH);
            return 1;
        }

        if (install_to_profile(store_path_to_install, profile_name) == 0) {
            printf("\nInstallation complete. To use:\n");
            printf("  export PATH=\"/data/nix/profiles/%s/bin:$PATH\"\n", profile_name);
            printf("  # (You might also need to adjust LD_LIBRARY_PATH if not handled by wrappers)\n");
            return 0;
        }
        fprintf(stderr,"Installation into profile '%s' failed.\n", profile_name);
        return 1;
    }
    else if (strcmp(argv[1], "--create-profile") == 0) {
        // create new profile
        if (argc < 3) {
            fprintf(stderr, "Error: Missing profile name\n");
            return 1;
        }
        return create_profile(argv[2]);
    }
    else if (strcmp(argv[1], "--switch-profile") == 0) {
        // switch active profile
        if (argc < 3) {
            fprintf(stderr, "Error: Missing profile name\n");
            return 1;
        }
        return switch_profile(argv[2]);
    }

    // === Store Maintenance ===
    else if (strcmp(argv[1], "--verify") == 0) {
        // verify store path
        if (argc < 3) { fprintf(stderr,"Error: Missing path for --verify\n"); return 1; }
        int result = verify_store_path(argv[2]);
        return (result == 0) ? 0 : 1;
    }
    else if (strcmp(argv[1], "--gc") == 0) {
        // run garbage collection
        return (gc_collect_garbage() == 0) ? 0 : 1;
    }

    // === Query Operations ===
    else if (strcmp(argv[1], "--query-references") == 0) {
        // show dependencies
        if (argc < 3) { fprintf(stderr,"Error: Missing path for --query-references\n"); return 1; }

        char** refs = db_get_references(argv[2]);
        if (refs) {
            printf("References for %s:\n", argv[2]);
            int i = 0;
            if (refs[0] == NULL) {
                printf("  (No references registered)\n");
            } else {
                for (i = 0; refs[i] != NULL; i++) {
                    printf("  %s\n", refs[i]);
                    free(refs[i]);
                }
            }
            free(refs);
            return 0;
        }
        fprintf(stderr,"Path %s not found in database or error retrieving references.\n", argv[2]);
        return 1;
    }

    // === GC Root Management ===
    else if (strcmp(argv[1], "--add-root") == 0) {
        // register GC root
        if (argc < 3) { fprintf(stderr,"Error: Missing store path for --add-root\n"); return 1; }
        if (db_add_root(argv[2]) != 0) {
            fprintf(stderr,"Failed to add GC root.\n");
            return 1;
        }
        return 0;
    }
    else if (strcmp(argv[1], "--remove-root") == 0) {
        // unregister GC root
        if (argc < 3) { fprintf(stderr,"Error: Missing store path for --remove-root\n"); return 1; }
        if(db_remove_root(argv[2]) != 0) {
            fprintf(stderr,"Failed to remove GC root.\n");
            return 1;
        }
        return 0;
    }

    // === Profile Listing and Information ===
    else if (strcmp(argv[1], "--list-profiles") == 0) {
        // show profiles
        int count;
        ProfileInfo* profiles = list_profiles(&count);
        if (profiles) {
            printf("Available profiles:\n");
            for (int i = 0; i < count; i++) {
                printf("  %s -> %s\n", profiles[i].name, profiles[i].path);
            }
            free_profile_info(profiles, count);
            return 0;
        }
        return 1;
    }

    // === Generation Management ===
    else if (strcmp(argv[1], "--rollback") == 0) {
        // rollback profile
        if (argc < 3) {
            fprintf(stderr, "Error: Missing profile name for rollback\n");
            return 1;
        }
        return rollback_profile(argv[2]);
    }
    else if (strcmp(argv[1], "--list-generations") == 0) {
        // list generations
        if (argc < 3) {
            fprintf(stderr, "Error: Missing profile name\n");
            return 1;
        }
        time_t* timestamps;
        int count;
        if (get_profile_generations(argv[2], &timestamps, &count) == 0) {
            printf("Available generations for profile '%s':\n", argv[2]);
            for (int i = 0; i < count; i++) {
                printf("  %ld: %s", timestamps[i], ctime(&timestamps[i]));
            }
            free(timestamps);
            return 0;
        }
        return 1;
    }
    else if (strcmp(argv[1], "--switch-generation") == 0) {
        // switch generation
        if (argc < 4) {
            fprintf(stderr, "Error: Missing profile name or timestamp\n");
            return 1;
        }
        return switch_profile_generation(argv[2], atol(argv[3]));
    }
    else {
        // unknown command
        fprintf(stderr,"Unknown command: %s\n", argv[1]);
        print_usage();
        return 1;
    }

    return 0;
}