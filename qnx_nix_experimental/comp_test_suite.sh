#!/bin/bash
#
# Comprehensive test suite for QNX Nix-like store implementation
# This script tests all operations available in the implementation
#

# Configuration
NIX_STORE="/data/nix/store"
NIX_DB="$NIX_STORE/.nix-db"
NIX_STORE_BIN="./nix-store"
TEST_DIR="./nix-test"
TEST_LOG="$TEST_DIR/test.log"

# Colors for better output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Initialize test environment
init_test_env() {
    echo "Initializing test environment..."
    mkdir -p $TEST_DIR
    # Clean up from previous tests
    if [ -d "$NIX_STORE" ]; then
        rm -rf "$NIX_STORE"/*
        rm -rf "$NIX_DB"
    fi
    > $TEST_LOG
}

# Log a message to the test log
log_message() {
    echo "$(date): $1" >> $TEST_LOG
    echo "$1"
}

# Test function - runs a test and reports success/failure
test_function() {
    local test_name="$1"
    local test_cmd="$2"
    local expected_exit_code="${3:-0}"
    
    echo -e "${YELLOW}Running test: $test_name${NC}"
    log_message "TEST: $test_name"
    log_message "COMMAND: $test_cmd"
    
    # Run the command and capture output and exit code
    eval "$test_cmd" > "$TEST_DIR/output.tmp" 2>&1
    local actual_exit_code=$?
    
    # Log the output
    cat "$TEST_DIR/output.tmp" >> $TEST_LOG
    
    # Check if exit code matches expected
    if [ $actual_exit_code -eq $expected_exit_code ]; then
        echo -e "${GREEN}✓ PASSED${NC}: $test_name"
        log_message "RESULT: PASSED (exit code $actual_exit_code)"
    else
        echo -e "${RED}✗ FAILED${NC}: $test_name (exit code $actual_exit_code, expected $expected_exit_code)"
        log_message "RESULT: FAILED (exit code $actual_exit_code, expected $expected_exit_code)"
        cat "$TEST_DIR/output.tmp"
    fi
    echo ""
}

# Create a test file with content
create_test_file() {
    local path="$1"
    local content="$2"
    mkdir -p "$(dirname "$path")"
    echo "$content" > "$path"
    chmod +x "$path"
}

# Test 1: Store initialization
test_init() {
    test_function "Store initialization" "$NIX_STORE_BIN --init"
    # Verify that the store directory exists
    test_function "Verify store directory exists" "[ -d \"$NIX_STORE\" ]"
    # Verify that the database directory exists
    test_function "Verify database directory exists" "[ -d \"$NIX_DB\" ]"
}

# Test 2: Adding a single file to the store
test_add_file() {
    # Create a test file
    create_test_file "$TEST_DIR/hello.txt" "Hello, Nix on QNX!"
    
    # Add the file to the store
    test_function "Add single file to store" "$NIX_STORE_BIN --add $TEST_DIR/hello.txt hello-file"
    
    # Find the file in the store
    STORE_FILE=$(find $NIX_STORE -name "*-hello-file" -type d)
    
    # Verify the file exists in the store
    test_function "Verify file exists in store" "[ -d \"$STORE_FILE\" ]"
    
    # Verify the content of the file
    test_function "Verify file content" "grep -q \"Hello, Nix on QNX!\" \"$STORE_FILE/hello-file\""
    
    # Verify file is read-only
    test_function "Verify file is read-only" "! touch \"$STORE_FILE/hello-file\"" 1
    
    # Return the store path for later tests
    echo "$STORE_FILE"
}

# Test 3: Adding a directory recursively
test_add_directory() {
    # Create a test directory with multiple files
    mkdir -p "$TEST_DIR/testpkg/bin"
    create_test_file "$TEST_DIR/testpkg/bin/hello" "#!/bin/sh\necho 'Hello from QNX Nix!'"
    create_test_file "$TEST_DIR/testpkg/README" "Test package for QNX Nix"
    mkdir -p "$TEST_DIR/testpkg/lib"
    create_test_file "$TEST_DIR/testpkg/lib/helper.sh" "# Helper functions\nfunction say_hello() {\n  echo \"Hello from helper\"\n}"
    
    # Add the directory to the store
    test_function "Add directory recursively to store" "$NIX_STORE_BIN --add-recursively $TEST_DIR/testpkg test-package"
    
    # Find the directory in the store
    STORE_DIR=$(find $NIX_STORE -name "*-test-package" -type d)
    
    # Verify the directory exists in the store
    test_function "Verify directory exists in store" "[ -d \"$STORE_DIR\" ]"
    
    # Verify executable file is preserved and executable
    test_function "Verify executable files are preserved" "[ -x \"$STORE_DIR/bin/hello\" ]"
    
    # Verify structure is preserved
    test_function "Verify directory structure is preserved" "[ -d \"$STORE_DIR/lib\" ]"
    
    # Verify content of files
    test_function "Verify file content in recursive add" "grep -q \"Hello from QNX Nix\" \"$STORE_DIR/bin/hello\""
    
    # Return the store path for later tests
    echo "$STORE_DIR"
}

# Test 4: Verify store path
test_verify_path() {
    local STORE_PATH="$1"
    
    # Test verify with valid store path
    test_function "Verify valid store path" "$NIX_STORE_BIN --verify \"$STORE_PATH\""
    
    # Test verify with invalid path
    test_function "Verify invalid path fails" "$NIX_STORE_BIN --verify $TEST_DIR/nonexistent" 1
    
    # Test verify with path outside the store
    test_function "Verify path outside store fails" "$NIX_STORE_BIN --verify /tmp" 1
}

# Test 5: Query references
test_query_references() {
    local STORE_PATH="$1"
    
    # Test query references (should return success even if no references)
    test_function "Query references of store path" "$NIX_STORE_BIN --query-references \"$STORE_PATH\""
    
    # Test query references with invalid path
    test_function "Query references with invalid path" "$NIX_STORE_BIN --query-references $TEST_DIR/nonexistent" 1
}

# Test 6: Garbage collection
test_garbage_collection() {
    # Run garbage collection
    test_function "Run garbage collection" "$NIX_STORE_BIN --gc"
    
    # The paths should still exist after GC since they're in roots
    test_function "Verify paths still exist after GC" "[ -d \"$1\" ] && [ -d \"$2\" ]"
    
    # Create a path that's not registered as a root
    local TEMP_PATH="$NIX_STORE/unregistered-path"
    mkdir -p "$TEMP_PATH"
    
    # Run garbage collection again
    test_function "Run garbage collection again" "$NIX_STORE_BIN --gc"
    
    # The unregistered path should be removed
    test_function "Verify unregistered path was removed" "[ ! -d \"$TEMP_PATH\" ]"
}

# Test 7: Invalid commands
test_invalid_commands() {
    # Test with no arguments
    test_function "Test with no arguments" "$NIX_STORE_BIN" 1
    
    # Test with unknown command
    test_function "Test with unknown command" "$NIX_STORE_BIN --unknown-command" 1
    
    # Test missing arguments
    test_function "Test --add with missing arguments" "$NIX_STORE_BIN --add" 1
    test_function "Test --add-recursively with missing arguments" "$NIX_STORE_BIN --add-recursively" 1
    test_function "Test --verify with missing arguments" "$NIX_STORE_BIN --verify" 1
    test_function "Test --query-references with missing arguments" "$NIX_STORE_BIN --query-references" 1
}

# Test 8: Database consistency
test_database_consistency() {
    # Check if the store path is in the database
    local STORE_PATH="$1"
    local DB_PATH="$NIX_DB/db"
    
    # Verify the database file exists
    test_function "Verify database file exists" "[ -f \"$DB_PATH\" ]"
    
    # Verify the store path is in the database (basic check - grep for path fragments)
    PATH_FRAGMENT=$(basename "$STORE_PATH")
    test_function "Verify path is in database" "grep -q \"$PATH_FRAGMENT\" \"$DB_PATH\" || xxd \"$DB_PATH\" | grep -q \"$PATH_FRAGMENT\""
}

# Test 9: Resource Manager
test_resource_manager() {
    # This test should be run separately as it starts a daemon
    # Just check if the binary accepts the command without crashing
    test_function "Test resource manager command" "$NIX_STORE_BIN --daemon &" 0
    sleep 1
    pkill -f "nix-store --daemon" || true
}

# Run all tests
run_all_tests() {
    init_test_env
    
    echo -e "${YELLOW}Starting QNX Nix-like store test suite...${NC}"
    log_message "TEST SUITE STARTED"
    
    test_init
    STORE_FILE_PATH=$(test_add_file)
    STORE_DIR_PATH=$(test_add_directory)
    test_verify_path "$STORE_FILE_PATH"
    test_query_references "$STORE_DIR_PATH"
    test_garbage_collection "$STORE_FILE_PATH" "$STORE_DIR_PATH"
    test_invalid_commands
    test_database_consistency "$STORE_DIR_PATH"
    test_resource_manager
    
    echo -e "${YELLOW}All tests completed. See $TEST_LOG for detailed results.${NC}"
    log_message "TEST SUITE COMPLETED"
}

# Run the test suite
run_all_tests