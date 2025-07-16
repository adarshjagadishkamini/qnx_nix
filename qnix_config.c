#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qnix_config.h"
#include <limits.h>
#include <sys/param.h> // For MAXPATHLEN

#ifndef PATH_MAX
#define PATH_MAX MAXPATHLEN
#endif

#define CONFIG_FILE "nix.conf"
#define MAX_LINE 1024

static QnixConfig config;
static bool initialized = false;

// Forward declaration
static void config_free(void);

// Initialize with default values
void config_init(void) {
    // Free existing configuration if already initialized
    if (initialized) {
        config_free();
    }

    // Shell defaults - starting with strict isolation by default
    config.shell.allow_system_binaries = false;
    config.shell.allowed_system_paths = strdup("/system/bin,/bin,/sbin,/proc/boot");
    config.shell.preserved_env_vars = strdup("HOME,USER,TERM,DISPLAY,PWD");
    config.shell.debug_wrappers = false;

    // Store defaults
    config.store.store_path = strdup("/data/nix/store");
    config.store.enforce_readonly = true;
    config.store.verify_signatures = false;
    config.store.allow_user_install = false;
    config.store.store_path_permissions = 0555;

    // Dependencies defaults
    config.dependencies.auto_scan = true;
    config.dependencies.max_depth = 10;
    config.dependencies.extra_lib_paths = strdup("/proc/boot,/system/lib");
    config.dependencies.scanner = strdup("ldd");

    // Profile defaults
    config.profiles.default_profile = strdup("default");
    config.profiles.auto_backup = true;
    config.profiles.timestamp_format = strdup("%Y%m%d%H%M%S");
    config.profiles.allow_user_profile_switch = false;
    config.profiles.max_generations = 10;

    initialized = true;
}

static int install_default_config(void) {
    const char* default_config = 
        "# QNix Configuration File\n\n"
        "# Shell settings\n"
        "shell.allow_system_binaries = false\n"
        "shell.allowed_system_paths = /system/bin,/bin,/sbin,/proc/boot\n"
        "shell.preserved_env_vars = HOME,USER,TERM,DISPLAY,PWD\n"
        "shell.debug_wrappers = false\n\n"
        "# Store settings\n"
        "store.store_path = /data/nix/store\n"
        "store.enforce_readonly = true\n"
        "store.verify_signatures = false\n"
        "store.allow_user_install = false\n"
        "store.store_path_permissions = 0555\n\n"
        "# Dependencies settings\n"
        "dependencies.auto_scan = true\n"
        "dependencies.max_depth = 10\n"
        "dependencies.extra_lib_paths = /proc/boot,/system/lib\n"
        "dependencies.scanner = ldd\n\n"
        "# Profile settings\n"
        "profiles.default_profile = default\n"
        "profiles.auto_backup = true\n"
        "profiles.timestamp_format = %Y%m%d%H%M%S\n"
        "profiles.allow_user_profile_switch = false\n"
        "profiles.max_generations = 10\n";

    FILE* fp = fopen(CONFIG_FILE, "wx"); // Open for write, fail if exists
    if (!fp) {
        if (errno == EEXIST) {
            return 0; // File exists, which is fine
        }
        fprintf(stderr, "Failed to create default config file: %s\n", strerror(errno));
        return -1;
    }

    size_t written = fwrite(default_config, 1, strlen(default_config), fp);
    if (written != strlen(default_config)) {
        fprintf(stderr, "Failed to write complete default config\n");
        fclose(fp);
        return -1;
    }

    fclose(fp);
    printf("Created default configuration file\n");
    return 0;
}

static void config_free(void) {
    if (!initialized) return;

    free(config.shell.allowed_system_paths);
    free(config.shell.preserved_env_vars);
    free(config.store.store_path);
    free(config.dependencies.extra_lib_paths);
    free(config.dependencies.scanner);
    free(config.profiles.default_profile);
    free(config.profiles.timestamp_format);

    initialized = false;
}

