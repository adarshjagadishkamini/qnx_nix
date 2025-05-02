#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include "nix_store.h"

// required system utilities
static const char* essential_utils[] = {
    "/system/bin/bash",
    "/proc/boot/ls",
    "/system/bin/pwd",
    "/system/bin/cp",
    "/system/bin/mkdir",
    "/proc/boot/rm",
    "/proc/boot/cat",
    "/system/bin/which",
    NULL
};

// find tool in store
static char* find_store_path_for_util(const char* util_path) {
    const char* util_name = strrchr(util_path, '/');
    if (!util_name) return NULL;
    util_name++; // Skip the '/'

    DIR* dir = opendir(NIX_STORE_PATH);
    if (!dir) return NULL;

    char* result = NULL;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        // Check if this entry is for our utility
        if (strstr(entry->d_name, util_name) != NULL) {
            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", NIX_STORE_PATH, entry->d_name);
            
            // Verify it exists
            struct stat st;
            if (stat(full_path, &st) == 0) {
                result = strdup(full_path);
                break;
            }
        }
    }
    closedir(dir);
    return result;
}

// set up shell env
static int setup_environment(const char* profile_path) {
    char bin_path[PATH_MAX];
    char lib_path[PATH_MAX];
    char paths[PATH_MAX * 4] = "";  // Increased size for multiple paths

    // Construct bin path with overflow check
    int ret = snprintf(bin_path, sizeof(bin_path), "%s/bin", profile_path);
    if (ret < 0 || ret >= sizeof(bin_path)) {
        fprintf(stderr, "Profile bin path too long\n");
        return -1;
    }
    strcat(paths, bin_path);

    // Construct lib path with overflow check
    ret = snprintf(lib_path, sizeof(lib_path), "%s/lib", profile_path);
    if (ret < 0 || ret >= sizeof(lib_path)) {
        fprintf(stderr, "Profile lib path too long\n");
        return -1;
    }

    // Add essential utilities from store
    for (int i = 0; essential_utils[i] != NULL; i++) {
        char* store_path = find_store_path_for_util(essential_utils[i]);
        if (store_path) {
            char util_bin[PATH_MAX];
            snprintf(util_bin, sizeof(util_bin), "%s/bin", store_path);
            
            // Add to PATH if not already present
            if (strstr(paths, util_bin) == NULL) {
                strcat(paths, ":");
                strcat(paths, util_bin);
            }
            free(store_path);
        } else {
            fprintf(stderr, "Warning: Could not find store path for %s\n", essential_utils[i]);
        }
    }

    // Clear and set restricted environment
    char* term = getenv("TERM");
    char* user = getenv("USER");
    char* home = getenv("HOME");
    
    clearenv();  // Clear all environment variables
    
    // Restore minimal environment
    setenv("PATH", paths, 1);
    setenv("LD_LIBRARY_PATH", lib_path, 1);
    setenv("NIX_PROFILE", profile_path, 1);
    if (term) setenv("TERM", term, 1);
    if (user) setenv("USER", user, 1);
    if (home) setenv("HOME", home, 1);

    printf("Isolated environment set up:\n");
    printf("  PATH=%s\n", paths);
    printf("  LD_LIBRARY_PATH=%s\n", lib_path);
    printf("  NIX_PROFILE=%s\n", profile_path);

    return 0;
}

static void print_welcome_msg(const char* profile_name) {
    printf("\n");
    printf("Entering pure shell for profile: %s\n", profile_name);
    printf("Only packages from this profile and essential QNX utilities are available.\n");
    printf("Type 'exit' to leave the shell.\n\n");
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s PROFILE_NAME\n", argv[0]);
        return 1;
    }

    char profile_path[PATH_MAX];
    snprintf(profile_path, sizeof(profile_path), "/data/nix/profiles/%s", argv[1]);

    // Verify profile exists
    if (access(profile_path, F_OK) != 0) {
        fprintf(stderr, "Profile '%s' does not exist\n", argv[1]);
        return 1;
    }

    print_welcome_msg(argv[1]);
    if (setup_environment(profile_path) != 0) {
        return 1;
    }

    // Launch shell using bash instead of ksh
    char* shell_args[] = {"/system/bin/bash", NULL};
    execv("/system/bin/bash", shell_args);
    
    // If we get here, exec failed
    fprintf(stderr, "Failed to launch shell: %s\n", strerror(errno));
    return 1;
}
