# Rebuild Build System - Bootstrap Makefile
#
# This Makefile bootstraps the rebuild build system by:
# 1. Building vendored dependencies (libuv, umka, blake2)
# 2. Compiling rebuild's C source files
# 3. Linking everything into the ./rebuild binary
#
# Usage:
#   make          - Build rebuild binary (same as 'make rebuild')
#   make bootstrap - Alias for 'make all'
#   make clean    - Remove build artifacts
#   make vendor-clean - Also clean vendored libraries

# ============================================================================
# Configuration
# ============================================================================

# Compiler and flags
CC = cc
CFLAGS = -std=c11 -O2 -Wall -Wextra -g

# Project directories (relative to repository root)
SRC_DIR = ../src
VENDOR_DIR = ../vendor
BUILD_DIR = build

# Vendored library paths
LIBUV_DIR = $(VENDOR_DIR)/libuv
UMKA_DIR = $(VENDOR_DIR)/umka
BLAKE2_DIR = $(VENDOR_DIR)/blake2

# Library files
LIBUV_LIB = $(LIBUV_DIR)/.libs/libuv.a
UMKA_LIB = $(UMKA_DIR)/build/libumka.a
BLAKE2_OBJ = $(BUILD_DIR)/blake2b-ref.o

# Include paths
INCLUDES = -I$(SRC_DIR) \
           -I$(LIBUV_DIR)/include \
           -I$(UMKA_DIR)/src \
           -I$(BLAKE2_DIR)

# Link flags (order matters: -lpthread must come after object files on some systems)
LDFLAGS = -lm -ldl -lpthread

# Source files - all .c files in src/
SRC_FILES = $(wildcard $(SRC_DIR)/*.c)

# Object files (placed in build directory)
OBJ_FILES = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC_FILES))

# Output binary (goes to ../build/ directory, not bootstrap/)
REBUILD_BIN = ../build/rebuild

# ============================================================================
# Main Targets
# ============================================================================

.PHONY: all rebuild bootstrap clean vendor-clean help

# Default target
all: $(REBUILD_BIN)

# Alias for all
rebuild: all

# Alias for all
bootstrap: all

# Help target
help:
	@echo "Rebuild Build System - Bootstrap Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all/rebuild/bootstrap  - Build the rebuild binary (default)"
	@echo "  clean                  - Remove build artifacts"
	@echo "  vendor-clean          - Remove build artifacts and clean vendored libraries"
	@echo "  help                   - Show this help message"
	@echo ""
	@echo "The build process:"
	@echo "  1. Build vendored libuv (autogen, configure, make)"
	@echo "  2. Build vendored UMKA (make static)"
	@echo "  3. Compile BLAKE2b"
	@echo "  4. Compile all rebuild .c files in src/"
	@echo "  5. Link everything into ./rebuild binary"

# ============================================================================
# Rebuild Binary
# ============================================================================

$(REBUILD_BIN): $(BUILD_DIR) $(LIBUV_LIB) $(UMKA_LIB) $(BLAKE2_OBJ) $(OBJ_FILES)
	@echo "Linking $@..."
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $(OBJ_FILES) $(BLAKE2_OBJ) $(UMKA_LIB) $(LIBUV_LIB) $(LDFLAGS)
	@echo "Build complete: $@"

# ============================================================================
# Vendored Dependencies
# ============================================================================

# libuv - Async I/O library
$(LIBUV_LIB):
	@echo "Building libuv..."
	@if [ ! -f $(LIBUV_DIR)/configure ]; then \
		echo "Running autogen.sh for libuv..."; \
		cd $(LIBUV_DIR) && ./autogen.sh; \
	fi
	@if [ ! -f $(LIBUV_DIR)/Makefile ]; then \
		echo "Configuring libuv..."; \
		cd $(LIBUV_DIR) && ./configure --enable-static --disable-shared; \
	fi
	@echo "Compiling libuv..."
	cd $(LIBUV_DIR) && $(MAKE)
	@echo "libuv build complete"

# UMKA - Embedded scripting language
$(UMKA_LIB):
	@echo "Building UMKA..."
	cd $(UMKA_DIR) && $(MAKE) static
	@echo "UMKA build complete"

# BLAKE2b - Hash function (using reference implementation)
$(BLAKE2_OBJ): $(BLAKE2_DIR)/blake2b-ref.c | $(BUILD_DIR)
	@echo "Compiling BLAKE2b..."
	$(CC) $(CFLAGS) -I$(BLAKE2_DIR) -c $< -o $@

# ============================================================================
# Rebuild Source Files
# ============================================================================

# Create build directory
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# ============================================================================
# Clean Targets
# ============================================================================

clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)
	rm -f $(REBUILD_BIN)
	rm -f $(BLAKE2_OBJ)
	@echo "Clean complete"

vendor-clean: clean
	@echo "Cleaning vendored libraries..."
	@if [ -f $(LIBUV_DIR)/Makefile ]; then \
		cd $(LIBUV_DIR) && $(MAKE) distclean || $(MAKE) clean || true; \
	fi
	@if [ -f $(UMKA_DIR)/Makefile ]; then \
		cd $(UMKA_DIR) && $(MAKE) clean || true; \
	fi
	rm -f $(LIBUV_DIR)/configure $(LIBUV_DIR)/Makefile
	@echo "Vendor clean complete"

# ============================================================================
# Dependencies
# ============================================================================

# Automatically generate header dependencies for source files
# This ensures that changes to header files trigger recompilation
-include $(OBJ_FILES:.o=.d)

# Generate dependency files alongside object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

# ============================================================================
# Special Rules
# ============================================================================

# Prevent make from deleting intermediate files
.PRECIOUS: $(BUILD_DIR)/%.o

# Tell make these targets don't represent files
.PHONY: all rebuild bootstrap clean vendor-clean help
