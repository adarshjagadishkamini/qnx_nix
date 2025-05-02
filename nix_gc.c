// garbage collector implementation
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include "nix_store.h"
#include "nix_store_db.h"
#include <sys/param.h> // for MAXPATHLEN if PATH_MAX is not defined

#ifndef PATH_MAX
#define PATH_MAX MAXPATHLEN
#endif

#define PROFILES_DIR "/data/nix/profiles"

// path reference tracking
typedef struct PathRef {
    char path[PATH_MAX];
    int mark;  // for mark-and-sweep
    struct PathRef* next;
} PathRef;

// forward declarations
static void mark_path(PathRef* list, const char* path);

// add path to ref list
// Returns: Updated list on success, or original list on allocation failure
// Note: Memory allocation failure is considered non-fatal for GC since we can
// still keep the existing list and try to continue with partial collection
static PathRef* add_path_ref(PathRef* list, const char* path) {
    PathRef* ref = malloc(sizeof(PathRef));
    if (!ref) {
        fprintf(stderr, "GC Error: Failed to allocate memory for path reference.\n");
        fprintf(stderr, "Warning: Continuing with partial path list.\n");
        return list; // Return original list to allow partial collection
    }

    strncpy(ref->path, path, PATH_MAX - 1);
    ref->path[PATH_MAX - 1] = '\0';  // ensure null termination
    ref->mark = 0;
    ref->next = list;

    return ref;
}

