#!/bin/sh
rm -rf /data/nix/

# Ensure clean start
echo "1. Initializing store..."
./nix-store --init

echo "2. Adding boot libraries..."
./nix-store --add-boot-libs

echo "3. Adding sl to store..."
./nix-store --add-with-deps sl sl

echo "4. Creating default profile..."
./nix-store --create-profile default

echo "5. Installing sl to profile..."
STORE_PATH=$(find /data/nix/store -name "*-sl" -type d)
./nix-store --install "$STORE_PATH" default

echo "6. Testing installation..."
/data/nix/profiles/default/bin/sl
