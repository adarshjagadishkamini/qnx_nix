/*
 * nix_store_db.c - Database functionality for Nix-like store
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <limits.h>
#include "nix_store.h"
#include "nix_store_db.h"

// Define the database file path
#define DB_PATH NIX_STORE_PATH "/.nix-db/db"
#define ROOTS_PATH NIX_STORE_PATH "/.nix-db/roots"

// Structure for database entries
typedef struct {
    char path[PATH_MAX];
    char references[10][PATH_MAX];  // Up to 10 references per entry
    int ref_count;
    time_t creation_time;
} DBEntry;

// Open the database file
static FILE* open_db(const char* mode) {
    // Make sure the directory exists first
    struct stat st = {0};
    char dir[PATH_MAX];
    snprintf(dir, PATH_MAX, NIX_STORE_PATH "/.nix-db");
    
    if (stat(dir, &st) == -1) {
        if (mkdir(dir, 0755) == -1) {
            fprintf(stderr, "Failed to create database directory: %s\n", strerror(errno));
            return NULL;
        }
    }
    
    return fopen(DB_PATH, mode);
}

// Register a new path in the database
int db_register_path(const char* path, const char** references) {
    FILE* db = open_db("a+");
    if (!db) {
        fprintf(stderr, "Failed to open database: %s\n", strerror(errno));
        return -1;
    }
    
    // Check if the path already exists
    if (db_path_exists(path)) {
        fclose(db);
        return 0;  // Already registered
    }
    
    // Create a new entry
    DBEntry entry;
    memset(&entry, 0, sizeof(entry));
    strncpy(entry.path, path, PATH_MAX - 1);
    
    // Add references
    entry.ref_count = 0;
    if (references) {
        for (int i = 0; references[i] != NULL && i < 10; i++) {
            strncpy(entry.references[i], references[i], PATH_MAX - 1);
            entry.ref_count++;
        }
    }
    
    // Set creation time
    entry.creation_time = time(NULL);
    
    // Write the entry to the database
    if (fwrite(&entry, sizeof(entry), 1, db) != 1) {
        fprintf(stderr, "Failed to write to database: %s\n", strerror(errno));
        fclose(db);
        return -1;
    }
    
    fclose(db);
    
    // Add to roots if this is a top-level store item
    if (strstr(path, "/nix/store/") == path) {
        FILE* roots = fopen(ROOTS_PATH, "a+");
        if (roots) {
            fprintf(roots, "%s\n", path);
            fclose(roots);
        }
    }
    
    return 0;
}

// Check if a path exists in the database
int db_path_exists(const char* path) {
    FILE* db = open_db("r");
    if (!db) {
        return 0;  // Database doesn't exist or can't be opened
    }
    
    DBEntry entry;
    while (fread(&entry, sizeof(entry), 1, db) == 1) {
        if (strcmp(entry.path, path) == 0) {
            fclose(db);
            return 1;
        }
    }
    
    fclose(db);
    return 0;
}

// Get all references for a path
char** db_get_references(const char* path) {
    FILE* db = open_db("r");
    if (!db) {
        return NULL;
    }
    
    DBEntry entry;
    while (fread(&entry, sizeof(entry), 1, db) == 1) {
        if (strcmp(entry.path, path) == 0) {
            // Found the entry, create a return array
            char** refs = malloc((entry.ref_count + 1) * sizeof(char*));
            if (!refs) {
                fclose(db);
                return NULL;
            }
            
            for (int i = 0; i < entry.ref_count; i++) {
                refs[i] = strdup(entry.references[i]);
                if (!refs[i]) {
                    // Memory allocation failed, free allocated memory so far
                    for (int j = 0; j < i; j++) {
                        free(refs[j]);
                    }
                    free(refs);
                    fclose(db);
                    return NULL;
                }
            }
            refs[entry.ref_count] = NULL;  // Null-terminate the array
            
            fclose(db);
            return refs;
        }
    }
    
    fclose(db);
    return NULL;
}

// Remove a path from the database
int db_remove_path(const char* path) {
    FILE* db = open_db("r");
    if (!db) {
        return -1;
    }
    
    // Create a temporary file for the updated database
    char temp_path[PATH_MAX];
    snprintf(temp_path, PATH_MAX, "%s.tmp", DB_PATH);
    
    FILE* temp_db = fopen(temp_path, "w");
    if (!temp_db) {
        fclose(db);
        return -1;
    }
    
    // Copy all entries except the one to remove
    DBEntry entry;
    while (fread(&entry, sizeof(entry), 1, db) == 1) {
        if (strcmp(entry.path, path) != 0) {
            fwrite(&entry, sizeof(entry), 1, temp_db);
        }
    }
    
    fclose(db);
    fclose(temp_db);
    
    // Replace the old database with the new one
    if (rename(temp_path, DB_PATH) == -1) {
        fprintf(stderr, "Failed to update database: %s\n", strerror(errno));
        return -1;
    }
    
    // Also remove from roots if present
    FILE* roots = fopen(ROOTS_PATH, "r");
    if (roots) {
        FILE* temp_roots = fopen(ROOTS_PATH ".tmp", "w");
        if (temp_roots) {
            char line[PATH_MAX];
            while (fgets(line, PATH_MAX, roots)) {
                // Remove newline if present
                size_t len = strlen(line);
                if (len > 0 && line[len-1] == '\n') {
                    line[len-1] = '\0';
                }
                
                if (strcmp(line, path) != 0) {
                    fprintf(temp_roots, "%s\n", line);
                }
            }
            fclose(temp_roots);
            fclose(roots);
            rename(ROOTS_PATH ".tmp", ROOTS_PATH);
        } else {
            fclose(roots);
        }
    }
    
    return 0;
}