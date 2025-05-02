# Garbage Collection Implementation (nix_gc.c)

## Overview
Implements mark-and-sweep garbage collection for unreachable store paths.

## Key Components

### Reference Tracking
- `PathRef`: Structure for tracking store paths during GC
- `add_path_ref()`: Adds path to reference list
- `find_path_ref()`: Locates path in reference list
- `free_path_refs()`: Cleans up reference tracking

### Path Management
- `extract_base_store_path()`: Extracts store path from full path
- `mark_path()`: Marks path and its dependencies as reachable
- `scan_profile_and_mark()`: Scans profile directories for references

### Collection Process
1. Builds list of all paths in store
2. Marks reachable paths:
   - From database roots
   - From profile references
   - From dependencies
3. Sweeps unmarked paths:
   - Removes from filesystem
   - Updates database

## Implementation Details

### Mark Phase
- Follows dependency graph recursively
- Handles symlinks in profiles
- Processes boot libraries
- Includes error recovery

### Sweep Phase
- Safe filesystem cleanup
- Atomic database updates
- Preserves marked paths
- Reports collection statistics

### Error Handling
- Continues on memory allocation failures
- Skips inaccessible paths
- Reports issues without failing
- Maintains database consistency