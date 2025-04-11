#!/bin/sh

# Test rollback functionality
echo "Setting up test environment..."
./nix-store --init
./nix-store --add-boot-libs

# Install sl version 1
echo "Installing sl version 1..."
./nix-store --add-with-deps sl sl-program-v1
./nix-store --create-profile test
STORE_PATH=$(find /data/nix/store -name "*-sl-program-v1" -type d)
./nix-store --install "$STORE_PATH" test

sleep 2  # Ensure different timestamps

# Install sl version 2 (could be a different version)
echo "Installing sl version 2..."
./nix-store --add-with-deps sl sl-program-v2
STORE_PATH=$(find /data/nix/store -name "*-sl-program-v2" -type d)
./nix-store --install "$STORE_PATH" test

echo "Listing generations..."
./nix-store --list-generations test

echo "Testing rollback..."
./nix-store --rollback test

echo "Verify we're back to version 1..."
ls -l /data/nix/profiles/test/bin/sl
