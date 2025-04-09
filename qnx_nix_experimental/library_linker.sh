#!/bin/bash

# --- Configuration ---
NIX_STORE_CMD="./nix-store"       
NIX_STORE_BASE="/data/nix/store" 

# --- Option Parsing ---
VERIFY_MODE=0
# Check if the first argument is the verify flag
if [[ "$1" == "--verify" || "$1" == "--ldd" ]]; then
  VERIFY_MODE=1
  shift # Remove the --verify flag from the arguments list
fi

# --- Input ---
if [ -z "$1" ]; then
  # Updated usage message
  echo "Usage: $0 [--verify|--ldd] /data/nix/store/<hash>-<package-name> [arguments...]"
  exit 1
fi

# This variable holds the package *directory* path
PACKAGE_STORE_PATH="$1"
# If we are *not* in verify mode, remove the package path from arguments
# Otherwise (in verify mode), keep it, as there are no further arguments for ldd
if [ $VERIFY_MODE -eq 0 ]; then
    shift
fi

# --- Determine Dependency Library Directories ---
# (This part remains the same - query references and build LIB_DIRS)
echo "Querying references for ${PACKAGE_STORE_PATH}..."
RAW_OUTPUT=$("${NIX_STORE_CMD}" --query-references "${PACKAGE_STORE_PATH}" 2>&1)
LIB_DIRS=$(echo "${RAW_OUTPUT}" | grep "^[[:space:]]*${NIX_STORE_BASE}" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//' | sort -u)

if [ -z "$LIB_DIRS" ]; then
  echo "Warning: Could not extract any library directory paths from the references output."
  # Handle potential missing raw output if grep finds nothing
  if [ -z "$RAW_OUTPUT" ]; then
      echo "(No output received from --query-references)"
  else
      echo "Raw output was:"
      echo "${RAW_OUTPUT}"
  fi
  LD_LIBRARY_PATH_DIRS=""
else
  LD_LIBRARY_PATH_DIRS=$(echo "${LIB_DIRS}" | paste -sd ':')
fi

# --- Dynamically Find Executable ---
# (This part remains the same - find the executable path)
PACKAGE_DIR_NAME=$(basename "${PACKAGE_STORE_PATH}")
PACKAGE_NAME=$(echo "${PACKAGE_DIR_NAME}" | sed 's/^[0-9a-f]*-//')
EXECUTABLE_PATH=""

if [ -x "${PACKAGE_STORE_PATH}/bin/${PACKAGE_NAME}" ]; then
    EXECUTABLE_PATH="${PACKAGE_STORE_PATH}/bin/${PACKAGE_NAME}"
elif [ -x "${PACKAGE_STORE_PATH}/${PACKAGE_NAME}" ]; then
    EXECUTABLE_PATH="${PACKAGE_STORE_PATH}/${PACKAGE_NAME}"
else
    FOUND_EXEC=$(find "${PACKAGE_STORE_PATH}/bin" "${PACKAGE_STORE_PATH}" -maxdepth 1 -type f -executable -print -quit)
    if [ -n "$FOUND_EXEC" ]; then
        EXECUTABLE_PATH="$FOUND_EXEC"
    fi
fi

if [ -z "$EXECUTABLE_PATH" ]; then
    echo "Error: Could not automatically find an executable inside '${PACKAGE_STORE_PATH}'."
    exit 1
fi

# --- Action: Verify or Execute ---
if [ $VERIFY_MODE -eq 1 ]; then
    # --- Verify Mode ---
    echo "--- Verifying library linkage for ${EXECUTABLE_PATH} with Nix-only LD_LIBRARY_PATH ---"
    # Check if LD_LIBRARY_PATH_DIRS is empty and provide a clearer message/action
    if [ -z "$LD_LIBRARY_PATH_DIRS" ]; then
        echo "Warning: No Nix dependency library paths found. Running ldd with potentially empty LD_LIBRARY_PATH."
        # Optionally run with empty LD_LIBRARY_PATH or default system one:
        # LD_LIBRARY_PATH="" ldd "${EXECUTABLE_PATH}"
        # Or just ldd "${EXECUTABLE_PATH}"
        ldd "${EXECUTABLE_PATH}"
    else
        echo "Using LD_LIBRARY_PATH=${LD_LIBRARY_PATH_DIRS}"
        # Set the isolated LD_LIBRARY_PATH *only for this ldd command* using 'env'
        env LD_LIBRARY_PATH="${LD_LIBRARY_PATH_DIRS}" ldd "${EXECUTABLE_PATH}"
    fi
    exit $? # Exit with the status code of ldd
else
    # --- Execute Mode ---
    # Set LD_LIBRARY_PATH for ISOLATION for the subsequent exec command
    export LD_LIBRARY_PATH="${LD_LIBRARY_PATH_DIRS}"

    echo "--- Running with modified LD_LIBRARY_PATH ---"
    echo "LD_LIBRARY_PATH=${LD_LIBRARY_PATH}" # Will now only contain Nix paths
    echo "Executing: ${EXECUTABLE_PATH} $@"   # Shows the dynamically found executable
    echo "-----------------------------------------"

    # Execute the Program
    exec "${EXECUTABLE_PATH}" "$@"
    # exec replaces the script, so we won't reach here unless exec fails
    exit 127 # Exit code if exec fails
fi