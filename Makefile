# Rebuild Build System - Root Makefile
#
# This Makefile provides a simple interface to the bootstrap build system.
# The actual build logic is in bootstrap/Makefile.
#
# Usage:
#   make          - Build rebuild using bootstrap
#   make clean    - Clean build artifacts
#   make help     - Show help message

.PHONY: all clean vendor-clean help

# Default target - run bootstrap build
all:
	@echo "=== Rebuilding using bootstrap build system ==="
	$(MAKE) -C bootstrap
	@if [ -f bootstrap/rebuild ]; then \
		cp bootstrap/rebuild ./rebuild; \
		echo ""; \
		echo "=== Build complete ==="; \
		echo "Binary: ./rebuild"; \
		echo ""; \
		echo "Try: ./rebuild --help"; \
	fi

# Clean bootstrap build artifacts
clean:
	$(MAKE) -C bootstrap clean
	rm -f ./rebuild

# Clean everything including vendored libraries
vendor-clean:
	$(MAKE) -C bootstrap vendor-clean
	rm -f ./rebuild

# Show help
help:
	@echo "Rebuild Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all (default)  - Build rebuild using bootstrap"
	@echo "  clean          - Remove build artifacts"
	@echo "  vendor-clean   - Remove build artifacts and clean vendored libraries"
	@echo "  help           - Show this message"
	@echo ""
	@echo "The bootstrap build is in bootstrap/"
	@echo "  make -C bootstrap       - Build directly"
	@echo "  bootstrap/build.sh      - Shell script wrapper"
	@echo ""
	@echo "After building:"
	@echo "  ./rebuild --help        - Show rebuild help"
	@echo "  ./rebuild <target>      - Build a target"
