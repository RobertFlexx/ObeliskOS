#
# Obelisk OS - Top Level Makefile
# From Axioms, Order.
#

# Directories
KERNEL_DIR := kernel
USERLAND_DIR := userland
TOOLS_DIR := tools
BUILD_DIR := build
ISO_DIR := $(BUILD_DIR)/iso
ROOTFS_DIR := $(BUILD_DIR)/rootfs
ROOTFS_OVERLAY_DIR := rootfs-overlay
USERLAND_BUILD_DIR := $(USERLAND_DIR)/build
USERLAND_OUT_DIR := $(USERLAND_BUILD_DIR)/out

# Output files
KERNEL := $(BUILD_DIR)/kernel.elf
ISO := $(BUILD_DIR)/obelisk.iso
ROOTFS_TAR := $(BUILD_DIR)/rootfs.tar

# GRUB configuration
GRUB_CFG := grub.cfg

# QEMU settings
QEMU := qemu-system-x86_64
QEMU_MEMORY := 512M
QEMU_BASE_FLAGS := -m $(QEMU_MEMORY)
QEMU_DISPLAY ?= gtk,gl=off
QEMU_GRAPHICAL_FLAGS := -vga std -display $(QEMU_DISPLAY) -serial vc
QEMU_SERIAL_FLAGS := -nographic -monitor none -chardev stdio,mux=on,signal=off,id=char0 -serial chardev:char0

# Host userland import settings (real zsh/coreutils from host)
IMPORT_HOST_USERLAND ?= 1
HOST_ZSH_BIN ?= /usr/bin/zsh
HOST_COREUTILS_BINS ?= ls cat cp mv rm mkdir rmdir ln chmod chown pwd uname date head tail wc sort uniq cut tr tee sleep true false env printenv id whoami
HOST_USERLAND_IMPORT_SCRIPT := $(TOOLS_DIR)/import-host-userland.sh

# Debug flags
QEMU_DEBUG_FLAGS := \
    -s -S \
    -d int,cpu_reset

# Default target
.PHONY: all
all: iso

# Build kernel
.PHONY: kernel
kernel:
	@echo "Building kernel..."
	@$(MAKE) -C $(KERNEL_DIR)
	@mkdir -p $(BUILD_DIR)
	@cp $(KERNEL_DIR)/build/kernel.elf $(KERNEL)

# Build userland
.PHONY: userland
userland:
	@echo "Building userland..."
	@$(MAKE) -C $(USERLAND_DIR)

# Build a minimal rootfs tarball for future init handoff
.PHONY: rootfs
rootfs: userland
	@echo "Packaging root filesystem..."
	@mkdir -p $(ROOTFS_DIR)/sbin $(ROOTFS_DIR)/bin $(ROOTFS_DIR)/usr/bin $(ROOTFS_DIR)/usr/lib $(ROOTFS_DIR)/usr/lib64 $(ROOTFS_DIR)/lib $(ROOTFS_DIR)/lib64 $(ROOTFS_DIR)/etc/axiomd/policy $(ROOTFS_DIR)/etc $(ROOTFS_DIR)/var/log $(ROOTFS_DIR)/tmp $(ROOTFS_DIR)/proc $(ROOTFS_DIR)/dev
	@cp $(USERLAND_OUT_DIR)/init $(ROOTFS_DIR)/sbin/init
	@cp $(USERLAND_OUT_DIR)/sysctl $(ROOTFS_DIR)/sbin/sysctl
	@cp $(USERLAND_OUT_DIR)/installer $(ROOTFS_DIR)/sbin/installer
	@cp $(USERLAND_OUT_DIR)/installer-tui $(ROOTFS_DIR)/sbin/installer-tui
	@cp $(USERLAND_DIR)/axiomd/main.pro $(ROOTFS_DIR)/etc/axiomd/main.pro
	@cp $(USERLAND_DIR)/axiomd/kernel_ipc.pro $(ROOTFS_DIR)/etc/axiomd/kernel_ipc.pro
	@cp $(USERLAND_DIR)/axiomd/sandbox.pro $(ROOTFS_DIR)/etc/axiomd/sandbox.pro
	@cp $(USERLAND_DIR)/axiomd/policy/access.pro $(ROOTFS_DIR)/etc/axiomd/policy/access.pro
	@cp $(USERLAND_DIR)/axiomd/policy/allocation.pro $(ROOTFS_DIR)/etc/axiomd/policy/allocation.pro
	@cp $(USERLAND_DIR)/axiomd/policy/inheritance.pro $(ROOTFS_DIR)/etc/axiomd/policy/inheritance.pro
	@for tool in busybox sh zsh su sudo idcpp ls cat cp mv rm mkdir ln chmod chown sync mount umount dmesg ps kill grep awk sed tar; do \
		if [ -f "$(USERLAND_OUT_DIR)/$$tool" ]; then \
			cp "$(USERLAND_OUT_DIR)/$$tool" "$(ROOTFS_DIR)/bin/$$tool"; \
		fi; \
	done
	@if [ -f "$(ROOTFS_DIR)/bin/busybox" ] && [ ! -f "$(ROOTFS_DIR)/bin/sh" ]; then \
		ln -sf /bin/busybox "$(ROOTFS_DIR)/bin/sh"; \
	fi
	@if [ -f "$(ROOTFS_DIR)/bin/busybox" ] && [ ! -f "$(ROOTFS_DIR)/bin/zsh" ]; then \
		ln -sf /bin/busybox "$(ROOTFS_DIR)/bin/zsh"; \
	fi
	@if [ -f "$(ROOTFS_DIR)/bin/busybox" ] && [ ! -f "$(ROOTFS_DIR)/bin/su" ]; then \
		ln -sf /bin/busybox "$(ROOTFS_DIR)/bin/su"; \
	fi
	@if [ -f "$(ROOTFS_DIR)/bin/busybox" ] && [ ! -f "$(ROOTFS_DIR)/bin/sudo" ]; then \
		ln -sf /bin/busybox "$(ROOTFS_DIR)/bin/sudo"; \
	fi
	@if [ -d "$(ROOTFS_OVERLAY_DIR)" ]; then \
		echo "Applying rootfs overlay from $(ROOTFS_OVERLAY_DIR)..."; \
		cp -a "$(ROOTFS_OVERLAY_DIR)/." "$(ROOTFS_DIR)/"; \
	fi
	@if [ "$(IMPORT_HOST_USERLAND)" = "1" ] && [ -x "$(HOST_USERLAND_IMPORT_SCRIPT)" ]; then \
		"$(HOST_USERLAND_IMPORT_SCRIPT)" "$(ROOTFS_DIR)" "$(HOST_ZSH_BIN)" $(HOST_COREUTILS_BINS); \
	fi
	@rm -f "$(ROOTFS_DIR)/README.txt"
	@printf "%s\n" \
		"Obelisk OS early userspace image" \
		"Use /sbin/installer or /sbin/installer-tui to stage an installation." \
		"Host zsh/coreutils import is enabled by default (set IMPORT_HOST_USERLAND=0 to disable)." \
		> "$(ROOTFS_DIR)/etc/motd"
	@tar -C $(ROOTFS_DIR) -cf $(ROOTFS_TAR) .

