#ifndef NIX_STORE_H
#define NIX_STORE_H

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/neutrino.h>  
#include <sys/procfs.h>    // QNX process filesystem
#include <sys/iofunc.h>    // QNX I/O functions
#include <sys/dispatch.h>  // QNX message passing
#include "sha256.h"        // Our custom SHA-256 implementation
#include <limits.h> // PATH_MAX

// Define the base store path
#define NIX_STORE_PATH "/data/nix/store"

// Define time synchronization constants
#define MIN_VALID_TIMESTAMP 1746181969  // 2025-05-02 12:33
#define MAX_GENERATIONS 50              // Maximum number of generations to keep

// Structure to represent a store path
typedef struct {
    char path[PATH_MAX];
    char hash[SHA256_DIGEST_STRING_LENGTH];
    mode_t mode;
    uid_t owner;
    gid_t group;
} StorePathEntry;

// Function prototypes
int store_init(void);
char* compute_store_path(const char* name, const char* hash, const char** references);
int add_to_store(const char* source_path, const char* name, int recursive);
int add_to_store_with_deps(const char* source_path, const char* name, const char** deps, int deps_count);
int make_store_path_read_only(const char* path);
int verify_store_path(const char* path);
int gc_collect_garbage(void);
int scan_dependencies(const char* exec_path, char*** deps_out);
int add_boot_libraries(void);
int rollback_profile(const char* profile_name);
int get_profile_generations(const char* profile_name, time_t** timestamps, int* count);
int switch_profile_generation(const char* profile_name, time_t timestamp);

// Time validation and generation management
int verify_system_time(void);
int create_generation(const char* profile_name, time_t* timestamp);
int cleanup_old_generations(const char* profile_name);
int handle_procboot(const char* src_path, const char* dest_path);

// Structure to store profile information
typedef struct {
    char path[PATH_MAX];
    char* name;
    time_t timestamp;
} ProfileInfo;

// Profile-related functions - cleaned up API
int create_profile(const char* profile_name);                           // Creates empty profile
int install_to_profile(const char* store_path, const char* profile_name); // Installs package into profile (creates wrappers/symlinks)
int switch_profile(const char* profile_name);                          // Changes current profile
ProfileInfo* list_profiles(int* count);                                // Lists available profiles
void free_profile_info(ProfileInfo* profiles, int count);             // Cleanup helper

#endif /* NIX_STORE_H */