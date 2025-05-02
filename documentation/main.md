# Command Line Interface (main.c)

## Overview
Implements the command-line interface for the package manager.

## Commands

### Store Operations
- `--init`: Initialize store directories
- `--add`: Add package to store
- `--add-recursively`: Add directory recursively
- `--add-with-deps`: Add with dependency detection
- `--add-with-explicit-deps`: Add with specified dependencies
- `--verify`: Verify store path integrity
- `--add-boot-libs`: Add system libraries to store

### Profile Management
- `--create-profile`: Create new profile
- `--switch-profile`: Change active profile
- `--install`: Install package to profile
- `--list-profiles`: Show available profiles
- `--list-generations`: List profile generations
- `--rollback`: Revert to previous generation
- `--switch-generation`: Change to specific generation

### Garbage Collection
- `--gc`: Run garbage collection
- `--add-root`: Register GC root
- `--remove-root`: Unregister GC root
- `--query-references`: Show package dependencies

## Implementation Details

### Command Processing
- Validates arguments
- Handles error conditions
- Reports operation status
- Maintains consistent output format

### Error Handling
- Reports missing arguments
- Validates path existence
- Checks operation permissions
- Provides descriptive messages