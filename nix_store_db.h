/*
 * nix_store_db.h - Header for database functionality
 */
#ifndef NIX_STORE_DB_H
#define NIX_STORE_DB_H

#include <time.h>

// Database function prototypes
int db_register_path(const char* path, const char** references);
int db_path_exists(const char* path);
char** db_get_references(const char* path);
int db_remove_path(const char* path);

#endif /* NIX_STORE_DB_H */