# ---------------------------------------------------------------------------
# Piux port: x86 (i386)
# ---------------------------------------------------------------------------
# Included by the top-level Makefile after ARCH has been resolved. Build
# output stays under build/<arch>/ so ports never share object trees.
#
# Port-specific pieces contributed by this directory:
#   boot/bootx.asm      Multiboot1 entry stub (_start) -> kernel_main
#   linker/kernel.ld    kernel linker script (loads at 1 MiB)
#   include/arch/       arch/arch.h description header (PIUX_ARCH_*)

# Same default goal as the original Makefile; the recursive module sub-builds
# below pass their own goal explicitly.
.DEFAULT_GOAL := all

BUILD_DIR ?= build
ARCH_NAME  := x86
ARCH_BUILD := $(BUILD_DIR)/$(ARCH_NAME)
# Port makefile path, used for the recursive module sub-builds below.
ARCH_MAKEFILE := $(lastword $(MAKEFILE_LIST))

AS       := nasm
CC       := gcc
LD       := ld
OBJCOPY  := objcopy

# Toolchain target. Keep these in sync with arch/$(ARCH_NAME)/include/arch/arch.h.
ARCH_ASFORMAT     := elf32
ARCH_LD_TARGET    := elf_i386
ARCH_OBJCOPY_TGT  := elf32-i386
ARCH_OBJCOPY_BFD  := i386
ARCH_CFLAGS       := -m32 -march=i386
ARCH_ASFLAGS      := -f elf32

# Absolute port directory, so recursive `make -f` sub-builds resolve it the same
# way whether they are started from the top-level Makefile or by hand.
ARCH_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

CFLAGS   := $(ARCH_CFLAGS) -std=gnu11 -ffreestanding -fno-pic -fno-pie \
            -fno-stack-protector -nostdlib -Wall -Wextra \
            -Werror=implicit-function-declaration -O2 \
            -fno-asynchronous-unwind-tables -fno-unwind-tables
ASFLAGS  := $(ARCH_ASFLAGS)
LDFLAGS  := -T $(ARCH_DIR)/linker/kernel.ld -m $(ARCH_LD_TARGET) -nostdlib

KERNEL    := $(ARCH_BUILD)/kernel.elf
ISO       := $(BUILD_DIR)/$(ARCH_NAME)/piux.iso
DISK      := $(BUILD_DIR)/ext2.img
CURSOR_RAW := $(BUILD_DIR)/cursor.rgba
CURSOR_OBJ := $(BUILD_DIR)/cursor.o

