#!/bin/sh
# Script to properly install sl with dependencies

echo "1. Initialize store if not already done"
./nix-store --init

echo -e "\n2. Add required boot libraries to store"
./nix-store --add-boot-libs

echo -e "\n3. Now add sl with dependencies"
./nix-store --add-with-deps sl sl-program

echo -e "\n4. Create default profile"
./nix-store --create-profile default

echo -e "\n5. Install sl to profile"
STORE_PATH=$(find /data/nix/store -name "*-sl-program" -type d)
./nix-store --install "$STORE_PATH" default

echo -e "\n6. Test installation"
echo "You can now run: /data/nix/profiles/default/bin/sl"
echo "Or add to PATH: export PATH=/data/nix/profiles/default/bin:\$PATH"
