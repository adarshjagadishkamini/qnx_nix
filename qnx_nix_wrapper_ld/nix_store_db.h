#ifndef NIX_STORE_DB_H
#define NIX_STORE_DB_H

#include <time.h> // For time_t if needed by DBEntry structure (though not directly used in API)

// Register a new path in the database with its references
// references should be a NULL-terminated array of strings
int db_register_path(const char* path, const char** references);

// Check if a path exists in the database
int db_path_exists(const char* path);

// Get all references for a path (returns a NULL-terminated array, must be freed by caller)
char** db_get_references(const char* path);

// Remove a path from the database (used internally by GC)
int db_remove_path(const char* path);

// ---- NEW FUNCTIONS ----
// Add a store path as a GC root
int db_add_root(const char* path);

// Remove a store path from being a GC root
int db_remove_root(const char* path);

// Profile-related database functions
int db_register_profile(const char* profile_name, const char* path);
int db_remove_profile(const char* profile_name);
char* db_get_profile_path(const char* profile_name);

// ---- End of new functions ----
#endif // NIX_STORE_DB_H