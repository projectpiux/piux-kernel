<div align="center">

![Piux Banner](banner.png)

# Piux

**A small, Unix-like operating system for 32-bit x86, written from scratch.**

![Version](https://img.shields.io/badge/version-Rolling%20release-blue?style=flat-square)
![Arch](https://img.shields.io/badge/arch-i386-informational?style=flat-square)
![Boot](https://img.shields.io/badge/boot-GRUB%20Multiboot1-orange?style=flat-square)
![Language](https://img.shields.io/badge/lang-C%20%2B%20NASM-success?style=flat-square)
![License](https://img.shields.io/badge/license-GPLv3-lightgrey?style=flat-square)

[Overview](#-overview) •
[Quick Start](#-quick-start) •
[Running](#-running) •
[Interfaces](#-interfaces) •
[Under the Hood](#-under-the-hood)
[Community](#-community) •
[Contributing](#-contributing)

<br>

![Piux desktop preview](preview.png)

<sub>The PDE desktop running on Piux in QEMU.</sub>

<br>

[Thank you, contributors! <3](THANK-YOU-CONTRIBUTORS.md)

</div>

---

## ✨ Overview

Piux is an experimental operating system built to be **read, understood and modified**. It boots through GRUB, switches to a 1280×720 framebuffer, mounts a real ext2 disk and starts **PDE**, its desktop environment, all written in freestanding C and NASM.

It follows a rolling-release model with stable ISO snapshots, and every commit can be built into a working system.

| | |
|---|---|
| 🧠 **Kernel** | 32-bit protected mode with paging, interrupts, a timer and user-mode support |
| 🧩 **Modules** | Relocatable kernel modules (VFS, commands, net, installer) loaded at boot |
| 💾 **Storage** | ATA disk driver and a read/write ext2 filesystem behind a Unix-like VFS |
| 🖥️ **Graphics** | Native 1280×720 32-bit framebuffer with software rendering and double buffering |
| 🪟 **Interface** | PDE desktop running in user mode |
| 🔐 **Accounts** | First-boot installer, login and hashed passwords |
| 🌐 **Network** | RTL8139 driver and a minimal IP stack, with a built-in package manager |

> [!WARNING]
> Piux is a learning-oriented project. It is not meant to replace a mature operating system, and its security model is experimental.

---

## 🚀 Quick Start

```bash
# 📥 Get the source
git clone https://github.com/projectpiux/piux-kernel.git
cd piux-kernel

# 🚀 Build Piux and boot it in QEMU
bash fast-all.sh
```

`fast-all.sh` is an interactive TUI that builds Piux and boots it in QEMU. It can also create a fresh ext2 disk image or open an existing one. To clean previous build output:

```bash
# 🧹 Remove all previous build output
make clean
```

### Architecture ports

Everything CPU-specific lives in `arch/<name>/` (entry stub, linker script, toolchain flags); the rest of the kernel is architecture-independent. The only port today is `arch/x86` (32-bit x86 / i386), which is also the default, and its artifacts land in `build/x86/` (`kernel.elf`, `piux.iso`, `modules/`, user programs):

```bash
# 🧱 Build a specific port (ARCH defaults to x86)
make ARCH=x86

# 🧹 Clean just that port's build tree
make ARCH=x86 clean
```

See `docs/ARCH.md` for the layout and how to add a port.

### Prerequisites

<details open>
<summary><b>Debian / Ubuntu</b></summary>

```bash
# 📦 Toolchain · bootloader · ISO tools · emulator · ext2 utilities
sudo apt-get install nasm gcc binutils grub-pc-bin xorriso qemu-system-x86 e2fsprogs
```
</details>

<details>
<summary><b>Fedora / RHEL</b></summary>

```bash
# 📦 Toolchain · bootloader · ISO tools · emulator · ext2 utilities
sudo dnf install nasm gcc binutils grub2-tools xorriso qemu-system-x86 e2fsprogs
```
</details>

<details>
<summary><b>macOS (Homebrew)</b></summary>

```bash
# 📦 Toolchain · bootloader · ISO tools · emulator · ext2 utilities
brew install nasm gcc binutils grub xorriso qemu e2fsprogs
```
</details>

### System requirements

| Resource | Minimum |
|---|---|
| CPU | 32-bit x86 / i386-compatible |
| RAM | 32 MiB |
| Disk | 64 MiB |
| Firmware | BIOS / legacy boot with GRUB Multiboot1 |
| Video | Multiboot framebuffer with a supported graphics mode |

> [!NOTE]
> These are the project's target minimums. The default QEMU setup uses more.

---

## 🏃 Running

### QEMU

`fast-all.sh` boots Piux for you. To run an existing ISO manually (without a disk, Piux falls back to an in-memory filesystem):

```bash
# 🖥️ Boot the ISO with a 1280x720 framebuffer and 512 MiB of RAM
qemu-system-i386 \
  -cdrom build/x86/piux.iso \
  -m 512M \
  -vga none \
  -device VGA,xres=1280,yres=720
```

### Real hardware

Piux is technically bootable on real x86 hardware, but this is **strongly discouraged**: it has not been tested on physical machines and is not mature enough yet. Use QEMU instead.

> [!CAUTION]
> Booting on real hardware is at your own risk. If you still want to try, writing the ISO to a USB drive with `dd` **erases** the target device, so double-check the device path.

---

## 💻 Using Piux

On first boot with a blank disk, a keyboard-driven **installer** sets up the filesystem and your account. After that, you log in and land in the default desktop.

```text
╭──────────────────────────────╮
│        Welcome to Piux!      │
╰──────────────────────────────╯

user@piux> help
```

Type `help` at any time to list the available commands.

---

## 🪟 Interfaces

Piux ships with PDE, a mouse- and keyboard-driven desktop whose interface and registered apps run in user mode.

| Interface | Role | Description |
|---|---|---|
| **PDE** | Desktop | The Piux Desktop Environment: a mouse and keyboard desktop rendered through kernel video and input syscalls. |

---

## 🏗️ Under the Hood

**Boot.** BIOS hands over to GRUB, which loads the Piux kernel through Multiboot1 and passes along the framebuffer information. The kernel initializes its subsystems, mounts ext2 when a disk is available, runs the installer if needed, authenticates the user and starts the desktop.

```text
┌──────┐   ┌──────┐   ┌──────────────┐   ┌─────────────┐   ┌───────────────────┐   ┌─────────────┐
│ BIOS │ ─▶│ GRUB │ ─▶│ Piux kernel  │ ─▶│ ext2 mount  │ ─▶│ installer / login │ ─▶│     PDE     │
└──────┘   └──────┘   └──────────────┘   └─────────────┘   └───────────────────┘   └─────────────┘
```

**Filesystem.** Everything is exposed through a Unix-like VFS with file descriptors and device nodes such as `/dev/console` and `/dev/hda`. The ext2 driver is non-journaled and supports direct, single- and double-indirect blocks, file creation, reading, writing and deletion. Without a disk, Piux falls back to RAMFS.

**Processes.** The kernel provides `int 0x80` syscalls, a kernel/user GDT with TSS, isolated page directories and a preemptive round-robin scheduler for ring3 ELF processes. `/sbin/init.elf` runs as PID 1 and is controlled by `initctl` over the PIUX-IPC/1 mailbox protocol (`docs/INIT.md`); if init is missing the kernel starts a recovery shell. The desktop terminal invokes kernel shell handlers through a checked syscall rather than running each command as a separate user process.

**Ports.** CPU-specific code (boot stub, linker script, toolchain flags, architecture description) is isolated under `arch/<name>/`; `make ARCH=<name>` selects the port and builds into `build/<name>/`. Only the `x86` (i386) port exists for now, and the rest of the kernel is shared across ports. See `docs/ARCH.md`.

**Modules.** The core binary is small on purpose: the VFS, the command table, the network stack and the installer are built as relocatable modules (`/lib/modules/*.mod`) that the kernel links into itself at boot through its own ELF loader and kernel-symbol table. They run in kernel mode, in the same address space, but are versioned, dependency-ordered and reloadable — a monolithic kernel with a clean core/module boundary. See `docs/MODULES.md`.

**Graphics.** Text is rendered directly into the framebuffer with a software bitmap font. The interfaces draw into a back buffer and present complete frames, which avoids flicker.

**Network.** A polling-mode RTL8139 driver with ARP, IPv4, ICMP, UDP and a minimal single-connection TCP client, enough for HTTP downloads and the `pix` package manager.

<details>
<summary><b>Troubleshooting</b></summary>

**GRUB not found** — install the rescue tools:

```bash
sudo apt-get install grub-pc-bin xorriso
```

**QEMU won't start** — check that it is installed and install the QEMU x86 package if missing:

```bash
qemu-system-i386 --version
```

**No framebuffer** — make sure QEMU provides a graphics mode, e.g.:

```bash
-vga none -device VGA,xres=1280,yres=720
```

**Keyboard not responding** — check that QEMU exposes a PS/2 keyboard.

</details>

---

## 💬 Community

Join the Piux server on Fluxer to chat, ask questions, share your setup and follow development:

**[fluxer.gg/grjrqjHM](https://fluxer.gg/grjrqjHM)**

---

## 🎯 Philosophy

Piux favors **small size, simplicity, direct hardware interaction and learning by implementation**. It is open, customizable and meant to be explored.

---

## 🤝 Contributing

Contributions are welcome.

```bash
# 1️⃣ Fork the repository on GitHub, then create a branch
git checkout -b feature/amazing-feature

# 2️⃣ Commit your changes
git commit -am "Add amazing feature"

# 3️⃣ Push it, then open a Pull Request
git push origin feature/amazing-feature
```

1. Fork the repository
2. Create a branch: `git checkout -b feature/amazing-feature`
3. Commit your changes: `git commit -am "Add amazing feature"`
4. Push it: `git push origin feature/amazing-feature`
5. Open a Pull Request

For bugs and questions, open an Issue and include your build output, QEMU configuration and hardware details.
See `CONTRIBUTING.md` before contributing.

---

## 📄 License

Released under the **GPLv3**. See `LICENSE.md` for details.

---

## 🙏 Credits

NASM documentation · OSDev.org · GNU GRUB / Multiboot documentation · the open-source OS development community

---

## Note

Piux is coded with the help of AI.