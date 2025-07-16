#ifndef QNIX_CONFIG_H
#define QNIX_CONFIG_H

#include <stdbool.h>

// Configuration settings structure
typedef struct {
    struct {
        bool allow_system_binaries;
        char* allowed_system_paths;
        char* preserved_env_vars;
        bool debug_wrappers;
    } shell;

    struct {
        char* store_path;
        bool enforce_readonly;
        bool verify_signatures;
        bool allow_user_install;
        int store_path_permissions;
    } store;

    struct {
        bool auto_scan;
        int max_depth;
        char* extra_lib_paths;
        char* scanner;
    } dependencies;

    struct {
        char* default_profile;
        bool auto_backup;
        char* timestamp_format;
        bool allow_user_profile_switch;
        int max_generations;
    } profiles;
} QnixConfig;

// String trimming utility function
char* trim(char* str);

// Initialize configuration with defaults
void config_init(void);

// Load configuration from file
int config_load(const char* config_file);

// Get the global configuration instance
QnixConfig* config_get(void);

#endif /* QNIX_CONFIG_H */
