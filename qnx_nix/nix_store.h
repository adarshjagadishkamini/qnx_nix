/*
 * nix_store.h - Header for Nix-like store implementation on QNX
 */
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
#include <sys/neutrino.h>  // QNX specific header
#include <sys/procfs.h>    // QNX process filesystem
#include <sys/iofunc.h>    // QNX I/O functions
#include <sys/dispatch.h>  // QNX message passing
#include "sha256.h"        // Our custom SHA-256 implementation

// Define the base store path
#define NIX_STORE_PATH "/data/nix/store"

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
int make_store_path_read_only(const char* path);
int verify_store_path(const char* path);
int gc_collect_garbage(void);

#endif /* NIX_STORE_H */