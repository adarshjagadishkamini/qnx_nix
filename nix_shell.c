#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/param.h> // For MAXPATHLEN
#include "qnix_config.h"

#ifndef PATH_MAX
#define PATH_MAX MAXPATHLEN
#endif

static void print_welcome_msg(const char* profile_name) {
    printf("\n");
    printf("Entering pure shell for profile: %s\n", profile_name);
    if (config_get()->shell.allow_system_binaries) {
        printf("System binaries from allowed paths are accessible.\n");
        printf("Allowed paths: %s\n", config_get()->shell.allowed_system_paths);
    } else {
        printf("Only packages from this profile and essential QNX utilities are available.\n");
    }
    printf("Type 'exit' to leave the shell.\n\n");
}

// Helper function to check if a path exists and is accessible
static int path_exists(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

static bool validate_shell_environment(void) {
    QnixConfig* cfg = config_get();
    char* path = getenv("PATH");
    
    // Verify PATH containment
    if (cfg->shell.allow_system_binaries == false && path != NULL) {
        char* path_copy = strdup(path);
        char* dir = strtok(path_copy, ":");
        char profile_bin[PATH_MAX];
        
        snprintf(profile_bin, sizeof(profile_bin), "%s/bin", getenv("NIX_PROFILE"));
        
        while (dir != NULL) {
            if (strcmp(dir, profile_bin) != 0) {
                fprintf(stderr, "Warning: Non-profile path found in PATH: %s\n", dir);
                free(path_copy);
                return false;
            }
            dir = strtok(NULL, ":");
        }
        free(path_copy);
    }

    // Verify environment variables
    char** env;
    for (env = environ; *env != NULL; env++) {
        char* var_name = strdup(*env);
        char* equals = strchr(var_name, '=');
        if (equals) *equals = '\0';

        // Check if variable is in preserved list
        bool preserved = false;
        char* preserved_copy = strdup(cfg->shell.preserved_env_vars);
        char* preserved_var = strtok(preserved_copy, ",");
        
        while (preserved_var != NULL) {
            preserved_var = trim(preserved_var);
            if (strcmp(preserved_var, var_name) == 0) {
                preserved = true;
                break;
            }
            preserved_var = strtok(NULL, ",");
        }
        free(preserved_copy);

        // Check essential variables we always keep
        if (!preserved && 
            strcmp(var_name, "PATH") != 0 &&
            strcmp(var_name, "LD_LIBRARY_PATH") != 0 &&
            strcmp(var_name, "NIX_PROFILE") != 0 &&
            strcmp(var_name, "PS1") != 0) {
            fprintf(stderr, "Warning: Non-preserved environment variable: %s\n", var_name);
            free(var_name);
            return false;
        }
        free(var_name);
    }

    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s PROFILE_NAME\n", argv[0]);
        return 1;
    }

    // Load configuration
    if (config_load(NULL) != 0) {
        fprintf(stderr, "Warning: Using default configuration\n");
    }
    QnixConfig* cfg = config_get();

    // Construct and check profile path
    char profile_path[PATH_MAX];
    int ret = snprintf(profile_path, sizeof(profile_path), "/data/nix/profiles/%s", argv[1]);
    if (ret < 0 || ret >= sizeof(profile_path)) {
        fprintf(stderr, "Error: Profile path too long for '%s'\n", argv[1]);
        return 1;
    }

    // Verify profile exists
    if (!path_exists(profile_path)) {
        fprintf(stderr, "Profile '%s' does not exist\n", argv[1]);
        return 1;
    }

    print_welcome_msg(argv[1]);

    // Set up the isolated environment
    char bin_path[PATH_MAX];
    char lib_path[PATH_MAX];
    
    ret = snprintf(bin_path, sizeof(bin_path), "%s/bin", profile_path);
    if (ret < 0 || ret >= sizeof(bin_path)) {
        fprintf(stderr, "Error: Bin path too long for profile '%s'\n", argv[1]);
        return 1;
    }

    ret = snprintf(lib_path, sizeof(lib_path), "%s/lib", profile_path);
    if (ret < 0 || ret >= sizeof(lib_path)) {
        fprintf(stderr, "Error: Lib path too long for profile '%s'\n", argv[1]);
        return 1;
    }

    // Validate all library paths are from store or the current profile's lib
    char* ld_paths = strdup(lib_path);
    char* path = strtok(ld_paths, ":");
    while (path) {
        if (strncmp(path, "/data/nix/store/", 16) != 0 && strcmp(path, lib_path) != 0) {
            fprintf(stderr, "Error: Non-store library path detected: %s\n", path);
            free(ld_paths);
            return 1;
        }
        path = strtok(NULL, ":");
    }
    free(ld_paths);

    // Save original environment variables we might need
    char* orig_env[128];
    int env_count = 0;
    extern char** environ;
    for (char** env = environ; *env && env_count < 128; env++) {
        orig_env[env_count++] = strdup(*env);
    }

    // Clear environment and set minimal vars
    clearenv();

    // Set PATH based on configuration
    char final_path[PATH_MAX * 2] = "";
    if (cfg->shell.allow_system_binaries && cfg->shell.allowed_system_paths) {
        // Convert comma-separated system paths to colon-separated
        char* system_paths = strdup(cfg->shell.allowed_system_paths);
        char* path = strtok(system_paths, ",");
        int first = 1;
        while (path) {
            path = trim(path);
            if (!first) strncat(final_path, ":", sizeof(final_path) - strlen(final_path) - 1);
            strncat(final_path, path, sizeof(final_path) - strlen(final_path) - 1);
            first = 0;
            path = strtok(NULL, ",");
        }
        free(system_paths);
        // Add profile bin at the end
        strncat(final_path, ":", sizeof(final_path) - strlen(final_path) - 1);
        strncat(final_path, bin_path, sizeof(final_path) - strlen(final_path) - 1);
    } else {
        // Profile-only mode - just use profile bin
        strncpy(final_path, bin_path, sizeof(final_path) - 1);
    }

    // Set essential environment variables
    setenv("PATH", final_path, 1);
    setenv("LD_LIBRARY_PATH", lib_path, 1);
    setenv("NIX_PROFILE", profile_path, 1);

    // Set custom shell prompt
    char ps1[128];
    snprintf(ps1, sizeof(ps1), "%s-nix-shell# ", argv[1]);
    setenv("PS1", ps1, 1);
    
    // Preserve configured environment variables
    if (cfg->shell.preserved_env_vars) {
        char* preserved_vars = strdup(cfg->shell.preserved_env_vars);
        char* var = strtok(preserved_vars, ",");
        while (var) {
            // Find the original value from saved environment
            for (int i = 0; i < env_count; i++) {
                if (strncmp(orig_env[i], var, strlen(var)) == 0 && 
                    orig_env[i][strlen(var)] == '=') {
                    setenv(var, strchr(orig_env[i], '=') + 1, 1);
                    break;
                }
            }
            var = strtok(NULL, ",");
        }
        free(preserved_vars);
    }

    // Clean up saved environment
    for (int i = 0; i < env_count; i++) {
        free(orig_env[i]);
    }

    if (cfg->shell.debug_wrappers) {
        printf("DEBUG: PATH=%s\n", final_path);
        printf("DEBUG: LD_LIBRARY_PATH=%s\n", lib_path);
    }

    // Isolation: use a shell wrapper script to block absolute paths
    if (!cfg->shell.allow_system_binaries) {
        // Use the real bash binary from the Nix store
        const char* bash_path = "/data/nix/store/c0ea1e8f1446cfa89963b8c6f507a2048768cf5d786f25166e969018f198ba22-bash/bin/bash";
        // Set PATH to only the profile's bin
        setenv("PATH", bin_path, 1);
        // Set LD_LIBRARY_PATH to all Nix store dependency dirs (colon-separated)
        setenv("LD_LIBRARY_PATH",
            "/data/nix/store/186e6f5af0a93da0a6e23978adefded62488bcde51f20c8a5e1012781ac6c25c-libncursesw.so.1:"
            "/data/nix/store/da7c0bc28f9c338b77f7ab0a9a1c12d64d0e37b7d8ca1b0ddf7092754d1c7028-libintl.so.1:"
            "/data/nix/store/132445306ab076fde62c7e5ae9d395563b11867d640d53b829e8a034ce5e9b20-libiconv.so.1:"
            "/data/nix/store/9f0c5e501bed08687a2d2d1244b3b9336e5e76227db113bacf50cc5c4d404e60-libc.so.6:"
            "/data/nix/store/7cd20568963b07497789a9ba47635bcb21cce11476c3d9d67163c7748fb3a6f9-libregex.so.1:"
            "/data/nix/store/92cc1c04c0b5f1af885e0294b36189e1fafc551f913038f78970158ca198c89b-libgcc_s.so.1",
            1);
        // Use bash preexec to block absolute path execution
        char* preexec_block =
            "preexec() {\n"
            "  case \"$BASH_COMMAND\" in\n"
            "    $PATH/*) ;;\n"
            "    /*) echo 'Absolute path execution is not allowed in isolated shell, killing shell.'; kill -KILL $$ ;;\n"
            "  esac\n"
            "}\n"
            "trap preexec DEBUG\n";
        // Start bash with preexec_block in --rcfile
        char rcfile_path[PATH_MAX];
        snprintf(rcfile_path, sizeof(rcfile_path), "%s/nix_shell_bashrc", profile_path);
        FILE* rc = fopen(rcfile_path, "w");
        if (!rc) {
            fprintf(stderr, "Failed to create bashrc: %s\n", strerror(errno));
            return 1;
        }
        fprintf(rc, "%s", preexec_block);
        fclose(rc);
        chmod(rcfile_path, 0700);
        execl(bash_path, "bash", "--noprofile", "--rcfile", rcfile_path, NULL);
        fprintf(stderr, "Failed to launch isolated bash: %s\n", strerror(errno));
        return 1;
    }

    // Validate environment isolation
    if (!validate_shell_environment()) {
        fprintf(stderr, "Shell environment validation failed\n");
        if (!cfg->shell.debug_wrappers) {
            return 1; // Only exit if not in debug mode
        }
    }
    
    // Launch shell for non-isolated mode (still using nix store bash)
    const char* bash_path = "/data/nix/store/c0ea1e8f1446cfa89963b8c6f507a2048768cf5d786f25166e969018f198ba22-bash/bin/bash";
    if (!path_exists(bash_path)) {
        fprintf(stderr, "Cannot launch shell: Nix store bash not found\n");
        return 1;
    }
    execl(bash_path, "bash", NULL);
    // If we get here, exec failed
    fprintf(stderr, "Failed to launch shell: %s\n", strerror(errno));
    return 1;
}
