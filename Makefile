#
# Obelisk OS - Top Level Makefile
# From Axioms, Order.
#

# Directories
KERNEL_DIR := kernel
USERLAND_DIR := userland
TOOLS_DIR := tools
DIAG_DIR := $(TOOLS_DIR)/diagnostics
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
QEMU_NET_FLAGS ?= -nic user,model=e1000
QEMU_DISPLAY ?= gtk,gl=off,zoom-to-fit=off
QEMU_VIDEO ?= virtio-vga
QEMU_GRAPHICAL_FLAGS := -device $(QEMU_VIDEO) -display $(QEMU_DISPLAY)
QEMU_GUI_SERIAL_FLAGS := -serial mon:stdio
QEMU_SERIAL_FLAGS := -nographic -monitor none -chardev stdio,mux=on,signal=off,id=char0 -serial chardev:char0

# Host userland import settings (optional host tools)
IMPORT_HOST_USERLAND ?= 0
EXPERIMENTAL_DYNAMIC_ELF ?= 0
HOST_COREUTILS_BINS ?= ls cat cp mv rm mkdir rmdir ln chmod chown pwd uname date head tail wc sort uniq cut tr tee sleep true false env printenv id whoami users
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
	@$(MAKE) -C $(KERNEL_DIR) EXPERIMENTAL_DYNAMIC_ELF=$(EXPERIMENTAL_DYNAMIC_ELF)
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
	@rm -rf "$(ROOTFS_DIR)"
	@mkdir -p $(ROOTFS_DIR)/sbin $(ROOTFS_DIR)/bin $(ROOTFS_DIR)/usr/bin $(ROOTFS_DIR)/usr/lib $(ROOTFS_DIR)/usr/lib64 $(ROOTFS_DIR)/lib $(ROOTFS_DIR)/lib64 $(ROOTFS_DIR)/etc/axiomd/policy $(ROOTFS_DIR)/etc $(ROOTFS_DIR)/var/log $(ROOTFS_DIR)/tmp $(ROOTFS_DIR)/proc $(ROOTFS_DIR)/dev $(ROOTFS_DIR)/home/obelisk
	@chmod 1777 $(ROOTFS_DIR)/tmp
	@chmod 1777 $(ROOTFS_DIR)/tmp
	@cp $(USERLAND_OUT_DIR)/obeliskd $(ROOTFS_DIR)/sbin/init
	@cp $(USERLAND_OUT_DIR)/init $(ROOTFS_DIR)/sbin/init-legacy
	@cp $(USERLAND_OUT_DIR)/sysctl $(ROOTFS_DIR)/sbin/sysctl
	@cp $(USERLAND_OUT_DIR)/installer $(ROOTFS_DIR)/sbin/installer
	@cp $(USERLAND_OUT_DIR)/installer-tui $(ROOTFS_DIR)/sbin/installer-tui
	@cp $(USERLAND_DIR)/axiomd/main.pro $(ROOTFS_DIR)/etc/axiomd/main.pro
	@cp $(USERLAND_DIR)/axiomd/kernel_ipc.pro $(ROOTFS_DIR)/etc/axiomd/kernel_ipc.pro
	@cp $(USERLAND_DIR)/axiomd/sandbox.pro $(ROOTFS_DIR)/etc/axiomd/sandbox.pro
	@cp $(USERLAND_DIR)/axiomd/policy/access.pro $(ROOTFS_DIR)/etc/axiomd/policy/access.pro
	@cp $(USERLAND_DIR)/axiomd/policy/allocation.pro $(ROOTFS_DIR)/etc/axiomd/policy/allocation.pro
	@cp $(USERLAND_DIR)/axiomd/policy/inheritance.pro $(ROOTFS_DIR)/etc/axiomd/policy/inheritance.pro
	@for tool in rockbox osh sh su sudo idcpp statcpp credprobe setuidcheck execprobe traverseprobe mkstatprobe opkg dprobe ping nslookup curl fetch fbinfo xorg-smoke desktop-session oed ls cat cp mv rm mkdir ln chmod chown sync mount umount dmesg ps kill grep awk sed tar find time; do \
		if [ -f "$(USERLAND_OUT_DIR)/$$tool" ]; then \
			cp "$(USERLAND_OUT_DIR)/$$tool" "$(ROOTFS_DIR)/bin/$$tool"; \
		fi; \
	done
	@if [ -f "$(ROOTFS_DIR)/bin/rockbox" ] && [ ! -f "$(ROOTFS_DIR)/bin/osh" ]; then \
		cp "$(ROOTFS_DIR)/bin/rockbox" "$(ROOTFS_DIR)/bin/osh"; \
	fi
	@if [ -f "$(ROOTFS_DIR)/bin/rockbox" ] && [ ! -f "$(ROOTFS_DIR)/bin/sh" ]; then \
		cp "$(ROOTFS_DIR)/bin/rockbox" "$(ROOTFS_DIR)/bin/sh"; \
	fi
	@if [ -f "$(ROOTFS_DIR)/bin/rockbox" ] && [ ! -f "$(ROOTFS_DIR)/bin/su" ]; then \
		cp "$(ROOTFS_DIR)/bin/rockbox" "$(ROOTFS_DIR)/bin/su"; \
	fi
	@if [ -f "$(ROOTFS_DIR)/bin/rockbox" ] && [ ! -f "$(ROOTFS_DIR)/bin/sudo" ]; then \
		cp "$(ROOTFS_DIR)/bin/rockbox" "$(ROOTFS_DIR)/bin/sudo"; \
	fi
	@if [ -f "$(ROOTFS_DIR)/bin/rockbox" ]; then \
		for app in ls cat cp mv rm mkdir rmdir ln chmod chown pwd whoami id users uname date head tail wc sort uniq cut tr tee sleep true false env printenv find time; do \
			if [ ! -f "$(ROOTFS_DIR)/bin/$$app" ]; then \
				cp "$(ROOTFS_DIR)/bin/rockbox" "$(ROOTFS_DIR)/bin/$$app"; \
			fi; \
		done; \
	fi
	@for app in osh sh rockbox ls cat cp mv rm mkdir rmdir ln chmod chown pwd whoami id users uname date head tail wc cut true false env printenv stat opkg dprobe ping nslookup curl fetch fbinfo xorg-smoke desktop-session oed find time; do \
		if [ -f "$(ROOTFS_DIR)/bin/$$app" ]; then \
			cp "$(ROOTFS_DIR)/bin/$$app" "$(ROOTFS_DIR)/usr/bin/$$app"; \
		fi; \
	done
	@if [ -d "$(ROOTFS_OVERLAY_DIR)" ]; then \
		echo "Applying rootfs overlay from $(ROOTFS_OVERLAY_DIR)..."; \
		cp -a "$(ROOTFS_OVERLAY_DIR)/." "$(ROOTFS_DIR)/"; \
	fi
	@mkdir -p "$(ROOTFS_DIR)/etc/ssl/certs"
	@if [ -f "/etc/ssl/certs/ca-certificates.crt" ]; then \
		cp "/etc/ssl/certs/ca-certificates.crt" "$(ROOTFS_DIR)/etc/ssl/certs/ca-certificates.crt"; \
	elif [ -f "/etc/pki/tls/certs/ca-bundle.crt" ]; then \
		cp "/etc/pki/tls/certs/ca-bundle.crt" "$(ROOTFS_DIR)/etc/ssl/certs/ca-certificates.crt"; \
	fi
	@mkdir -p "$(ROOTFS_DIR)/var/cache/opkg/repo/packages"
	@if [ -d "opkg/examples/samplepkg" ]; then \
		echo "Preparing bundled opkg demo repository..."; \
		if [ ! -x "opkg/opkg" ]; then \
			(cd opkg && dub build); \
		fi; \
		./opkg/opkg build opkg/examples/samplepkg "$(ROOTFS_DIR)/var/cache/opkg/repo/packages/hello-sample-1.0.0-x86_64.opk"; \
		if [ -d "opkg/examples/grep" ]; then \
			./opkg/opkg build opkg/examples/grep "$(ROOTFS_DIR)/var/cache/opkg/repo/packages/grep-1.0.0-x86_64.opk"; \
		fi; \
		if [ -d "opkg/examples/sed" ]; then \
			./opkg/opkg build opkg/examples/sed "$(ROOTFS_DIR)/var/cache/opkg/repo/packages/sed-1.0.0-x86_64.opk"; \
		fi; \
		if [ -d "opkg/examples/xorg" ]; then \
			./opkg/opkg build opkg/examples/xorg "$(ROOTFS_DIR)/var/cache/opkg/repo/packages/xorg-1.0.0-x86_64.opk"; \
		fi; \
		if [ -d "opkg/examples/xfce" ]; then \
			./opkg/opkg build opkg/examples/xfce "$(ROOTFS_DIR)/var/cache/opkg/repo/packages/xfce-1.0.0-x86_64.opk"; \
		fi; \
		./opkg/opkg repo index "$(ROOTFS_DIR)/var/cache/opkg/repo"; \
	fi
	@if [ "$(IMPORT_HOST_USERLAND)" = "1" ] && [ -x "$(HOST_USERLAND_IMPORT_SCRIPT)" ]; then \
		"$(HOST_USERLAND_IMPORT_SCRIPT)" "$(ROOTFS_DIR)" "" $(HOST_COREUTILS_BINS); \
	fi
	@rm -f "$(ROOTFS_DIR)/README.txt"
	@printf "%s\n" \
		"Obelisk OS early userspace image" \
		"Use /sbin/installer or /sbin/installer-tui to stage an installation." \
		"Static-only default image. Host tool import is disabled unless IMPORT_HOST_USERLAND=1." \
		> "$(ROOTFS_DIR)/etc/motd"
	@tar -C $(ROOTFS_DIR) -cf $(ROOTFS_TAR) .