# Create ISO image
.PHONY: iso
iso: kernel rootfs
	@echo "Creating ISO image..."
	@command -v grub-mkrescue >/dev/null || (echo "Error: grub-mkrescue is required" && exit 1)
	@command -v xorriso >/dev/null || (echo "Error: xorriso is required by grub-mkrescue" && exit 1)
	@mkdir -p $(ISO_DIR)/boot/grub
	@cp $(KERNEL) $(ISO_DIR)/boot/
	@cp $(ROOTFS_TAR) $(ISO_DIR)/boot/
	@cp $(GRUB_CFG) $(ISO_DIR)/boot/grub/
	@grub-mkrescue -o $(ISO) $(ISO_DIR)
	@echo "ISO created: $(ISO)"

# Run in QEMU (default: robust serial mode)
.PHONY: run
run: iso
	@echo "Starting QEMU (default serial console mode)..."
	@echo "Ctrl+C is sent to guest; quit with Ctrl+A then X"
	@$(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SERIAL_FLAGS) -cdrom $(ISO)

# Run in QEMU graphical mode
.PHONY: run-gui
run-gui: iso
	@echo "Starting QEMU..."
	@$(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_GRAPHICAL_FLAGS) -cdrom $(ISO)

.PHONY: run-sdl
run-sdl: iso
	@echo "Starting QEMU (SDL display fallback)..."
	@$(QEMU) $(QEMU_BASE_FLAGS) -vga std -display sdl -serial vc -cdrom $(ISO)

# Run with terminal-attached serial console
.PHONY: run-serial
run-serial: iso
	@echo "Starting QEMU with terminal serial console..."
	@echo "Tip: Ctrl+C now goes to guest UART, not host kill."
	@echo "QEMU quit keys: Ctrl+A then X"
	@$(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SERIAL_FLAGS) -cdrom $(ISO)

# Run with KVM acceleration
.PHONY: run-kvm
run-kvm: iso
	@echo "Starting QEMU with KVM..."
	@$(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_GRAPHICAL_FLAGS) -enable-kvm -cpu host -cdrom $(ISO)

# Debug with GDB
.PHONY: debug
debug: iso
	@echo "Starting QEMU in debug mode..."
	@echo "Connect GDB with: target remote localhost:1234"
	@$(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_GRAPHICAL_FLAGS) $(QEMU_DEBUG_FLAGS) -cdrom $(ISO)

# Run with verbose logging
.PHONY: run-verbose
run-verbose: iso
	@$(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_SERIAL_FLAGS) -d int -cdrom $(ISO) 2>&1 | head -1000

# Clean everything
.PHONY: clean
clean:
	@echo "Cleaning..."
	@$(MAKE) -C $(KERNEL_DIR) clean
	@$(MAKE) -C $(USERLAND_DIR) clean
	@rm -rf $(BUILD_DIR)

# Show help
.PHONY: help
help:
	@echo "Obelisk OS Build System"
	@echo "======================="
	@echo ""
	@echo "Targets:"
	@echo "  all        - Build kernel and create ISO (default)"
	@echo "  kernel     - Build kernel only"
	@echo "  userland   - Build userland programs"
	@echo "  iso        - Create bootable ISO image"
	@echo "  run        - Run in QEMU"
	@echo "  run-gui    - Run in QEMU graphical mode"
	@echo "  run-sdl    - Run in QEMU with SDL display fallback"
	@echo "  run-kvm    - Run in QEMU with KVM acceleration"
	@echo "  debug      - Run in QEMU with GDB server"
	@echo "  clean      - Remove all build artifacts"
	@echo "  help       - Show this help message"
	@echo ""
	@echo "From Axioms, Order."

# Cross-compiler check
.PHONY: check-toolchain
check-toolchain:
	@echo "Checking toolchain..."
	@which x86_64-elf-gcc > /dev/null || \
		(echo "Error: x86_64-elf-gcc not found. Run tools/mkaxiomfs/cross-compile.sh" && exit 1)
	@echo "Toolchain OK"
