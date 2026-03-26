#
# Obelisk OS - Kernel Makefile
# From Axioms, Order.
#

# Architecture
ARCH := x86_64
EXPERIMENTAL_DYNAMIC_ELF ?= 1

# Toolchain
ifeq ($(CROSS_COMPILE),)
ifneq ($(shell command -v x86_64-elf-gcc 2>/dev/null),)
    CROSS_COMPILE := x86_64-elf-
else
    CROSS_COMPILE := /usr/bin/
endif
endif

CC := $(CROSS_COMPILE)gcc
AS := $(CROSS_COMPILE)as
LD := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
ZIG ?= zig

# Directories
SRCDIR := .
BUILDDIR := build
ARCH_DIR := arch/$(ARCH)

# Output
KERNEL := $(BUILDDIR)/kernel.elf
KERNEL_BIN := $(BUILDDIR)/kernel.bin
KERNEL_MAP := $(BUILDDIR)/kernel.map

# Include paths
INCLUDES := \
    -I$(SRCDIR)/include \
    -I$(SRCDIR)/$(ARCH_DIR)/include \
    -I$(SRCDIR)/mm/include \
    -I$(SRCDIR)/proc/include \
    -I$(SRCDIR)/fs/include \
    -I$(SRCDIR)/fs/axiomfs/include \
    -I$(SRCDIR)/ipc/include \
    -I$(SRCDIR)/sysctl/include

# Compiler flags
CFLAGS := \
    -ffreestanding \
    -fno-stack-protector \
    -fno-pie \
    -fno-pic \
    -mno-red-zone \
    -mno-mmx \
    -mno-sse \
    -mno-sse2 \
    -mcmodel=large \
    -Wall \
    -Wextra \
    -Wno-cast-function-type \
    -std=gnu11 \
    -O2 \
    -g \
    -DCONFIG_EXPERIMENTAL_DYNAMIC_ELF=$(EXPERIMENTAL_DYNAMIC_ELF) \
    $(INCLUDES)

# Assembler flags
ASFLAGS := \
    -g

# Linker flags
LDFLAGS := \
    -nostdlib \
    -static \
    -z max-page-size=0x1000 \
    -T $(SRCDIR)/linker.ld \
    -Map=$(KERNEL_MAP)

# Source files
C_SOURCES := \
    $(ARCH_DIR)/boot/gdt.c \
    $(ARCH_DIR)/boot/idt.c \
    $(ARCH_DIR)/cpu.c \
    $(ARCH_DIR)/io/port.c \
    $(ARCH_DIR)/io/uart.c \
    $(ARCH_DIR)/mm/paging.c \
    $(ARCH_DIR)/mm/physmem.c \
    $(ARCH_DIR)/process/syscall.c \
    drivers/pci.c \
    net/core.c \
    net/e1000.c \
    net/virtio_net.c \
    mm/pmm.c \
    mm/vmm.c \
    mm/kmalloc.c \
    kernel/main.c \
    kernel/bootinfo.c \
    kernel/initramfs.c \
    kernel/time.c \
    kernel/panic.c \
    kernel/printk.c \
    kernel/power.c \
    kernel/kthread.c \
    proc/process.c \
    proc/scheduler.c \
    proc/fork.c \
    proc/exec.c \
    fs/vfs.c \
    fs/inode.c \
    fs/dentry.c \
    fs/file.c \
    fs/axiomfs/super.c \
    fs/axiomfs/inode.c \
    fs/axiomfs/dir.c \
    fs/axiomfs/file.c \
    fs/axiomfs/policy_ipc.c \
    fs/axiomfs/cache.c \
    fs/devfs/devfs.c \
    ipc/msgqueue.c \
    ipc/axiomd_channel.c \
    ipc/shm.c \
    sysctl/sysctl.c \
    sysctl/nodes.c \
    sysctl/handlers.c \
    lib/string.c \
    lib/printf.c \
    lib/bitmap.c \
    lib/rbtree.c

ZIG_SOURCES := \
    support/elf_validate.zig \
    support/initramfs_scan.zig \
    support/path_validate.zig \
    support/mul_overflow.zig

ASM_SOURCES := \
    $(ARCH_DIR)/boot/boot.S \
    $(ARCH_DIR)/boot/interrupt.S \
    $(ARCH_DIR)/process/context.S

# Object files
C_OBJECTS := $(patsubst %.c,$(BUILDDIR)/%.o,$(C_SOURCES))
ASM_OBJECTS := $(patsubst %.S,$(BUILDDIR)/%.o,$(ASM_SOURCES))
ZIG_OBJECTS := $(patsubst %.zig,$(BUILDDIR)/%.o,$(ZIG_SOURCES))
OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS) $(ZIG_OBJECTS)

# Dependency files
DEPS := $(C_OBJECTS:.o=.d)

# Default target
.PHONY: all
all: $(KERNEL)

# Create build directories
$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

# Generate directory structure for object files
$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILDDIR)/%.o: %.S | $(BUILDDIR)
	@mkdir -p $(dir $@)
	@echo "  AS      $<"
	@$(AS) $(ASFLAGS) $< -o $@

$(BUILDDIR)/%.o: %.zig | $(BUILDDIR)
	@mkdir -p $(dir $@)
	@echo "  ZIG     $<"
	@$(ZIG) build-obj $< -target x86_64-freestanding-none -O ReleaseSmall -mcmodel=large -femit-bin=$@

# Link kernel
$(KERNEL): $(OBJECTS)
	@echo "  LD      $@"
	@$(LD) $(LDFLAGS) -o $@ $(OBJECTS)
	@echo "  OBJCOPY $@"
	@$(OBJCOPY) --only-keep-debug $@ $(KERNEL).debug
	@$(OBJCOPY) --strip-debug $@

# Generate disassembly
.PHONY: disasm
disasm: $(KERNEL)
	@$(OBJDUMP) -d $< > $(BUILDDIR)/kernel.asm

# Clean
.PHONY: clean
clean:
	@echo "  CLEAN"
	@rm -rf $(BUILDDIR)

# Include dependencies
-include $(DEPS)

# Print variables (for debugging)
.PHONY: vars
vars:
	@echo "CC = $(CC)"
	@echo "CFLAGS = $(CFLAGS)"
	@echo "SOURCES = $(C_SOURCES)"
	@echo "OBJECTS = $(OBJECTS)"