char* trim(char* str) {
    char* end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

static bool parse_bool(const char* value) {
    return (strcasecmp(value, "true") == 0 || 
            strcasecmp(value, "yes") == 0 || 
            strcasecmp(value, "1") == 0);
}

// Validate a comma-separated path list
static bool validate_path_list(const char* paths) {
    char* paths_copy = strdup(paths);
    char* path = strtok(paths_copy, ",");
    bool valid = true;

    while (path && valid) {
        // Trim whitespace
        path = trim(path);
        
        // Basic path validation
        if (strlen(path) == 0 || 
            strstr(path, "..") != NULL || // No parent directory references
            path[0] != '/') {  // Must be absolute paths
            valid = false;
        }
        path = strtok(NULL, ",");
    }

    free(paths_copy);
    return valid;
}

int config_load(const char* config_path) {
    const char* path = config_path ? config_path : CONFIG_FILE;
    
    // Try to install default config if no config file exists
    if (access(path, F_OK) != 0) {
        printf("No configuration file found, installing defaults...\n");
        if (install_default_config() != 0) {
            fprintf(stderr, "Warning: Failed to install default config\n");
        }
    }

    FILE* fp;
    char line[MAX_LINE];
    char* key;
    char* value;
    char cwd[PATH_MAX];

    // Initialize with defaults first
    config_init();

    fp = fopen(path, "r");

    // Print debug info
    getcwd(cwd, sizeof(cwd));
    printf("Loading config from: %s (CWD: %s)\n", path, cwd);

    if (!fp) {
        fprintf(stderr, "Error: Could not open config file %s: %s\n", 
                path, strerror(errno));
        return -1;
    }

    printf("Successfully opened config file\n");

    while (fgets(line, sizeof(line), fp)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '[') continue;

        // Remove trailing newline
        line[strcspn(line, "\n")] = 0;

        // Split into key=value
        key = line;
        value = strchr(line, '=');
        if (!value) continue;
        *value++ = '\0';

        key = trim(key);
        value = trim(value);

        // Parse settings
        if (strcmp(key, "shell.allow_system_binaries") == 0) {
            config.shell.allow_system_binaries = parse_bool(value);
            printf("Set allow_system_binaries to: %s\n", 
                   config.shell.allow_system_binaries ? "true" : "false");
        }
        else if (strcmp(key, "shell.allowed_system_paths") == 0) {
            if (validate_path_list(value)) {
                free(config.shell.allowed_system_paths);
                config.shell.allowed_system_paths = strdup(value);
                //printf("Set allowed_system_paths to: %s\n", value);
            } else {
                fprintf(stderr, "Warning: Invalid system paths ignored: %s\n", value);
            }
        }
        else if (strcmp(key, "shell.preserved_env_vars") == 0) {
            // Validate environment variable names
            bool valid = true;
            char* vars_copy = strdup(value);
            char* var = strtok(vars_copy, ",");
            while (var && valid) {
                var = trim(var);
                // Basic environment variable name validation
                for (char* c = var; *c; c++) {
                    if (!isalnum((unsigned char)*c) && *c != '_') {
                        valid = false;
                        break;
                    }
                }
                var = strtok(NULL, ",");
            }
            free(vars_copy);

            if (valid) {
                free(config.shell.preserved_env_vars);
                config.shell.preserved_env_vars = strdup(value);
                printf("Set preserved_env_vars to: %s\n", value);
            } else {
                fprintf(stderr, "Warning: Invalid environment variables ignored: %s\n", value);
            }
        }
        else if (strcmp(key, "shell.debug_wrappers") == 0) {
            config.shell.debug_wrappers = parse_bool(value);
        }
        else if (strcmp(key, "store.enforce_readonly") == 0) {
            config.store.enforce_readonly = parse_bool(value);
        }
        else if (strcmp(key, "store.store_path") == 0) {
            if (value[0] == '/') { // Must be absolute path
                free(config.store.store_path);
                config.store.store_path = strdup(value);
            }
        }
        else if (strcmp(key, "store.verify_signatures") == 0) {
            config.store.verify_signatures = parse_bool(value);
        }
        else if (strcmp(key, "store.allow_user_install") == 0) {
            config.store.allow_user_install = parse_bool(value);
        }
        else if (strcmp(key, "store.store_path_permissions") == 0) {
            int perms = strtol(value, NULL, 8);
            if (perms >= 0 && perms <= 0777) {
                config.store.store_path_permissions = perms;
            }
        }
        else if (strcmp(key, "dependencies.auto_scan") == 0) {
            config.dependencies.auto_scan = parse_bool(value);
        }
        else if (strcmp(key, "dependencies.max_depth") == 0) {
            int depth = atoi(value);
            if (depth > 0 && depth <= 100) {
                config.dependencies.max_depth = depth;
            }
        }
        else if (strcmp(key, "dependencies.extra_lib_paths") == 0) {
            if (validate_path_list(value)) {
                free(config.dependencies.extra_lib_paths);
                config.dependencies.extra_lib_paths = strdup(value);
            }
        }
        else if (strcmp(key, "dependencies.scanner") == 0) {
            if (strchr(value, '/') == NULL) { // Must be program name only
                free(config.dependencies.scanner);
                config.dependencies.scanner = strdup(value);
            }
        }
        else if (strcmp(key, "profiles.default_profile") == 0) {
            // Basic profile name validation
            bool valid = true;
            for (const char* c = value; *c; c++) {
                if (!isalnum((unsigned char)*c) && *c != '_' && *c != '-') {
                    valid = false;
                    break;
                }
            }
            if (valid) {
                free(config.profiles.default_profile);
                config.profiles.default_profile = strdup(value);
            }
        }
        else if (strcmp(key, "profiles.auto_backup") == 0) {
            config.profiles.auto_backup = parse_bool(value);
        }
        else if (strcmp(key, "profiles.timestamp_format") == 0) {
            free(config.profiles.timestamp_format);
            config.profiles.timestamp_format = strdup(value);
        }
        else if (strcmp(key, "profiles.allow_user_profile_switch") == 0) {
            config.profiles.allow_user_profile_switch = parse_bool(value);
        }
        else if (strcmp(key, "profiles.max_generations") == 0) {
            int gens = atoi(value);
            if (gens >= 0 && gens <= 1000) {
                config.profiles.max_generations = gens;
            }
        }
    }

    fclose(fp);
    return 0;
}

QnixConfig* config_get(void) {
    if (!initialized) config_init();
    return &config;
}
