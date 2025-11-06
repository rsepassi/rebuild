#!/bin/sh
# Rebuild Build System - Bootstrap Build Script
#
# This script builds the rebuild binary using only Make and POSIX sh.
# It's a simple wrapper around the bootstrap Makefile.
#
# Usage:
#   ./bootstrap/build.sh         - Build rebuild
#   ./bootstrap/build.sh clean   - Clean build artifacts

set -e  # Exit on error

# Change to bootstrap directory
cd "$(dirname "$0")"

# Check what command was requested
if [ "$1" = "clean" ]; then
    echo "=== Cleaning bootstrap build ==="
    make clean
    exit 0
elif [ "$1" = "vendor-clean" ]; then
    echo "=== Cleaning bootstrap build and vendored libraries ==="
    make vendor-clean
    exit 0
elif [ "$1" = "help" ] || [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    make help
    exit 0
elif [ -n "$1" ]; then
    echo "Unknown command: $1"
    echo "Usage: $0 [clean|vendor-clean|help]"
    exit 1
fi

# Build rebuild
echo "=== Building rebuild using bootstrap ==="
echo ""

# Run make
make

echo ""
echo "=== Build complete ==="
echo "Binary: bootstrap/rebuild"
echo ""

# Copy to root if successful
if [ -f rebuild ]; then
    cp rebuild ../rebuild
    echo "Copied to: ./rebuild"
    echo ""
    echo "Try: ./rebuild --help"
else
    echo "ERROR: Bootstrap build failed - rebuild binary not found"
    exit 1
fi
