#!/bin/sh

echo "1. Initialize store if not already done"
./nix-store --init

echo -e "\n2. Add required boot libraries to store"
./nix-store --add-boot-libs

echo -e "\n3. Compile sl with RPATH"
qcc -o sl.rpath sl.c -Wl,-rpath,'$ORIGIN/../lib'

echo -e "\n4. Add sl with dependencies"
./nix-store --add-with-deps sl.rpath sl-program

echo -e "\n5. Create default profile"
./nix-store --create-profile default

echo -e "\n6. Install sl to profile"
STORE_PATH=$(find /data/nix/store -name "*-sl-program" -type d)
./nix-store --install "$STORE_PATH" default

echo -e "\n7. Verify RPATH settings"
use -l /data/nix/profiles/default/bin/sl

echo -e "\n8. Test library resolution"
ldd /data/nix/profiles/default/bin/sl