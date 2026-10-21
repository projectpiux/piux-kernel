AS       := nasm
CC       := gcc
LD       := ld
OBJCOPY  := objcopy

CFLAGS   := -m32 -std=gnu11 -ffreestanding -fno-pic -fno-pie \
            -fno-stack-protector -nostdlib -Wall -Wextra \
            -Werror=implicit-function-declaration -march=i386 -O2
ASFLAGS  := -f elf32
LDFLAGS  := -T linker.ld -m elf_i386 -nostdlib

BUILD_DIR := build
KERNEL    := $(BUILD_DIR)/kernel.elf
ISO       := piux.iso
DISK      := $(BUILD_DIR)/ext2.img
PWM_INFO  := $(BUILD_DIR)/pwm-info.o
CURSOR_RAW := $(BUILD_DIR)/cursor.rgba
CURSOR_OBJ := $(BUILD_DIR)/cursor.o

KERNEL_OBJS := $(patsubst kernel/%.c,$(BUILD_DIR)/kernel/%.o,$(wildcard kernel/*.c))
KERNEL_ASM_OBJS := $(BUILD_DIR)/kernel/syscall_entry.o
KERNEL_ASM_OBJS += $(BUILD_DIR)/kernel/gdt_flush.o
KERNEL_ASM_OBJS += $(BUILD_DIR)/kernel/interrupt_entry.o
KERNEL_ASM_OBJS += $(BUILD_DIR)/kernel/context.o
BIN_OBJS    := $(patsubst bin/%.c,$(BUILD_DIR)/bin/%.o,$(wildcard bin/*.c))
TUI_OBJS    := $(patsubst tui/installer/%.c,$(BUILD_DIR)/tui/installer/%.o,$(wildcard tui/installer/*.c))
WM_OBJS     := $(patsubst tui/wm/%.c,$(BUILD_DIR)/tui/wm/%.o,$(wildcard tui/wm/*.c))

LOGOS      := $(wildcard kernel/logo/ascii/*/*)
LOGO_OBJS  := $(patsubst kernel/logo/ascii/%,$(BUILD_DIR)/logo-%.o,$(LOGOS))

OBJECTS := $(BUILD_DIR)/bootx.o $(KERNEL_OBJS) $(KERNEL_ASM_OBJS) $(BIN_OBJS) $(TUI_OBJS) $(WM_OBJS) $(LOGO_OBJS) $(BUILD_DIR)/os-infos.o $(PWM_INFO) $(CURSOR_OBJ)

all: $(ISO)

$(BUILD_DIR)/bootx.o: boot/bootx.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD_DIR)/kernel/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/kernel/%.o: kernel/%.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD_DIR)/bin/%.o: bin/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I kernel -I bin -c -o $@ $<

$(BUILD_DIR)/tui/installer/%.o: tui/installer/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I kernel -I bin -I tui/installer -c -o $@ $<

$(BUILD_DIR)/tui/wm/%.o: tui/wm/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I kernel -I bin -I tui/wm -c -o $@ $<

$(BUILD_DIR)/os-infos.o: etc/os-infos
	@mkdir -p $(dir $@)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 \
	  --rename-section .data=.os_infos $< $@

$(PWM_INFO): etc/pwm-info
	@mkdir -p $(dir $@)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 \
	  --rename-section .data=.pwm_info,alloc,load,readonly,data,contents $< $@

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

run: $(ISO) $(DISK)
	qemu-system-i386 -cdrom $(ISO) -drive file=$(DISK),format=raw,if=ide -device rtl8139,netdev=n0 -netdev user,id=n0 -m 512M -vga std -display gtk,zoom-to-fit=off

debug: $(KERNEL)
	qemu-system-i386 -cdrom $(ISO) -m 512M -s -S &
	gdb -ex "target remote :1234" -ex "symbol-file $(KERNEL)"

clean:
	rm -rf $(BUILD_DIR) $(ISO)

.PHONY: all run debug clean