# Create ISO image
.PHONY: iso
iso: kernel rootfs
	@echo "Creating ISO image..."
	@command -v grub-mkrescue >/dev/null || (echo "Error: grub-mkrescue is required" && exit 1)
	@XORRISO_PATH="$$(command -v xorriso || true)"; \
	if [ -z "$$XORRISO_PATH" ]; then \
		for cand in "/home/linuxbrew/.linuxbrew/bin/xorriso" "/home/robertsolus/.linuxbrew/bin/xorriso" "/opt/homebrew/bin/xorriso"; do \
			if [ -x "$$cand" ]; then \
				XORRISO_PATH="$$cand"; \
				break; \
			fi; \
		done; \
	fi; \
	[ -n "$$XORRISO_PATH" ] || (echo "Error: xorriso is required by grub-mkrescue" && exit 1); \
	echo "Using xorriso: $$XORRISO_PATH"; \
	PATH="$$(dirname "$$XORRISO_PATH"):$$PATH" grub-mkrescue --version >/dev/null
	@mkdir -p $(ISO_DIR)/boot/grub
	@cp $(KERNEL) $(ISO_DIR)/boot/
	@cp $(ROOTFS_TAR) $(ISO_DIR)/boot/
	@cp $(GRUB_CFG) $(ISO_DIR)/boot/grub/
	@XORRISO_PATH="$$(command -v xorriso || true)"; \
	if [ -z "$$XORRISO_PATH" ]; then \
		for cand in "/home/linuxbrew/.linuxbrew/bin/xorriso" "/home/robertsolus/.linuxbrew/bin/xorriso" "/opt/homebrew/bin/xorriso"; do \
			if [ -x "$$cand" ]; then \
				XORRISO_PATH="$$cand"; \
				break; \
			fi; \
		done; \
	fi; \
	PATH="$$(dirname "$$XORRISO_PATH"):$$PATH" grub-mkrescue -o $(ISO) $(ISO_DIR)
	@echo "ISO created: $(ISO)"

