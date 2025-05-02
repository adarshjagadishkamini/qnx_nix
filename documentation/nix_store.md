# Nix Store Implementation (nix_store.c)

## Overview
Core implementation of the package store system, handling package installation, profile management, and store operations.

## Key Components

### Store Management
- `store_init()`: Initializes store directory structure
- `compute_store_path()`: Generates content-addressed paths using SHA-256
- `make_store_path_read_only()`: Ensures store path immutability

### Package Operations
- `add_to_store()`: Basic package addition
- `add_to_store_with_deps()`: Package addition with dependency tracking
- `verify_store_path()`: Validates store path integrity
- `scan_dependencies()`: Automatic dependency detection using ldd
- `add_boot_libraries()`: Special handling for system libraries

### Profile Management
- `create_profile()`: Creates new profile environment
- `install_to_profile()`: Installs packages into profiles
- `switch_profile()`: Changes active profile
- `list_profiles()`: Lists available profiles
- `create_wrapper_script()`: Creates executable wrappers
- `create_library_symlinks()`: Manages library dependencies

### Generation Management
- `create_generation()`: Creates profile generations
- `cleanup_old_generations()`: Maintains generation limit
- `rollback_profile()`: Reverts to previous generation
- `switch_profile_generation()`: Changes to specific generation

### Special Handling
- `handle_procboot()`: Manages early boot libraries with MMU alignment
- `verify_system_time()`: Ensures accurate timestamps for generations

## Implementation Details

### Store Path Creation
1. Computes hash from name and dependencies
2. Creates content-addressed directory
3. Copies package contents
4. Makes path read-only
5. Registers in database

### Profile Installation Process
1. Creates backup generation
2. Sets up directory structure
3. Creates wrapper scripts
4. Links libraries
5. Updates database

### Generation Management
- Uses timestamp-based naming
- Maintains configurable number of backups
- Supports atomic updates
- Provides rollback capability