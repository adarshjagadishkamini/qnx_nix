#!/bin/sh
# This script runs a series of Valgrind tests on the Nix store and profiles.
# Create test directory
TEST_DIR="valgrind_tests"
mkdir -p $TEST_DIR

echo "Running Valgrind tests..."

# Test 1: Store initialization
echo "Test 1: Store initialization"
valgrind --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --verbose \
    --log-file="$TEST_DIR/test1_init.txt" \
    ./nix-store --init

# Test 2: Adding a package
echo "Test 2: Adding a package"
valgrind --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --verbose \
    --log-file="$TEST_DIR/test2_add.txt" \
    ./nix-store --add /bin/ls test-ls

# Test 3: Profile creation
echo "Test 3: Profile creation"
valgrind --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --verbose \
    --log-file="$TEST_DIR/test3_profile.txt" \
    ./nix-store --create-profile test1

# Test 4: Package installation
echo "Test 4: Package installation"
valgrind --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --verbose \
    --log-file="$TEST_DIR/test4_install.txt" \
    ./nix-store --install /data/nix/store/*-test-ls test1

# Test 5: GC collection
echo "Test 5: Garbage collection"
valgrind --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --verbose \
    --log-file="$TEST_DIR/test5_gc.txt" \
    ./nix-store --gc

# Test 6: Shell environment
echo "Test 6: Shell environment"
valgrind --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --verbose \
    --log-file="$TEST_DIR/test6_shell.txt" \
    ./nix-shell-qnx test1 -c "exit"

echo "Tests complete. Check the $TEST_DIR directory for results."

# Analyze results
echo "\nAnalyzing results..."
for file in $TEST_DIR/*.txt; do
    echo "\nResults from $file:"
    grep "definitely lost" "$file"
    grep "indirectly lost" "$file"
    grep "possibly lost" "$file"
    grep "still reachable" "$file"
done