# Run in QEMU (default: robust serial mode)
.PHONY: run
run: iso
	@echo "Starting QEMU (default serial console mode)..."
	@echo "Ctrl+C is sent to guest; quit with Ctrl+A then X"
	@$(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_NET_FLAGS) $(QEMU_SERIAL_FLAGS) -cdrom $(ISO)

# Run in QEMU graphical mode
.PHONY: run-gui
run-gui: iso
	@echo "Starting QEMU (GUI + terminal serial log mirror)..."
	@$(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_NET_FLAGS) $(QEMU_GRAPHICAL_FLAGS) $(QEMU_GUI_SERIAL_FLAGS) -cdrom $(ISO)

.PHONY: run-sdl
run-sdl: iso
	@echo "Starting QEMU (SDL display fallback + terminal serial mirror)..."
	@$(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_NET_FLAGS) -device $(QEMU_VIDEO) -display sdl $(QEMU_GUI_SERIAL_FLAGS) -cdrom $(ISO)

# Run with terminal-attached serial console
.PHONY: run-serial
run-serial: iso
	@echo "Starting QEMU with terminal serial console..."
	@echo "Tip: Ctrl+C now goes to guest UART, not host kill."
	@echo "QEMU quit keys: Ctrl+A then X"
	@$(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_NET_FLAGS) $(QEMU_SERIAL_FLAGS) -cdrom $(ISO)

