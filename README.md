# QNX Nix Package Manager

A package management system for QNX that provides isolated environments and reproducible package management.

## Core Architecture 

### Store (/data/nix/store/)
- content-addressed storage with sha256
- hash-based package directories
- binaries in bin/
- libraries in lib/ 
- read-only paths
- dependency tracking in db

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

### Dependencies
- Automatic scanning using ldd
- Maps boot libraries to store paths
- Handles /proc/boot and /system/bin libraries
- Supports explicit dependency specification

### Profile System
- Multiple concurrent profiles
- Generation-based timestamps for rollbacks
- Clean environment isolation
- Automatic dependency linking
- Wrapper scripts with debug output

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
nix-store --init

# Add boot libraries
nix-store --add-boot-libs

# Add package with auto-detected dependencies
nix-store --add-with-deps /path/to/binary name

# Add package with explicit dependencies
nix-store --add-with-explicit-deps /path/to/binary name dep1 dep2
```

### Profile Management
```bash
# Create profile
nix-store --create-profile name

# Install package
nix-store --install store-path [profile-name]

# List profiles
nix-store --list-profiles

# List generations
nix-store --list-generations profile-name

# Switch generation
nix-store --switch-generation profile-name timestamp

# Rollback profile
nix-store --rollback profile-name
```

### Shell Environment
```bash
# Enter pure shell
nix-shell-qnx profile-name
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
# Wrapper with debug output
ORIG_PWD="$(pwd)"
ORIG_LD_LIBRARY_PATH="$LD_LIBRARY_PATH"
SCRIPT_DIR="$(dirname "$0")"
PROFILE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROFILE_LIB="$PROFILE_DIR/lib"

echo "Original working directory: $ORIG_PWD"
echo "Profile directory: $PROFILE_DIR"
echo "Profile lib directory: $PROFILE_LIB"
echo "Binary dependencies:"
ldd '/path/to/binary'

cd "$ORIG_PWD"
exec env - \
    PATH="$PATH" \
    PWD="$ORIG_PWD" \
    HOME="$HOME" \
    USER="$USER" \
    TERM="$TERM" \
    LD_LIBRARY_PATH="$PROFILE_LIB" \
    '/path/to/binary' "$@"
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
2. Manual package registration
3. No binary caching
4. Limited to scanning ldd dependencies
5. Time-based generations require accurate system time
6. Store paths must be under /data/nix/store
7. Root privileges required for installation

## Future Work

1. Remote package support
2. Automated package registration
3. Binary caching
4. Enhanced dependency detection
5. User-space installation support
6. Package building integration
7. Resource manager daemon implementation
