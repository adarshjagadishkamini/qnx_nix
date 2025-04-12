#!/bin/sh

# Test QNX-specific functionality
echo "Testing QNX Nix Store Implementation"
echo "==================================="

# 1. Set up test directory
TEST_DIR="."
rm -rf "$TEST_DIR" /data/nix
mkdir -p "$TEST_DIR"

# 2. Initialize store with QNX permissions
./nix-store --init
chmod -R a+w /data/nix  # Temporary for testing

# 3. Test boot library detection
echo "\nTesting boot library detection..."
./nix-store --add-boot-libs

# 4. Create test program linking to QNX libraries
cat > "$TEST_DIR/test.c" << 'EOF'
#include <stdio.h>
#include <process.h>  // QNX-specific header
int main() {
    printf("QNX Nix Test\n");
    return 0;
}
EOF

# 5. Compile with QNX compiler
qcc -o "$TEST_DIR/test" "$TEST_DIR/test.c"

# 6. Check initial dependencies
echo "\nInitial dependencies:"
ldd "$TEST_DIR/test"

# 7. Add to store and check RPATH
./nix-store --add-with-deps "$TEST_DIR/test" qnx-test
STORE_PATH=$(find /data/nix/store -name "*-qnx-test" -type d)

echo "\nStore path RPATH:"
readelf -d "$STORE_PATH/bin/test" | grep -E "RPATH|RUNPATH"

# 8. Test profile installation
./nix-store --create-profile qnx-test
./nix-store --install "$STORE_PATH" qnx-test

echo "\nFinal binary RPATH:"
readelf -d /data/nix/profiles/qnx-test/bin/test | grep -E "RPATH|RUNPATH"

# Cleanup
rm -rf "$TEST_DIR"