# Run with KVM acceleration
.PHONY: run-kvm
run-kvm: iso
	@echo "Starting QEMU with KVM (GUI + terminal serial log mirror)..."
	@$(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_NET_FLAGS) $(QEMU_GRAPHICAL_FLAGS) $(QEMU_GUI_SERIAL_FLAGS) -enable-kvm -cpu host -cdrom $(ISO)

# Debug with GDB
.PHONY: debug
debug: iso
	@echo "Starting QEMU in debug mode..."
	@echo "Connect GDB with: target remote localhost:1234"
	@$(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_NET_FLAGS) $(QEMU_GRAPHICAL_FLAGS) $(QEMU_DEBUG_FLAGS) -cdrom $(ISO)

# Run with verbose logging
.PHONY: run-verbose
run-verbose: iso
	@$(QEMU) $(QEMU_BASE_FLAGS) $(QEMU_NET_FLAGS) $(QEMU_SERIAL_FLAGS) -d int -cdrom $(ISO) 2>&1 | head -1000

# Diagnostics helpers
.PHONY: diag-capture
diag-capture:
	@bash "$(DIAG_DIR)/capture-run-serial.sh" "$(or $(TIMEOUT),180)"

.PHONY: diag-repro
diag-repro:
	@REPRO_CMD="$(or $(CMD),sudo ls)" BOOT_WAIT_SEC="$(or $(BOOT_WAIT),25)" POST_CMD_WAIT_SEC="$(or $(POST_WAIT),10)" \
		bash "$(DIAG_DIR)/repro-run-serial.sh" "$(or $(TIMEOUT),240)"

.PHONY: diag-extract
diag-extract:
	@if [ -z "$(LOG)" ]; then \
		echo "Usage: make diag-extract LOG=build/diagnostics/<run-log>.log"; \
		exit 1; \
	fi
	@bash "$(DIAG_DIR)/extract-panic.sh" "$(LOG)"

.PHONY: diag-symbolicate
diag-symbolicate:
	@if [ -z "$(LOG)" ]; then \
		echo "Usage: make diag-symbolicate LOG=build/diagnostics/<run-log>.log [KERNEL_ELF=build/kernel.elf]"; \
		exit 1; \
	fi
	@bash "$(DIAG_DIR)/symbolicate-panic.sh" "$(or $(KERNEL_ELF),$(KERNEL))" "$(LOG)"

.PHONY: diag-triage
diag-triage:
	@bash "$(DIAG_DIR)/triage-panic.sh" "$(or $(TIMEOUT),180)"

.PHONY: diag-triage-repro
diag-triage-repro:
	@MODE=repro REPRO_CMD="$(or $(CMD),sudo ls)" BOOT_WAIT_SEC="$(or $(BOOT_WAIT),25)" POST_CMD_WAIT_SEC="$(or $(POST_WAIT),10)" \
		bash "$(DIAG_DIR)/triage-panic.sh" "$(or $(TIMEOUT),240)"

.PHONY: validate-regression
validate-regression:
	@bash "$(DIAG_DIR)/smoke-regression.sh" "$(or $(TIMEOUT),200)"

.PHONY: demo-smoke
demo-smoke:
	@bash "$(DIAG_DIR)/demo-smoke.sh" "$(or $(TIMEOUT),180)"

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
	@echo "  diag-capture     - Capture a serial run log (TIMEOUT=<sec>)"
	@echo "  diag-repro       - Capture serial run with injected CMD (default: sudo ls)"
	@echo "  diag-extract     - Extract last panic block (LOG=<path>)"
	@echo "  diag-symbolicate - Map panic addresses to symbols (LOG=<path>)"
	@echo "  diag-triage      - Capture + extract + symbolicate in one step"
	@echo "  diag-triage-repro - Repro capture + extract + symbolicate in one step"
	@echo "  validate-regression - Run boot/userland smoke regression suite"
	@echo "  demo-smoke   - Run a short guided demo command sequence"
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
