#!/bin/sh

# Clean environment
rm -rf /data/nix
./nix-store --init
./nix-store --add-boot-libs

echo "1. Examining original sl binary..."
readelf -d sl | grep -E "RPATH|RUNPATH|NEEDED"

echo "\n2. Add to store..."
./nix-store --add-with-deps sl sl.v1
STORE_PATH=$(find /data/nix/store -name "*-sl.v1" -type d)

echo "\n3. Examining sl in store..."
readelf -d $STORE_PATH/bin/sl | grep -E "RPATH|RUNPATH|NEEDED"

echo "\n4. Checking dynamic section details..."
readelf -Wd $STORE_PATH/bin/sl

echo "\n5. Library dependencies..."
readelf -d $STORE_PATH/bin/sl | grep NEEDED

echo "\n6. Dynamic linker/interpreter..."
readelf -l $STORE_PATH/bin/sl | grep "interpreter"

echo "\n7. Symbol versioning..."
readelf -V $STORE_PATH/bin/sl

echo "\n8. Create and install to profile..."
./nix-store --create-profile test
./nix-store --install $STORE_PATH test

echo "\n9. Final binary examination..."
readelf -d /data/nix/profiles/test/bin/sl | grep -E "RPATH|RUNPATH|NEEDED"

echo "\n10. Runtime library resolution..."
ldd /data/nix/profiles/test/bin/sl

echo "\n11. Testing execution..."
/data/nix/profiles/test/bin/sl
