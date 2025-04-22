#ifndef NIX_STORE_DB_H
#define NIX_STORE_DB_H

#include <time.h>

// register path in db
int db_register_path(const char* path, const char** references);

// check path existence
int db_path_exists(const char* path);

// get path references
char** db_get_references(const char* path);

// remove path from db
int db_remove_path(const char* path);

// gc root management
int db_add_root(const char* path);
int db_remove_root(const char* path);

// profile db operations
int db_register_profile(const char* profile_name, const char* path);
int db_remove_profile(const char* profile_name);
char* db_get_profile_path(const char* profile_name);

// hash verification
int db_store_hash(const char* path, const char* hash);
char* db_get_hash(const char* path);
int db_verify_path_hash(const char* path);

#endif