// find path in ref list
static PathRef* find_path_ref(PathRef* list, const char* path) {
    PathRef* current = list;
    while (current) {
        if (strcmp(current->path, path) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

// extract base store path from full path
static char* extract_base_store_path(const char* full_path) {
     if (!full_path || strncmp(full_path, NIX_STORE_PATH, strlen(NIX_STORE_PATH)) != 0) {
         return NULL; // not a store path
     }

     const char* start_after_store = full_path + strlen(NIX_STORE_PATH);
     if (*start_after_store == '/') {
         start_after_store++; // skip the slash after /data/nix/store
     } else {
          return NULL; // invalid format
     }

     const char* end_of_base = strchr(start_after_store, '/');
     size_t base_len;
     if (end_of_base) {
         // path points inside a store directory (e.g., .../hash-name/bin/file)
         base_len = end_of_base - start_after_store;
     } else {
          // path *is* the store directory (e.g., .../hash-name)
          base_len = strlen(start_after_store);
     }

      if (base_len == 0) return NULL; // empty hash-name?

     // allocate memory for the base path string: /data/nix/store + / + hash-name + \0
     char* base_path = malloc(strlen(NIX_STORE_PATH) + 1 + base_len + 1);
     if (!base_path) {
          fprintf(stderr, "GC Error: Failed to allocate memory for base path extraction.\n");
          return NULL;
     }

     snprintf(base_path, strlen(NIX_STORE_PATH) + 1 + base_len + 1, "%s/%.*s", NIX_STORE_PATH, (int)base_len, start_after_store);

     return base_path;
}

// mark path and its references
static void mark_path(PathRef* list, const char* path) {
    // ensure path is a valid store path (starts with NIX_STORE_PATH)
    if (!path || strncmp(path, NIX_STORE_PATH, strlen(NIX_STORE_PATH)) != 0) {
        fprintf(stderr, "GC Warning: Attempting to mark non-store path: %s\n", path ? path : "(null)");
        return;
    }

    // find the base store path in our list
    PathRef* current = find_path_ref(list, path);
    if (!current) {
        // this should ideally not happen if the path came from DB roots or profile scan
        // if it does, it might mean a broken link or inconsistent state.
        fprintf(stderr, "GC Warning: Path %s to be marked not found in the initial store list. Skipping.\n", path);
        return;
    }

    if (current->mark) {
        return;  // already marked
    }

    // mark this base store path
    current->mark = 1;

    // mark all references (dependencies) of this store path
    char** refs = db_get_references(current->path);
    if (refs) {
        for (int i = 0; refs[i] != NULL; i++) {
            mark_path(list, refs[i]); // recursive call
            free(refs[i]);
        }
        free(refs);
    }
}

// scan profile dir and mark store paths
static void scan_profile_and_mark(PathRef* list, const char* profile_path) {
    DIR* dir = opendir(profile_path);
    if (!dir) {
        if (errno != ENOENT) { // don't warn if profile just doesn't exist
            fprintf(stderr, "GC Warning: Cannot open profile directory %s: %s\n", profile_path, strerror(errno));
        }
        return;
    }

    struct dirent* entry;
    char item_path[PATH_MAX];
    char target_path[PATH_MAX];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(item_path, PATH_MAX, "%s/%s", profile_path, entry->d_name);

        struct stat st;
        if (lstat(item_path, &st) == -1) {
             fprintf(stderr, "GC Warning: Cannot lstat profile item %s: %s\n", item_path, strerror(errno));
            continue;
        }

        if (S_ISLNK(st.st_mode)) {
            ssize_t len = readlink(item_path, target_path, sizeof(target_path) - 1);
            if (len != -1) {
                target_path[len] = '\0';

                // extract the base store path from the target
                char* base_store_path = extract_base_store_path(target_path);
                if (base_store_path) {
                    mark_path(list, base_store_path); // mark the base path
                    free(base_store_path); // free the extracted path
                }
            } else {
                 fprintf(stderr, "GC Warning: Cannot readlink profile symlink %s: %s\n", item_path, strerror(errno));
            }
        } else if (S_ISDIR(st.st_mode)) {
            // recursively scan subdirectories (bin, lib, etc.)
            scan_profile_and_mark(list, item_path);
        }
    }

    closedir(dir);
}

// free path ref list
static void free_path_refs(PathRef* list) {
    while (list) {
        PathRef* next = list->next;
        free(list);
        list = next;
    }
}

// collect unreachable paths
int gc_collect_garbage(void) {
    printf("Starting garbage collection...\n");
    // build a list of all paths currently existing in the store directory
    PathRef* paths = NULL;
    int path_count = 0;

    DIR* store_dir = opendir(NIX_STORE_PATH);
    if (!store_dir) {
        fprintf(stderr, "Failed to open store directory %s: %s\n", NIX_STORE_PATH, strerror(errno));
        return -1;
    }

    struct dirent* entry;
    while ((entry = readdir(store_dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, ".nix-db") == 0) { // ignore DB dir
            continue;
        }

        char full_path[PATH_MAX];
        snprintf(full_path, PATH_MAX, "%s/%s", NIX_STORE_PATH, entry->d_name);

        struct stat st_path;
        if (stat(full_path, &st_path) == 0 && S_ISDIR(st_path.st_mode)) {
             paths = add_path_ref(paths, full_path);
             path_count++;
        } else {
             fprintf(stderr,"GC Warning: Non-directory item found in store root: %s\n", entry->d_name);
        }
    }
    closedir(store_dir);
    printf("Found %d potential store paths in filesystem.\n", path_count);

    // mark phase
    printf("Marking roots...\n");
    // 1. mark roots from the database roots file
    FILE* roots_file = fopen(NIX_STORE_PATH "/.nix-db/roots", "r");
    if (roots_file) {
        char root[PATH_MAX];
        while (fgets(root, PATH_MAX, roots_file)) {
            size_t len = strlen(root);
            if (len > 0 && root[len-1] == '\n') {
                root[len-1] = '\0';
            }
             if (strlen(root) > 0) { // ensure not empty line
                printf("  Marking root from DB: %s\n", root);
                mark_path(paths, root);
            }
        }
        fclose(roots_file);
    } else {
         // don't warn if file doesn't exist, it's optional
         if (errno != ENOENT) {
             fprintf(stderr, "GC Warning: Could not read roots file %s: %s\n", NIX_STORE_PATH "/.nix-db/roots", strerror(errno));
         } else {
              printf("  No roots file found. Skipping DB roots.\n");
         }
    }

    // 2. mark roots derived from scanning profiles
    printf("Marking roots from profiles in %s...\n", PROFILES_DIR);
    DIR* profiles_root_dir = opendir(PROFILES_DIR);
    if (profiles_root_dir) {
        struct dirent* profile_entry;
        char profile_path[PATH_MAX];
        while ((profile_entry = readdir(profiles_root_dir)) != NULL) {
             if (strcmp(profile_entry->d_name, ".") == 0 || strcmp(profile_entry->d_name, "..") == 0) {
                continue;
            }
            snprintf(profile_path, PATH_MAX, "%s/%s", PROFILES_DIR, profile_entry->d_name);
             struct stat st_profile;
             // follow symlink if profile itself is a link (like profiles/default -> profiles/default-N)
             if (stat(profile_path, &st_profile) == 0 && S_ISDIR(st_profile.st_mode)) {
                  printf("  Scanning profile directory: %s\n", profile_path);
                  scan_profile_and_mark(paths, profile_path); // scan this profile directory
             }
        }
         closedir(profiles_root_dir);
    } else {
         // don't warn if dir doesn't exist
         if (errno != ENOENT) {
             fprintf(stderr, "GC Warning: Could not open profiles directory %s: %s\n", PROFILES_DIR, strerror(errno));
         } else {
             printf("  Profiles directory not found. Skipping profile scan.\n");
         }
    }

    // sweep phase
    printf("Sweeping unmarked paths...\n");
    PathRef* current = paths;
    int removed_count = 0;

    while (current) {
        if (!current->mark) {
            printf("Removing unused path: %s\n", current->path);

            // recursive removal using system rm -rf
            char cmd[PATH_MAX + 10];
            snprintf(cmd, sizeof(cmd), "rm -rf %s", current->path);
            int ret = system(cmd);
            if (ret == 0) {
                // remove from database only if successfully deleted from filesystem
                db_remove_path(current->path);
                removed_count++;
            } else {
                fprintf(stderr, "Failed to remove path from filesystem: %s (system rm -rf returned %d)\n", current->path, ret);
                // do not remove from DB if filesystem removal failed
            }
        }
        current = current->next;
    }

    printf("Garbage collection complete. Removed %d unused paths.\n", removed_count);

    // free the path list
    free_path_refs(paths);

    return 0;
}