# QNX Nix Package Manager

A package management system for QNX that provides isolated environments and reproducible package management.

## Core Architecture

### Store (/data/nix/store/)
- Content-addressed storage using SHA256 hashes
- Each package gets a unique hash-based directory
- Binary files stored in bin/ subdirectory
- Libraries stored in lib/ subdirectory
- All store paths are immutable (read-only)
- Database in .nix-db/ tracks dependencies

### Profiles (/data/nix/profiles/)
- Named environments (test1, test2, etc.)
- Each profile contains:
  - bin/ - Wrapper scripts for executables
  - lib/ - Symlinks to required libraries
  - share/ - Documentation and data files
- Generation backups with timestamps
- Atomic updates and rollbacks

### Shell Environments
- Pure isolated shells per profile
- Limited PATH to profile + essential utils
- Clean environment variables
- Profile-specific library paths

## Package Flow

1. Adding to Store:
   ```
   Source File → Hash Computation → Store Path Creation → File Copy
   ```

2. Installing to Profile:
   ```
   Store Package → Generate Wrappers → Create Lib Links → Register in Profile
   ```

3. Runtime Execution:
   ```
   Wrapper Script → Set LD_LIBRARY_PATH → Execute Binary → Load Libraries
   ```

## Key Features

### Package Management
- Content-addressed storage
- Automatic dependency tracking
- Multiple versions can coexist
- Garbage collection
- Profile generations

### Dependencies
- Automatic scanning using ldd
- Tracks library requirements
- Maps system libraries
- Handles boot libraries

### Profile System
- Multiple concurrent profiles
- Generation-based rollbacks
- Clean environment isolation
- Easy profile switching

## Command Reference

### Store Operations
```bash
# Initialize store
./nix-store --init

# Add system utilities
./nix-store --add-boot-libs

# Add package with dependencies
./nix-store --add-with-deps /path/to/binary name
```

### Profile Management
```bash
# Create profile
./nix-store --create-profile name

# Install package
./nix-store --install store-path profile-name

# Switch profiles
./nix-store --switch-profile name

# Rollback profile
./nix-store --rollback profile-name
```

### Shell Environment
```bash
# Enter isolated shell
./nix-shell-qnx profile-name
```

## Directory Structure
```
/data/nix/
├── store/                      # Package store
│   ├── <hash>-package/        # Individual packages
│   │   ├── bin/              # Executables
│   │   └── lib/              # Libraries
│   └── .nix-db/              # Package database
└── profiles/                  # User environments
    ├── test1/                # Named profile
    │   ├── bin/             # Wrapper scripts
    │   └── lib/             # Library symlinks
    └── current -> test1     # Current profile
```

## Implementation Details

### Wrapper Scripts
- Located in profile's bin/
- Set up correct LD_LIBRARY_PATH
- Execute store binaries
- Preserve environment isolation

Example wrapper:
```bash
#!/bin/sh
PROFILE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
export LD_LIBRARY_PATH="$PROFILE_DIR/lib"
exec "/data/nix/store/<hash>-pkg/bin/program" "$@"
```

### Dependencies
- Scanned automatically using ldd
- Stored in database
- Referenced by hash
- Tracked for garbage collection

### Garbage Collection
- Starts from root profiles
- Follows dependencies
- Removes unreachable packages
- Preserves active profiles

### Generation Management
- Full copies for reliability
- Timestamp-based naming
- Easy rollback support
- Profile switching

## Benefits

1. Isolation
   - Clean environment per profile
   - No dependency conflicts
   - Predictable execution

2. Reproducibility
   - Hash-based storage
   - Explicit dependencies
   - Immutable packages

3. Flexibility
   - Multiple profiles
   - Easy rollbacks
   - System integration

4. Management
   - Simple package addition
   - Clean uninstallation
   - Automatic cleanup

## Current Limitations

1. No remote repositories
2. Manual dependency resolution
3. No binary cache
4. Limited to QNX system utilities

## Future Work

1. Remote package support
2. Binary caching
3. Better dependency resolution
4. Multi-user support
5. Package building
