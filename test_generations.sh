#!/bin/sh

# Clean start
rm -rf /data/nix/profiles/test* /data/nix/store/*-sl-program*
./nix-store --init
./nix-store --add-boot-libs

# Install working version (v1)
echo "Creating working version..."
cp sl sl.v1
./nix-store --add-with-deps sl.v1 sl-program-v1
STORE_PATH_V1=$(find /data/nix/store -name "*-sl-program-v1" -type d)
./nix-store --create-profile test
./nix-store --install "$STORE_PATH_V1" test

echo "Profile structure after v1:"
ls -la /data/nix/profiles/test*

# Install broken version (v2)
echo "Creating broken version..."
echo '#!/bin/sh' > sl.v2
echo 'echo "This is broken"' >> sl.v2
chmod +x sl.v2
./nix-store --add-with-deps sl.v2 sl-program-v2
STORE_PATH_V2=$(find /data/nix/store -name "*-sl-program-v2" -type d)
./nix-store --install "$STORE_PATH_V2" test

echo "Profile structure after v2 (should see backup):"
ls -la /data/nix/profiles/test*

echo "Testing broken version..."
/data/nix/profiles/test/bin/sl.v2

echo "Rolling back..."
./nix-store --rollback test

echo "Profile structure after rollback:"
ls -la /data/nix/profiles/test*

echo "Testing rolled back version..."
#/data/nix/profiles/test/bin/sl.v1

# Cleanup
rm -f sl.v1 sl.v2
