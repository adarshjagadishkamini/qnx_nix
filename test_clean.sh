#!/bin/sh

echo "1. Cleaning up everything..."
rm -rf /data/nix
rm -rf /tmp/nix-test

echo "2. Creating test directory..."
mkdir -p /tmp/nix-test

echo "3. Creating test program..."
cat > /tmp/nix-test/hello.c << 'EOF'
#include <stdio.h>
int main() {
    printf("Hello from QNX Nix!\n");
    return 0;
}
EOF

echo "4. Compiling test program..."
qcc -o /tmp/nix-test/hello /tmp/nix-test/hello.c

echo "5. Initialize fresh store..."
./nix-store --init

echo "6. Add boot libraries (needed for dependencies)..."
./nix-store --add-boot-libs

echo "7. Add program to store with dependencies..."
./nix-store --add-with-deps /tmp/nix-test/hello hello-test

echo "8. Show what's in the store..."
ls -l /data/nix/store/

echo "9. Create a new profile..."
./nix-store --create-profile test

echo "10. Get store path of our program..."
STORE_PATH=$(find /data/nix/store -name "*-hello-test" -type d)
echo "Store path: $STORE_PATH"

echo "11. Install to profile..."
./nix-store --install "$STORE_PATH" test

echo "12. Show profile structure..."
ls -lR /data/nix/profiles/test/

echo "13. Test running the program..."
/data/nix/profiles/test/bin/hello

echo "14. List current generations..."
./nix-store --list-generations test

echo "15. Create second version..."
echo '#!/bin/sh' > /tmp/nix-test/hello.v2
echo 'echo "Version 2!"' >> /tmp/nix-test/hello.v2
chmod +x /tmp/nix-test/hello.v2
./nix-store --add-with-deps /tmp/nix-test/hello.v2 hello-test
STORE_PATH_V2=$(find /data/nix/store -name "*-hello-test" -type d | grep -v "$STORE_PATH")
./nix-store --install "$STORE_PATH_V2" test

echo "16. Test second version..."
/data/nix/profiles/test/bin/hello.v2

echo "17. List generations after second install..."
./nix-store --list-generations test

echo "18. Rollback to first version..."
./nix-store --rollback test

echo "19. Test after rollback..."
/data/nix/profiles/test/bin/hello
