#!/bin/sh
# Clean up from previous tests
rm -rf /data/nix/store/*
rm -rf /data/nix/store/.nix-db

# Initialize
./nix-store --init
echo "Initialized store"

# Add single file
echo "Test content" > single-file.txt
./nix-store --add single-file.txt single-file
echo "Added single file"

# Add directory
mkdir -p test-pkg/bin
cat << 'EOF' > test-pkg/bin/hello.sh
#!/bin/sh
echo 'Hello from QNX Nix'
EOF
chmod +x test-pkg/bin/hello
./nix-store --add-recursively test-pkg test-pkg
echo "Added directory"

# Query and verify
STORE_PATH=$(find /data/nix/store -name "*-test-pkg" -type d)
./nix-store --verify $STORE_PATH
echo "Verified: $STORE_PATH"

# Run garbage collection
./nix-store --gc
echo "Garbage collection completed"

# Check if paths still exist
if [ -d "$STORE_PATH" ]; then
    echo "Test package still exists after GC, as expected"
else
    echo "ERROR: Test package was removed by GC"
fi