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
#include "nix_store.h"

// Define the database file path
#define DB_PATH NIX_STORE_PATH "/.nix-db/db"

// Structure for database entries
typedef struct {
    char path[PATH_MAX];
    char references[10][PATH_MAX];  // Up to 10 references per entry
    int ref_count;
    time_t creation_time;
} DBEntry;

// Open the database file
static FILE* open_db(const char* mode) {
    return fopen(DB_PATH, mode);
}

// Register a new path in the database
int db_register_path(const char* path, const char** references) {
    FILE* db = open_db("a+");
    if (!db) {
        fprintf(stderr, "Failed to open database: %s\n", strerror(errno));
        return -1;
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
    
    return 0;
}