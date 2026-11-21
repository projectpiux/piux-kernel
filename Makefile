# ---------------------------------------------------------------------------
# Piux top-level Makefile
# ---------------------------------------------------------------------------
# Architecture-independent entry point. Everything that depends on the target
# CPU lives in arch/<name>/Makefile.mk; this file only selects the port:
#
#   make                 # build the ISO, modules and user programs
#   make ARCH=x86        # explicit port selection (default)
#   make ARCH=x86 clean  # clean that port's build tree
#
# Adding a port means adding arch/<name>/Makefile.mk next to its boot stub
# (arch/<name>/boot/) and linker script (arch/<name>/linker/).

ARCH ?= x86
BUILD_DIR ?= build
ARCH_DIR := arch/$(ARCH)
ARCH_MAKEFILE := $(ARCH_DIR)/Makefile.mk

ifeq ($(wildcard $(ARCH_MAKEFILE)),)
$(error Unknown ARCH '$(ARCH)': $(ARCH_MAKEFILE) not found. Available ports: $(patsubst arch/%/Makefile.mk,%,$(wildcard arch/*/Makefile.mk)))
endif

# Remember the last port used, so an ARCH-less `make clean` still targets the
# tree that was actually built.
ARCH_STAMP := $(BUILD_DIR)/.arch
$(shell mkdir -p $(BUILD_DIR); if [ -f $(ARCH_STAMP) ]; then :; else printf '%s\n' $(ARCH) > $(ARCH_STAMP); fi)

# The port makefile provides every target and the default goal.
include $(ARCH_MAKEFILE)
