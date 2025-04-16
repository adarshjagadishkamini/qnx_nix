#!/bin/sh
set -e  # Exit on error

echo "=== Full System Test ==="

# Clean start
echo "1. Initializing clean environment..."
rm -rf /data/nix
./nix-store --init

# Add boot libraries
echo "2. Adding boot libraries..."
./nix-store --add-boot-libs >/dev/null

# Add multiple test packages
echo "3. Adding test packages..."
./nix-store --add-with-deps /system/bin/head head >/dev/null
./nix-store --add-with-deps /system/bin/tail tail >/dev/null
./nix-store --add-with-deps /proc/boot/ls ls >/dev/null
./nix-store --add-with-deps sl sl
HEAD_PATH=$(find /data/nix/store -name "*-head" -type d)
TAIL_PATH=$(find /data/nix/store -name "*-tail" -type d)
LS_PATH=$(find /data/nix/store -name "*-ls" -type d)
SL_PATH=$(find /data/nix/store -name "*-sl" -type d)

# Test profile creation and package installation
echo "4. Testing profile management..."
echo "4.1 Creating profiles..."
./nix-store --create-profile test1
./nix-store --create-profile test2
./nix-store --create-profile test3

echo "4.2 Installing packages to profiles..."
# Profile 1: head only
./nix-store --install $HEAD_PATH test1

# Profile 2: tail only
./nix-store --install $TAIL_PATH test2

# Profile 3: multiple packages
./nix-store --install $HEAD_PATH test3
./nix-store --install $TAIL_PATH test3
./nix-store --install $LS_PATH test2
./nix-store --install $SL_PATH test1

echo "4.3 Verifying profile contents..."
ls -l /data/nix/profiles/test1/bin/
ls -l /data/nix/profiles/test2/bin/
ls -l /data/nix/profiles/test3/bin/

# Test generation management
echo "5. Testing generations..."
echo "5.1 Creating new generations..."
# Modify profile 3
./nix-store --install $HEAD_PATH test3  # Should create new generation
./nix-store --list-generations test3

#echo "5.2 Testing generation switching..."
#FIRST_GEN=$(./nix-store --list-generations test3 | head -n1 | awk '{print $1}')
#./nix-store --switch-generation test3 $FIRST_GEN
#ls -l /data/nix/profiles/test3/bin/

echo "5.3 Testing rollback..."
./nix-store --rollback test3
ls -l /data/nix/profiles/test3/bin/

# Test garbage collection
echo "6. Testing garbage collection..."
echo "6.1 Current store state:"
ls -l /data/nix/store/

echo "6.2 Creating temporary package..."
./nix-store --add-with-deps /system/bin/cat cat
CAT_PATH=$(find /data/nix/store -name "*-cat" -type d)

echo "6.3 Running garbage collection..."
./nix-store --gc
echo "Store after GC:"
ls -l /data/nix/store/

# Test profile switching
echo "7. Testing profile switching..."
./nix-store --list-profiles
./nix-store --switch-profile test1
ls -l /data/nix/profiles/current/bin/
./nix-store --switch-profile test3
ls -l /data/nix/profiles/current/bin/

# Verify functionality
echo "8. Testing package functionality..."
echo "Test line" > testfile
/data/nix/profiles/test1/bin/head testfile
/data/nix/profiles/test2/bin/tail testfile
/data/nix/profiles/test3/bin/ls -l testfile

echo "=== Test Complete ==="
