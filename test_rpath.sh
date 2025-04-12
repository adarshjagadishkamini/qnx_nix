#!/bin/sh

echo "RPATH Testing Script"
echo "==================="

# Setup test environment
TEST_DIR="/tmp/nix-rpath-test"
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"

# 1. Clean start
echo "\n1. Cleaning up everything..."
rm -rf /data/nix
./nix-store --init

# 2. Create test program that uses shared libraries
echo "\n2. Creating test program..."
cat > "$TEST_DIR/test.c" << 'EOF'
#include <stdio.h>
#include <ncurses.h>
int main() {
    printf("Testing library linkage...\n");
    initscr();  // ncurses function
    printw("If you see this, ncurses works!");
    refresh();
    getch();
    endwin();
    return 0;
}
EOF

# 3. Compile with shared libraries
echo "\n3. Compiling test program..."
qcc -o "$TEST_DIR/test" "$TEST_DIR/test.c" -lncurses

# 4. Add boot libraries to store
echo "\n4. Adding boot libraries..."
./nix-store --add-boot-libs

# 5. Check initial RPATH
echo "\n5. Checking initial RPATH..."
readelf -d "$TEST_DIR/test" | grep "RPATH\|RUNPATH"

# 6. Add program to store
echo "\n6. Adding program to store..."
./nix-store --add-with-deps "$TEST_DIR/test" test-program

# 7. Get store path
STORE_PATH=$(find /data/nix/store -name "*-test-program" -type d)
echo "\nStore path: $STORE_PATH"

# 8. Check RPATH after installation
echo "\n8. Checking RPATH after installation..."
readelf -d "$STORE_PATH/bin/test" | grep "RPATH\|RUNPATH"

# 9. Create and install to profile
echo "\n9. Creating profile..."
./nix-store --create-profile test
./nix-store --install "$STORE_PATH" test

# 10. Check final executable
echo "\n10. Checking profile executable..."
readelf -d /data/nix/profiles/test/bin/test | grep "RPATH\|RUNPATH"

# 11. Try running
echo "\n11. Running test program..."
echo "The program should show a ncurses screen."
echo "Press any key to continue when the program runs..."
/data/nix/profiles/test/bin/test

# 12. Check library dependencies
echo "\n12. Checking runtime library resolution..."
ldd /data/nix/profiles/test/bin/test

# Cleanup
rm -rf "$TEST_DIR"

echo "\nTest complete!"
