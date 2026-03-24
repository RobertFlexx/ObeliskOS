#
# Obelisk OS - Userland Makefile
# From Axioms, Order.
#

ifeq ($(CROSS_COMPILE),)
ifneq ($(shell command -v x86_64-elf-gcc 2>/dev/null),)
    CROSS_COMPILE := x86_64-elf-
else
    CROSS_COMPILE := /usr/bin/
endif
endif

CC := $(CROSS_COMPILE)gcc
CXX := $(CROSS_COMPILE)g++
AR := $(CROSS_COMPILE)ar

BUILDDIR := build
OUTDIR := $(BUILDDIR)/out
LIBC_DIR := libc
INIT_DIR := init
BIN_DIR := bin

LIBC_OBJS := \
	$(BUILDDIR)/libc/string.o \
	$(BUILDDIR)/libc/stdio.o \
	$(BUILDDIR)/libc/syscall.o

LIBC := $(BUILDDIR)/libc.a
INIT := $(OUTDIR)/init
SYSCTL := $(OUTDIR)/sysctl
INSTALLER := $(OUTDIR)/installer
INSTALLER_TUI := $(OUTDIR)/installer-tui
BUSYBOX := $(OUTDIR)/busybox
IDCPP := $(OUTDIR)/idcpp

PROGRAMS := $(INIT) $(SYSCTL) $(INSTALLER) $(INSTALLER_TUI) $(BUSYBOX) $(IDCPP)

CFLAGS := \
	-ffreestanding \
	-fno-stack-protector \
	-fno-pie \
	-fno-pic \
	-mno-red-zone \
	-mno-mmx \
	-mno-sse \
	-mno-sse2 \
	-mcmodel=small \
	-fno-tree-vectorize \
	-Wall \
	-Wextra \
	-Werror \
	-O2 \
	-g

CXXFLAGS := \
	$(CFLAGS) \
	-fno-exceptions \
	-fno-rtti

LDFLAGS := \
	-nostdlib \
	-static \
	-no-pie \
	-Wl,-e,_start

.PHONY: all clean
all: $(PROGRAMS)

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(OUTDIR): | $(BUILDDIR)
	@mkdir -p $(OUTDIR)

$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: %.cpp | $(BUILDDIR)
	@mkdir -p $(dir $@)
	@echo "  CXX     $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(LIBC): $(LIBC_OBJS)
	@echo "  AR      $@"
	@$(AR) rcs $@ $^

$(INIT): $(BUILDDIR)/init/init.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $<

$(SYSCTL): $(BUILDDIR)/bin/sysctl/sysctl.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $< $(LIBC)

$(INSTALLER): $(BUILDDIR)/bin/installer/installer.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $< $(LIBC)

$(INSTALLER_TUI): $(BUILDDIR)/bin/installer-tui/installer_tui.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $< $(LIBC)

$(BUSYBOX): $(BUILDDIR)/bin/busybox/busybox.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $< $(LIBC)

$(IDCPP): $(BUILDDIR)/bin/idcpp/idcpp.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CXX) $(LDFLAGS) -o $@ $< $(LIBC)

clean:
	@echo "  CLEAN"
	@rm -rf $(BUILDDIR)
