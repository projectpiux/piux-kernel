#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$SCRIPT_DIR"

echo "Piux Build & Boot Script"
echo "This script speeds up the process of cleaning, compiling, creating images and starting Piux in VM"
echo "v1.0.0"

DISK="build/ext2.img"
ISO="piux.iso"

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "[!] Missing required command: $1"
        echo "    Install the Piux prerequisites from README.md, then run this script again."
        exit 1
    fi
}

require_command make

if [[ -n "${PIUX_QEMU_DISPLAY:-}" ]]; then
    QEMU_DISPLAY=("-display" "$PIUX_QEMU_DISPLAY")
elif [[ -n "${WAYLAND_DISPLAY:-}" || -n "${DISPLAY:-}" ]]; then
    QEMU_DISPLAY=("-display" "gtk,zoom-to-fit=off")
else
    QEMU_DISPLAY=("-display" "curses")
    echo "[!] No graphical display detected; curses does not show the graphical framebuffer."
fi

build_piux() {
    echo "[*] Cleaning..."
    make clean
    echo "[*] Compiling..."
    make
}

create_disk() {
    echo "[*] Creating disk image..."
    make install-user-programs
}

start_vm() {
    require_command qemu-system-i386
    echo "[*] Starting VM with an adaptive framebuffer..."
    qemu-system-i386 \
        -cdrom "$ISO" \
        -drive "file=$DISK,format=raw,if=ide" \
        -device rtl8139,netdev=n0 \
        -netdev user,id=n0 \
        -m 512M \
        -vga std \
        "${QEMU_DISPLAY[@]}"
}

while true; do
    echo ""
    echo "Select an option:"
    echo "  1) Compile from scratch"
    echo "  2) Boot from an existing disk (VM)"
    echo "  3) Compile from scratch and start from VM disk"
    echo "  4) Exit"
    echo -n "> "
    if ! read -r choice; then
        echo ""
        echo "Bye!"
        exit 0
    fi

    case "$choice" in
        1)
            build_piux
            ;;
        2)
            if [ ! -f "$DISK" ] || [ ! -f "$ISO" ]; then
                echo "[!] Missing $DISK or $ISO. Build Piux first with option 3."
                continue
            fi
            make install-user-programs
            start_vm
            ;;
        3)
            build_piux
            create_disk
            start_vm
            ;;
        4)
            echo "Bye!"
            exit 0
            ;;
        *)
            echo "[!] Invalid choice"
            ;;
    esac
done
