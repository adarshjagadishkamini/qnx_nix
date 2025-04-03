#!/bin/sh
# Clean up from previous tests
rm -rf /data/nix/store/*
rm -rf /data/nix/store/.nix-db

# Initialize
./nix-store --init
echo "Initialized store"

# Create a simple hello package
mkdir -p hello-pkg/bin
cat << 'EOF' > hello-pkg/bin/hello
#!/bin/sh
echo 'Hello from QNX Nix!'
EOF
chmod +x hello-pkg/bin/hello

# Add the hello package to the store
./nix-store --add-recursively hello-pkg hello-1.0

# Get the store path
HELLO_PATH=$(find /data/nix/store -name "*-hello-1.0" -type d)
echo "Hello package installed at: $HELLO_PATH"

# Verify it works
if [ -x "$HELLO_PATH/bin/hello" ]; then
    echo "Testing hello:"
    $HELLO_PATH/bin/hello
else
    echo "ERROR: hello executable not found or not executable"
fi

# Run garbage collection
./nix-store --gc

# Check if the hello package still exists
if [ -d "$HELLO_PATH" ]; then
    echo "Hello package still exists after GC, as expected"
else
    echo "ERROR: Hello package was removed by GC"
fi