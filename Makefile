AS       := nasm
CC       := gcc
LD       := ld
OBJCOPY  := objcopy

CFLAGS   := -m32 -std=gnu11 -ffreestanding -fno-pic -fno-pie \
            -fno-stack-protector -nostdlib -Wall -Wextra \
            -Werror=implicit-function-declaration -march=i386 -O2 \
            -fno-asynchronous-unwind-tables -fno-unwind-tables
ASFLAGS  := -f elf32
LDFLAGS  := -T linker.ld -m elf_i386 -nostdlib

BUILD_DIR := build
KERNEL    := $(BUILD_DIR)/kernel.elf
ISO       := piux.iso
DISK      := $(BUILD_DIR)/ext2.img
CURSOR_RAW := $(BUILD_DIR)/cursor.rgba
CURSOR_OBJ := $(BUILD_DIR)/cursor.o

KERNEL_OBJS := $(patsubst kernel/%.c,$(BUILD_DIR)/kernel/%.o,$(wildcard kernel/*/*.c))
# kernel/core/kernel_entry.asm è mantenuto solo come riferimento: l'entry point
# reale (_start) vive in boot/bootx.asm e chiama kernel_main.
KERNEL_ASM_SOURCES := $(filter-out kernel/core/kernel_entry.asm,$(wildcard kernel/*/*.asm))
KERNEL_ASM_OBJS := $(patsubst kernel/%.asm,$(BUILD_DIR)/kernel/%.o,$(KERNEL_ASM_SOURCES))
# Subsystems shipped as loadable modules (see kernel/module_abi.h).
MODULES     := vfs commands net installer
MODULE_FLAGS := $(CFLAGS) -fno-asynchronous-unwind-tables
MOD_DIR     := $(BUILD_DIR)/modules
MODULE_CORE_RELEASE := piux-1.0
USER_DESKTOP_OBJS := $(patsubst user/desktop/%.c,$(BUILD_DIR)/user/desktop/%.o,$(wildcard user/desktop/*.c))
USER_APP_SOURCES := $(wildcard user/apps/*.c) $(wildcard user/apps/*/*.c)
USER_APP_OBJS := $(patsubst user/apps/%.c,$(BUILD_DIR)/user/apps/%.o,$(USER_APP_SOURCES))
USER_API_OBJ := $(BUILD_DIR)/user/lib/api.o $(BUILD_DIR)/user/lib/pixelfont.o
INIT_OBJ := $(BUILD_DIR)/init/init.o $(BUILD_DIR)/user/lib/api.o $(BUILD_DIR)/user/lib/pixelfont.o $(BUILD_DIR)/user/lib/ustr.o
INITCTL_OBJ := $(BUILD_DIR)/bin/initctl.o $(BUILD_DIR)/user/lib/api.o $(BUILD_DIR)/user/lib/pixelfont.o $(BUILD_DIR)/user/lib/ustr.o
USER_PROGRAMS := $(BUILD_DIR)/user/hello.elf $(BUILD_DIR)/user/isolation_test.elf \
                 $(BUILD_DIR)/user/desktop.elf \
                 $(BUILD_DIR)/init/init.elf $(BUILD_DIR)/bin/initctl.elf

LOGOS      := $(wildcard kernel/logo/ascii/*/*)

KERNEL_INCLUDES := -I kernel/core -I kernel/fs -I kernel/net -I kernel/drivers -I kernel/video
LOGO_OBJS  := $(patsubst kernel/logo/ascii/%,$(BUILD_DIR)/logo-%.o,$(LOGOS))

OBJECTS := $(BUILD_DIR)/bootx.o $(KERNEL_OBJS) $(KERNEL_ASM_OBJS) $(LOGO_OBJS) $(BUILD_DIR)/os-infos.o
MODULE_FILES := $(patsubst %,$(MOD_DIR)/%.mod,$(MODULES))

all: $(ISO) $(MODULE_FILES) $(USER_PROGRAMS)

$(BUILD_DIR)/bootx.o: boot/bootx.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD_DIR)/kernel/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(KERNEL_INCLUDES) -DPIUX_MODULE_RELEASE=\"$(MODULE_CORE_RELEASE)\" -c -o $@ $<

$(BUILD_DIR)/kernel/%.o: kernel/%.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD_DIR)/bin/%.o: bin/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(KERNEL_INCLUDES) -I bin -c -o $@ $<

$(BUILD_DIR)/tui/installer/%.o: tui/installer/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(KERNEL_INCLUDES) -I bin -I tui/installer -c -o $@ $<

# ---------------------------------------------------------------------------
# Loadable modules
# ---------------------------------------------------------------------------
# Each module is compiled with its own PIUX_MODULE_<NAME> guard (so the
# headers provide compile-time interface functions) and linked with `ld -r`
# into a relocatable object. The loader takes care of resolution at runtime.

MODULE_CORE_RELEASE := $(shell cat build/kernel.release 2>/dev/null || printf piux-1.0)

$(MOD_DIR)/%.mod: FORCE
	@mkdir -p $(dir $@)
	@$(MAKE) --no-print-directory $(MOD_DIR)/$*.mod.stage

$(MOD_DIR)/vfs.mod.stage: kernel/fs/vfs.c $(wildcard kernel/*/*.h) FORCE
	@mkdir -p $(dir $@)
	$(CC) $(MODULE_FLAGS) -DPIUX_MODULE_VFS -DPIUX_MODULE_VERSION=\"1.0.0\" \
	  -DPIUX_MODULE_RELEASE=\"$(MODULE_CORE_RELEASE)\" $(KERNEL_INCLUDES) \
	  -c -o $(MOD_DIR)/vfs.o $<
	$(LD) -r -m elf_i386 -o $(MOD_DIR)/vfs.mod $(MOD_DIR)/vfs.o
	@touch $@

$(MOD_DIR)/commands.mod.stage: $(BUILD_DIR)/modules/commands-src.stamp FORCE
	$(LD) -r -m elf_i386 -o $(MOD_DIR)/commands.mod $(wildcard $(MOD_DIR)/commands/*.o)
	@touch $@

COMMAND_SOURCES := $(filter-out bin/initctl.c,$(wildcard bin/*.c))

$(BUILD_DIR)/modules/commands-src.stamp: $(COMMAND_SOURCES) $(wildcard kernel/*/*.h) FORCE
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
	$(LD) -r -m elf_i386 -o $(MOD_DIR)/net.mod $(MOD_DIR)/net/arp.o $(MOD_DIR)/net/ip.o \
	  $(MOD_DIR)/net/icmp.o $(MOD_DIR)/net/udp.o $(MOD_DIR)/net/tcp.o \
	  $(MOD_DIR)/net/dhcp.o $(MOD_DIR)/net/eth.o $(MOD_DIR)/net/net.o
	@touch $@

$(MOD_DIR)/installer.mod.stage: tui/installer/installer.c $(wildcard kernel/*/*.h) FORCE
	@mkdir -p $(dir $@)
	$(CC) $(MODULE_FLAGS) -DPIUX_MODULE_INSTALLER -DPIUX_MODULE_VERSION=\"1.0.0\" \
	  -DPIUX_MODULE_RELEASE=\"$(MODULE_CORE_RELEASE)\" $(KERNEL_INCLUDES) -I tui/installer \
	  -c -o $(MOD_DIR)/installer.o $<
	$(LD) -r -m elf_i386 -o $(MOD_DIR)/installer.mod $(MOD_DIR)/installer.o
	@touch $@

modules: $(MODULE_FILES)

.PHONY: FORCE modules
FORCE:

$(BUILD_DIR)/user/desktop/%.o: user/desktop/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I user/lib -I user/desktop -I user/apps -c -o $@ $<

$(BUILD_DIR)/user/apps/%.o: user/apps/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I user/lib -I user/desktop -I user/apps -c -o $@ $<

$(BUILD_DIR)/user/lib/%.o: user/lib/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I user/lib -I user/desktop -c -o $@ $<

$(BUILD_DIR)/user/hello.o: user/hello.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/user/hello.elf: $(BUILD_DIR)/user/hello.o user/user.ld
	$(LD) -T user/user.ld -m elf_i386 -nostdlib -o $@ $<

$(BUILD_DIR)/user/isolation_test.o: user/isolation_test.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/user/isolation_test.elf: $(BUILD_DIR)/user/isolation_test.o user/user.ld
	$(LD) -T user/user.ld -m elf_i386 -nostdlib -o $@ $<

$(BUILD_DIR)/user/desktop.elf: $(USER_DESKTOP_OBJS) $(USER_APP_OBJS) $(USER_API_OBJ) $(CURSOR_OBJ) user/user.ld
	$(LD) -T user/user.ld -m elf_i386 -nostdlib -o $@ $(USER_DESKTOP_OBJS) $(USER_APP_OBJS) $(USER_API_OBJ) $(CURSOR_OBJ)

$(BUILD_DIR)/init/init.o: init/init.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I user/lib $(KERNEL_INCLUDES) -c -o $@ $<

$(BUILD_DIR)/init/init.elf: $(INIT_OBJ) user/user.ld
	$(LD) -T user/user.ld -m elf_i386 -nostdlib -o $@ $(INIT_OBJ)

$(BUILD_DIR)/bin/initctl.o: bin/initctl.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I user/lib $(KERNEL_INCLUDES) -c -o $@ $<

$(BUILD_DIR)/bin/initctl.elf: $(INITCTL_OBJ) user/user.ld
	$(LD) -T user/user.ld -m elf_i386 -nostdlib -o $@ $(INITCTL_OBJ)

user-programs: $(USER_PROGRAMS)

install-user-programs: $(DISK) $(USER_PROGRAMS) $(MODULE_FILES)
	@for dir in /bin /usr /usr/bin /lib /lib/modules /etc /home /root /tmp /var /var/log /var/tmp; do \
	  debugfs -w -R "mkdir $$dir" $(DISK) >/dev/null 2>&1 || true; \
	done
	debugfs -w -R "rm /desktop.elf" $(DISK) >/dev/null 2>&1 || true
	debugfs -w -R "rm /bin/desktop.elf" $(DISK) >/dev/null 2>&1 || true
	debugfs -w -R "write $(BUILD_DIR)/user/desktop.elf /bin/desktop.elf" $(DISK)
	debugfs -w -R "rm /usr/bin/hello" $(DISK) >/dev/null 2>&1 || true
	debugfs -w -R "write $(BUILD_DIR)/user/hello.elf /usr/bin/hello" $(DISK)
	debugfs -w -R "rm /usr/bin/isolation-test" $(DISK) >/dev/null 2>&1 || true
	debugfs -w -R "write $(BUILD_DIR)/user/isolation_test.elf /usr/bin/isolation-test" $(DISK)
	debugfs -w -R "mkdir /sbin" $(DISK) >/dev/null 2>&1 || true
	debugfs -w -R "rm /sbin/init.elf" $(DISK) >/dev/null 2>&1 || true
	debugfs -w -R "write $(BUILD_DIR)/init/init.elf /sbin/init.elf" $(DISK)
	debugfs -w -R "rm /usr/bin/initctl" $(DISK) >/dev/null 2>&1 || true
	debugfs -w -R "write $(BUILD_DIR)/bin/initctl.elf /usr/bin/initctl" $(DISK)
	debugfs -w -R "mkdir /etc" $(DISK) >/dev/null 2>&1 || true
	debugfs -w -R "mkdir /etc/piux" $(DISK) >/dev/null 2>&1 || true
	debugfs -w -R "rm /etc/piux/init.conf" $(DISK) >/dev/null 2>&1 || true
	echo -e "# Piux init configuration\n\n[STARTUP]\npde\n\n[SERVICE:net]\ntype = oneshot\n\n" > $(BUILD_DIR)/init.conf.tmp
	debugfs -w -R "write $(BUILD_DIR)/init.conf.tmp /etc/piux/init.conf" $(DISK)
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

$(BUILD_DIR)/os-infos.o: etc/os-infos
	@mkdir -p $(dir $@)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 \
	  --rename-section .data=.os_infos $< $@

$(CURSOR_RAW): kernel/logo/cur/arrow.png
	@mkdir -p $(dir $@)
	convert $< -resize 24x24! -depth 8 RGBA:$@

$(CURSOR_OBJ): $(CURSOR_RAW)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents $< $@

$(BUILD_DIR)/logo-%.o: kernel/logo/ascii/%
	@mkdir -p $(dir $@)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents $< $@

$(KERNEL): $(OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

$(ISO): $(KERNEL) grub.cfg
	mkdir -p $(BUILD_DIR)/isodir/boot/grub
	cp $(KERNEL) $(BUILD_DIR)/isodir/boot/
	cp grub.cfg $(BUILD_DIR)/isodir/boot/grub/
	grub-mkrescue -o $@ $(BUILD_DIR)/isodir

$(DISK):
	dd if=/dev/zero of=$@ bs=1M count=32 status=none
	mke2fs -q -t ext2 -F $@

run: $(ISO) install-user-programs
	qemu-system-i386 -cdrom $(ISO) -drive file=$(DISK),format=raw,if=ide -device rtl8139,netdev=n0 -netdev user,id=n0 -m 512M -vga std -display gtk,zoom-to-fit=off

debug: $(KERNEL)
	qemu-system-i386 -cdrom $(ISO) -m 512M -s -S &
	gdb -ex "target remote :1234" -ex "symbol-file $(KERNEL)"

clean:
	rm -rf $(BUILD_DIR) $(ISO)

.PHONY: all run debug clean user-programs install-user-programs install-modules