# QNIX Package Manager Code Structure

## Core Files
- `nix_store.c/h`: Core package store functionality
- `nix_store_db.c/h`: Database operations for package metadata
- `nix_gc.c`: Garbage collection implementation
- `nix_shell.c`: Shell environment management
- `main.c`: Command-line interface
- `sha256.c/h`: SHA-256 hash implementation

## Directory Structure
```
/data/nix/
├── store/          # Package store
│   ├── <hash>-pkg/ # Individual packages
│   └── .nix-db/    # Package database
└── profiles/       # User environments
```