KERNEL_OBJS := $(patsubst kernel/%.c,$(ARCH_BUILD)/kernel/%.o,$(wildcard kernel/*/*.c))
# kernel/core/kernel_entry.asm è mantenuto solo come riferimento: l'entry point
# reale (_start) vive in arch/$(ARCH_NAME)/boot/bootx.asm e chiama kernel_main.
KERNEL_ASM_SOURCES := $(filter-out kernel/core/kernel_entry.asm,$(wildcard kernel/*/*.asm))
KERNEL_ASM_OBJS := $(patsubst kernel/%.asm,$(ARCH_BUILD)/kernel/%.o,$(KERNEL_ASM_SOURCES))
# Subsystems shipped as loadable modules (see kernel/module_abi.h).
ARCH_BOOT_OBJ := $(ARCH_BUILD)/boot/bootx.o
MODULES     := vfs commands net installer
MODULE_FLAGS := $(CFLAGS) -fno-asynchronous-unwind-tables
MOD_DIR     := $(ARCH_BUILD)/modules
MODULE_CORE_RELEASE := piux-1.0
USER_DESKTOP_OBJS := $(patsubst user/desktop/%.c,$(ARCH_BUILD)/user/desktop/%.o,$(wildcard user/desktop/*.c))
USER_APP_SOURCES := $(wildcard user/apps/*.c) $(wildcard user/apps/*/*.c)
USER_APP_OBJS := $(patsubst user/apps/%.c,$(ARCH_BUILD)/user/apps/%.o,$(USER_APP_SOURCES))
USER_API_OBJ := $(ARCH_BUILD)/user/lib/api.o $(ARCH_BUILD)/user/lib/pixelfont.o
INIT_OBJ := $(ARCH_BUILD)/init/init.o $(ARCH_BUILD)/user/lib/api.o $(ARCH_BUILD)/user/lib/pixelfont.o $(ARCH_BUILD)/user/lib/ustr.o
INITCTL_OBJ := $(ARCH_BUILD)/bin/initctl.o $(ARCH_BUILD)/user/lib/api.o $(ARCH_BUILD)/user/lib/pixelfont.o $(ARCH_BUILD)/user/lib/ustr.o
USER_PROGRAMS := $(ARCH_BUILD)/user/hello.elf $(ARCH_BUILD)/user/isolation_test.elf \
                 $(ARCH_BUILD)/user/desktop.elf \
                 $(ARCH_BUILD)/init/init.elf $(ARCH_BUILD)/bin/initctl.elf

LOGOS      := $(wildcard kernel/logo/ascii/*/*)

KERNEL_INCLUDES := -I $(ARCH_DIR)/include -I kernel/core -I kernel/fs -I kernel/net -I kernel/drivers -I kernel/video
LOGO_OBJS  := $(patsubst kernel/logo/ascii/%,$(ARCH_BUILD)/logo-%.o,$(LOGOS))

OBJECTS := $(ARCH_BOOT_OBJ) $(KERNEL_OBJS) $(KERNEL_ASM_OBJS) $(LOGO_OBJS) $(ARCH_BUILD)/os-infos.o
MODULE_FILES := $(patsubst %,$(MOD_DIR)/%.mod,$(MODULES))

all: $(ISO) $(MODULE_FILES) $(USER_PROGRAMS)

$(ARCH_BOOT_OBJ): $(ARCH_DIR)/boot/bootx.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -o $@ $<

$(ARCH_BUILD)/kernel/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(KERNEL_INCLUDES) -DPIUX_MODULE_RELEASE=\"$(MODULE_CORE_RELEASE)\" -c -o $@ $<

$(ARCH_BUILD)/kernel/%.o: kernel/%.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -o $@ $<

$(ARCH_BUILD)/bin/%.o: bin/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(KERNEL_INCLUDES) -I bin -c -o $@ $<

$(ARCH_BUILD)/tui/installer/%.o: tui/installer/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(KERNEL_INCLUDES) -I bin -I tui/installer -c -o $@ $<

# ---------------------------------------------------------------------------
# Loadable modules
# ---------------------------------------------------------------------------
# Each module is compiled with its own PIUX_MODULE_<NAME> guard (so the
# headers provide compile-time interface functions) and linked with `ld -r`
# into a relocatable object. The loader takes care of resolution at runtime.

MODULE_CORE_RELEASE := $(shell cat $(ARCH_BUILD)/kernel.release 2>/dev/null || printf piux-1.0)

# Only modules with their own build rule are buildable; unknown names fail here
# instead of recursing into a missing _module-stage-<name> target.
SUPPORTED_MODULES := vfs commands net installer
UNKNOWN_MODULES := $(filter-out $(SUPPORTED_MODULES),$(MODULES))
ifneq ($(UNKNOWN_MODULES),)
$(error Unsupported MODULES entry for $(ARCH_NAME): $(UNKNOWN_MODULES))
endif


$(MOD_DIR)/%.mod: FORCE
	@mkdir -p $(dir $@)
	@$(MAKE) --no-print-directory -f $(ARCH_DIR)/Makefile.mk PIUX_STAGE=$$(basename $@ .mod) _module-stage

$(MOD_DIR)/vfs.mod.stage: kernel/fs/vfs.c kernel/fs/procfs.c $(wildcard kernel/*/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(MODULE_FLAGS) -DPIUX_MODULE_VFS -DPIUX_MODULE_VERSION=\"1.0.0\" \
	  -DPIUX_MODULE_RELEASE=\"$(MODULE_CORE_RELEASE)\" $(KERNEL_INCLUDES) \
	  -c -o $(MOD_DIR)/vfs.o $<
	$(LD) -r -m $(ARCH_LD_TARGET) -o $(MOD_DIR)/vfs.mod $(MOD_DIR)/vfs.o
	@touch $@

COMMAND_SOURCES := $(filter-out bin/initctl.c,$(wildcard bin/*.c))

# COMMAND_SOURCES and the kernel headers must be known before this rule is
# declared; depending on them (instead of FORCE) makes an edited bin/*.c really
# rebuild commands.mod, while an untouched tree still relinks for free.
$(MOD_DIR)/commands.mod.stage: $(ARCH_BUILD)/modules/commands-src.stamp $(COMMAND_SOURCES) $(wildcard kernel/*/*.h)
	$(LD) -r -m $(ARCH_LD_TARGET) -o $(MOD_DIR)/commands.mod $(wildcard $(MOD_DIR)/commands/*.o)
	@touch $@

$(ARCH_BUILD)/modules/commands-src.stamp: $(COMMAND_SOURCES) $(wildcard kernel/*/*.h)
	@mkdir -p $(MOD_DIR)/commands
	@rm -f $(MOD_DIR)/commands/*.o
	@for source in $(COMMAND_SOURCES); do \
	  $(CC) $(MODULE_FLAGS) -DPIUX_MODULE_COMMANDS -DPIUX_MODULE_VERSION=\"1.0.0\" \
	    -DPIUX_MODULE_RELEASE=\"$(MODULE_CORE_RELEASE)\" $(KERNEL_INCLUDES) -I bin \
	    -c -o $(MOD_DIR)/commands/$$(basename $$source .c).o $$source || exit 1; \
	done
	@touch $@

$(MOD_DIR)/net.mod.stage: $(wildcard kernel/net/arp.c kernel/net/ip.c kernel/net/icmp.c kernel/net/udp.c kernel/net/tcp.c kernel/net/dhcp.c kernel/net/eth.c kernel/net/net.c) $(wildcard kernel/*/*.h) FORCE
	@mkdir -p $(MOD_DIR)/net
	@rm -f $(MOD_DIR)/net/*.o
	@for source in arp ip icmp udp tcp dhcp eth net; do \
	  $(CC) $(MODULE_FLAGS) -DPIUX_MODULE_NET -DPIUX_MODULE_VERSION=\"1.0.0\" \
	    -DPIUX_MODULE_RELEASE=\"$(MODULE_CORE_RELEASE)\" $(KERNEL_INCLUDES) \
	    -c -o $(MOD_DIR)/net/$$source.o kernel/net/$$source.c || exit 1; \
	done
	$(LD) -r -m $(ARCH_LD_TARGET) -o $(MOD_DIR)/net.mod $(MOD_DIR)/net/arp.o $(MOD_DIR)/net/ip.o \
	  $(MOD_DIR)/net/icmp.o $(MOD_DIR)/net/udp.o $(MOD_DIR)/net/tcp.o \
	  $(MOD_DIR)/net/dhcp.o $(MOD_DIR)/net/eth.o $(MOD_DIR)/net/net.o
	@touch $@

$(MOD_DIR)/installer.mod.stage: tui/installer/installer.c $(wildcard kernel/*/*.h) FORCE
	@mkdir -p $(dir $@)
	$(CC) $(MODULE_FLAGS) -DPIUX_MODULE_INSTALLER -DPIUX_MODULE_VERSION=\"1.0.0\" \
	  -DPIUX_MODULE_RELEASE=\"$(MODULE_CORE_RELEASE)\" $(KERNEL_INCLUDES) -I tui/installer \
	  -c -o $(MOD_DIR)/installer.o $<
	$(LD) -r -m $(ARCH_LD_TARGET) -o $(MOD_DIR)/installer.mod $(MOD_DIR)/installer.o
	@touch $@

ifeq ($(filter $(PIUX_STAGE),$(SUPPORTED_MODULES)),$(PIUX_STAGE))
_module-stage: _module-stage-$(PIUX_STAGE)
endif

_module-stage-vfs: $(MOD_DIR)/vfs.mod.stage
_module-stage-commands: $(MOD_DIR)/commands.mod.stage
_module-stage-net: $(MOD_DIR)/net.mod.stage
_module-stage-installer: $(MOD_DIR)/installer.mod.stage

modules: $(MODULE_FILES)

FORCE:

$(ARCH_BUILD)/user/desktop/%.o: user/desktop/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I user/lib -I user/desktop -I user/apps -c -o $@ $<

$(ARCH_BUILD)/user/apps/%.o: user/apps/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I user/lib -I user/desktop -I user/apps -c -o $@ $<

$(ARCH_BUILD)/user/lib/%.o: user/lib/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I user/lib -I user/desktop -c -o $@ $<

$(ARCH_BUILD)/user/hello.o: user/hello.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(ARCH_BUILD)/user/hello.elf: $(ARCH_BUILD)/user/hello.o user/user.ld
	$(LD) -T user/user.ld -m $(ARCH_LD_TARGET) -nostdlib -o $@ $<

$(ARCH_BUILD)/user/isolation_test.o: user/isolation_test.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(ARCH_BUILD)/user/isolation_test.elf: $(ARCH_BUILD)/user/isolation_test.o user/user.ld
	$(LD) -T user/user.ld -m $(ARCH_LD_TARGET) -nostdlib -o $@ $<

$(ARCH_BUILD)/user/desktop.elf: $(USER_DESKTOP_OBJS) $(USER_APP_OBJS) $(USER_API_OBJ) $(CURSOR_OBJ) user/user.ld
	$(LD) -T user/user.ld -m $(ARCH_LD_TARGET) -nostdlib -o $@ $(USER_DESKTOP_OBJS) $(USER_APP_OBJS) $(USER_API_OBJ) $(CURSOR_OBJ)

$(ARCH_BUILD)/init/init.o: init/init.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I user/lib $(KERNEL_INCLUDES) -c -o $@ $<

$(ARCH_BUILD)/init/init.elf: $(INIT_OBJ) user/user.ld
	$(LD) -T user/user.ld -m $(ARCH_LD_TARGET) -nostdlib -o $@ $(INIT_OBJ)

$(ARCH_BUILD)/bin/initctl.o: bin/initctl.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I user/lib $(KERNEL_INCLUDES) -c -o $@ $<

$(ARCH_BUILD)/bin/initctl.elf: $(INITCTL_OBJ) user/user.ld
	$(LD) -T user/user.ld -m $(ARCH_LD_TARGET) -nostdlib -o $@ $(INITCTL_OBJ)

user-programs: $(USER_PROGRAMS)

install-user-programs: $(DISK) $(USER_PROGRAMS) $(MODULE_FILES)
	@for dir in /bin /usr /usr/bin /lib /lib/modules /etc /home /root /tmp /var /var/log /var/tmp; do \
	  debugfs -w -R "mkdir $$dir" $(DISK) >/dev/null 2>&1 || true; \
	done
	debugfs -w -R "rm /desktop.elf" $(DISK) >/dev/null 2>&1 || true
	debugfs -w -R "rm /bin/desktop.elf" $(DISK) >/dev/null 2>&1 || true
	debugfs -w -R "write $(ARCH_BUILD)/user/desktop.elf /bin/desktop.elf" $(DISK)
	debugfs -w -R "rm /usr/bin/hello" $(DISK) >/dev/null 2>&1 || true
	debugfs -w -R "write $(ARCH_BUILD)/user/hello.elf /usr/bin/hello" $(DISK)
	debugfs -w -R "rm /usr/bin/isolation-test" $(DISK) >/dev/null 2>&1 || true
	debugfs -w -R "write $(ARCH_BUILD)/user/isolation_test.elf /usr/bin/isolation-test" $(DISK)
	debugfs -w -R "mkdir /sbin" $(DISK) >/dev/null 2>&1 || true
	debugfs -w -R "rm /sbin/init.elf" $(DISK) >/dev/null 2>&1 || true
	debugfs -w -R "write $(ARCH_BUILD)/init/init.elf /sbin/init.elf" $(DISK)
	debugfs -w -R "rm /usr/bin/initctl" $(DISK) >/dev/null 2>&1 || true
	debugfs -w -R "write $(ARCH_BUILD)/bin/initctl.elf /usr/bin/initctl" $(DISK)
	debugfs -w -R "mkdir /etc" $(DISK) >/dev/null 2>&1 || true
	debugfs -w -R "mkdir /etc/piux" $(DISK) >/dev/null 2>&1 || true
	debugfs -w -R "rm /etc/piux/init.conf" $(DISK) >/dev/null 2>&1 || true
	echo -e "# Piux init configuration\n\n[STARTUP]\npde\n\n[SERVICE:net]\ntype = oneshot\n\n" > $(ARCH_BUILD)/init.conf.tmp
	debugfs -w -R "write $(ARCH_BUILD)/init.conf.tmp /etc/piux/init.conf" $(DISK)
	for module in $(MODULE_FILES); do \
	  debugfs -w -R "mkdir /lib" $(DISK) >/dev/null 2>&1 || true; \
	  debugfs -w -R "mkdir /lib/modules" $(DISK) >/dev/null 2>&1 || true; \
	  debugfs -w -R "rm /lib/modules/$$(basename $$module)" $(DISK) >/dev/null 2>&1 || true; \
	  debugfs -w -R "write $$module /lib/modules/$$(basename $$module)" $(DISK); \
	done

install-modules: $(DISK) $(MODULE_FILES)
	for module in $(MODULE_FILES); do \
	  debugfs -w -R "mkdir /lib" $(DISK) >/dev/null 2>&1 || true; \
	  debugfs -w -R "mkdir /lib/modules" $(DISK) >/dev/null 2>&1 || true; \
	  debugfs -w -R "rm /lib/modules/$$(basename $$module)" $(DISK) >/dev/null 2>&1 || true; \
	  debugfs -w -R "write $$module /lib/modules/$$(basename $$module)" $(DISK); \
	done

$(ARCH_BUILD)/os-infos.o: etc/os-infos
	@mkdir -p $(dir $@)
	$(OBJCOPY) -I binary -O $(ARCH_OBJCOPY_TGT) -B $(ARCH_OBJCOPY_BFD) \
	  --rename-section .data=.os_infos $< $@

$(CURSOR_RAW): kernel/logo/cur/arrow.png
	@mkdir -p $(dir $@)
	convert $< -resize 24x24! -depth 8 RGBA:$@

$(CURSOR_OBJ): $(CURSOR_RAW)
	$(OBJCOPY) -I binary -O $(ARCH_OBJCOPY_TGT) -B $(ARCH_OBJCOPY_BFD) \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents $< $@

$(ARCH_BUILD)/logo-%.o: kernel/logo/ascii/%
	@mkdir -p $(dir $@)
	$(OBJCOPY) -I binary -O $(ARCH_OBJCOPY_TGT) -B $(ARCH_OBJCOPY_BFD) \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents $< $@

$(KERNEL): $(OBJECTS) $(ARCH_DIR)/linker/kernel.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

$(ISO): $(KERNEL) grub.cfg
	mkdir -p $(BUILD_DIR)/isodir/boot/grub
	cp $(KERNEL) $(BUILD_DIR)/isodir/boot/
	cp grub.cfg $(BUILD_DIR)/isodir/boot/grub/
	grub-mkrescue -o $@ $(BUILD_DIR)/isodir

iso: $(ISO)

$(DISK):
	dd if=/dev/zero of=$@ bs=1M count=32 status=none
	mke2fs -q -t ext2 -F $@

run: $(ISO) install-user-programs
	qemu-system-i386 -cdrom $(ISO) -drive file=$(DISK),format=raw,if=ide -device rtl8139,netdev=n0 -netdev user,id=n0 -m 512M -vga std -display gtk,zoom-to-fit=off

debug: $(KERNEL)
	qemu-system-i386 -cdrom $(ISO) -m 512M -s -S &
	gdb -ex "target remote :1234" -ex "symbol-file $(KERNEL)"

# Removes the selected port's output only, so other ports are left untouched.
clean:
	rm -rf $(ARCH_BUILD) $(BUILD_DIR)/isodir $(DISK) $(CURSOR_RAW) $(CURSOR_OBJ)

.PHONY: all iso run debug clean user-programs install-user-programs install-modules modules FORCE


