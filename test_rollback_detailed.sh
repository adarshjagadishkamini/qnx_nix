#!/bin/sh

# Clean start
#echo "1. Cleaning up everything..."
rm -rf /data/nix
./nix-store --init
./nix-store --add-boot-libs

# Create working version v1
#echo "2. Creating working version v1..."
cp sl sl.v1
./nix-store --add-with-deps sl.v1 sl-test
./nix-store --query-references /data/nix/store/*-sl-test
STORE_PATH_V1=$(find /data/nix/store -name "*-sl-test" -type d)
./nix-store --create-profile test
./nix-store --install "$STORE_PATH_V1" test

#echo "3. Testing version 1..."
/data/nix/profiles/test/bin/sl.v1 >> log.txt

# Wait a bit to ensure different timestamps
sleep 2

# Create broken version v2
#echo "4. Creating intentionally broken version v2..."
echo '#!/bin/sh' > sl.v2
echo 'echo "This version is broken!"' >> slv2
chmod +x slv2
./nix-store --add-with-deps slv2 sl-test
STORE_PATH_V2=$(find /data/nix/store -name "*-sl-test" -type d | grep -v "$STORE_PATH_V1")
./nix-store --install "$STORE_PATH_V2" test

#echo "5. Testing broken version..."
/data/nix/profiles/test/bin/slv2

echo "6. Checking available generations..."
./nix-store --list-generations test

echo "7. Rolling back to previous version..."
./nix-store --rollback test

echo "8. Testing after rollback (should be working version)..."
/data/nix/profiles/test/bin/sl

# Cleanup
rm -f sl.v1 sl.v2

echo "Rollback test complete!"
