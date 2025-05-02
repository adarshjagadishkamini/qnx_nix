# QNIX Package Manager User Guide

## Basic Usage

### Installation
```bash
# Initialize store
nix-store --init

# Add system libraries
nix-store --add-boot-libs
```

### Package Management
```bash
# Add package to store
nix-store --add /path/to/package package-name

# Add with dependencies
nix-store --add-with-deps /path/to/binary binary-name
```

### Profile Management
```bash
# Create profile
nix-store --create-profile myprofile

# Install to profile
nix-store --install store-path myprofile

# Switch profiles
nix-store --switch-profile myprofile
```

### Shell Usage
```bash
# Enter isolated shell
nix-shell-qnx myprofile
```

## Advanced Features

### Generation Management
```bash
# List generations
nix-store --list-generations myprofile

# Rollback
nix-store --rollback myprofile

# Switch to specific generation
nix-store --switch-generation myprofile timestamp
```

### Garbage Collection
```bash
# Run collection
nix-store --gc

# Add GC root
nix-store --add-root store-path
```

## Common Workflows

### Setting Up New Environment
1. Initialize store
2. Add system libraries
3. Create profile
4. Add packages
5. Enter shell

### Package Updates
1. Add new package version
2. Update profile
3. Previous generation preserved automatically

### Maintenance
1. Regular garbage collection
2. Profile cleanup
3. Generation management
4. Database verification