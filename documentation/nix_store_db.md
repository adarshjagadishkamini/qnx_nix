# Store Database Implementation (nix_store_db.c)

## Overview
Manages the persistent storage of package metadata, dependencies, and GC roots.

## Components

### Package Registration
- `db_register_path()`: Stores package metadata and dependencies
- `db_path_exists()`: Verifies package registration
- `db_remove_path()`: Removes package from database

### Dependency Management
- `db_get_references()`: Retrieves package dependencies
- `db_store_references()`: Stores dependency relationships
- `db_update_references()`: Updates dependency information

### GC Root Management
- `db_add_root()`: Marks package as GC root
- `db_remove_root()`: Removes GC root status
- `db_is_root()`: Checks root status

### Hash Verification
- `db_store_hash()`: Stores package content hash
- `db_get_hash()`: Retrieves stored hash
- `db_verify_path_hash()`: Validates package integrity

### Profile Database
- `db_register_profile()`: Records profile metadata
- `db_remove_profile()`: Removes profile registration
- `db_get_profile_path()`: Retrieves profile information

## Database Structure
```
.nix-db/
├── packages    # Package metadata and dependencies
├── roots      # GC root markers
└── profiles   # Profile registrations
```

## Implementation Details
- Uses flat file storage for simplicity
- Maintains atomic updates with temporary files
- Includes error recovery mechanisms
- Supports concurrent access safety