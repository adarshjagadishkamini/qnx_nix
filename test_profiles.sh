#!/bin/sh

# Test script for QNX Nix profile functionality
NIX_STORE="./nix-store"
TEST_DIR="/tmp/nix-test"
EXIT_CODE=0

# Helper function for test reporting
test_case() {
    printf "Testing %s... " "$1"
}

test_result() {
    if [ $1 -eq 0 ]; then
        echo "OK"
    else
        echo "FAILED"
        EXIT_CODE=1
    fi
}

# Clean up from previous tests
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"

# Create test files
cat > "$TEST_DIR/hello.c" << 'EOF'
#include <stdio.h>
int main() { printf("Hello from QNX Nix!\n"); return 0; }
EOF

# Compile test program
qcc -o "$TEST_DIR/hello" "$TEST_DIR/hello.c"

# Initialize store
test_case "store initialization"
$NIX_STORE --init
test_result $?

# Add program to store
test_case "adding program to store"
$NIX_STORE --add "$TEST_DIR/hello" hello-test
test_result $?

# Get the store path
STORE_PATH=$(find /data/nix/store -name "*-hello-test" -type d)

# Create test profile
test_case "creating test profile"
$NIX_STORE --create-profile test-profile
test_result $?

# Install program to profile
test_case "installing to profile"
$NIX_STORE --install "$STORE_PATH" test-profile
test_result $?

# Verify profile structure
test_case "profile structure"
[ -d "/data/nix/profiles/test-profile/bin" ] && \
[ -x "/data/nix/profiles/test-profile/bin/hello" ]
test_result $?

# Test program execution
test_case "program execution from profile"
OUTPUT=$(/data/nix/profiles/test-profile/bin/hello)
[ "$OUTPUT" = "Hello from QNX Nix!" ]
test_result $?

# Switch profile
test_case "profile switching"
$NIX_STORE --switch-profile test-profile
test_result $?

# Verify current profile link
test_case "current profile link"
[ "$(readlink /data/nix/profiles/current)" = "/data/nix/profiles/test-profile" ]
test_result $?

# List profiles
test_case "listing profiles"
$NIX_STORE --list-profiles | grep -q "test-profile"
test_result $?

# Clean up
rm -rf "$TEST_DIR"

echo "Test suite completed with exit code $EXIT_CODE"
exit $EXIT_CODE
