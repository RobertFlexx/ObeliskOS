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
DMD ?= dmd
DFLAGS := -betterC -m64 -O -release -boundscheck=off

BUILDDIR := build
OUTDIR := $(BUILDDIR)/out
LIBC_DIR := libc
INIT_DIR := init
INITD_DIR := initd
BIN_DIR := bin

LIBC_OBJS := \
	$(BUILDDIR)/libc/string.o \
	$(BUILDDIR)/libc/stdio.o \
	$(BUILDDIR)/libc/syscall.o

LIBC := $(BUILDDIR)/libc.a
INIT := $(OUTDIR)/init
OBELISKD := $(OUTDIR)/obeliskd
SYSCTL := $(OUTDIR)/sysctl
INSTALLER := $(OUTDIR)/installer
INSTALLER_TUI := $(OUTDIR)/installer-tui
ROCKBOX := $(OUTDIR)/rockbox
IDCPP := $(OUTDIR)/idcpp
STATCPP := $(OUTDIR)/statcpp
CREDPROBE := $(OUTDIR)/credprobe
SETUIDCHECK := $(OUTDIR)/setuidcheck
EXECPROBE := $(OUTDIR)/execprobe
TRAVERSEPROBE := $(OUTDIR)/traverseprobe
MKSTATPROBE := $(OUTDIR)/mkstatprobe
OPKG := $(OUTDIR)/opkg
DPROBE := $(OUTDIR)/dprobe
PING := $(OUTDIR)/ping
NSLOOKUP := $(OUTDIR)/nslookup
CURL := $(OUTDIR)/curl
FETCH := $(OUTDIR)/fetch
FBINFO := $(OUTDIR)/fbinfo
XORG_SMOKE := $(OUTDIR)/xorg-smoke
DESKTOP_SESSION := $(OUTDIR)/desktop-session

PROGRAMS := $(INIT) $(OBELISKD) $(SYSCTL) $(INSTALLER) $(INSTALLER_TUI) $(ROCKBOX) $(IDCPP) $(STATCPP) $(CREDPROBE) $(SETUIDCHECK) $(EXECPROBE) $(TRAVERSEPROBE) $(MKSTATPROBE) $(OPKG) $(DPROBE) $(PING) $(NSLOOKUP) $(CURL) $(FETCH) $(FBINFO) $(XORG_SMOKE) $(DESKTOP_SESSION)

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

$(BUILDDIR)/%.o: %.d | $(BUILDDIR)
	@mkdir -p $(dir $@)
	@echo "  DMD     $<"
	@$(DMD) $(DFLAGS) -c $< -of$@

$(LIBC): $(LIBC_OBJS)
	@echo "  AR      $@"
	@$(AR) rcs $@ $^

$(INIT): $(BUILDDIR)/init/init.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $<

$(OBELISKD): $(BUILDDIR)/initd/main.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $< $(LIBC)

$(SYSCTL): $(BUILDDIR)/bin/sysctl/sysctl.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $< $(LIBC)

$(INSTALLER): $(BUILDDIR)/bin/installer/installer.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $< $(LIBC)

$(INSTALLER_TUI): $(BUILDDIR)/bin/installer-tui/installer_tui.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $< $(LIBC)

$(ROCKBOX): $(BUILDDIR)/bin/rockbox/rockbox.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $< $(LIBC)

$(IDCPP): $(BUILDDIR)/bin/idcpp/idcpp.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CXX) $(LDFLAGS) -o $@ $< $(LIBC)

$(STATCPP): $(BUILDDIR)/bin/statcpp/statcpp.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CXX) $(LDFLAGS) -o $@ $< $(LIBC)

$(CREDPROBE): $(BUILDDIR)/bin/credprobe/credprobe.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CXX) $(LDFLAGS) -o $@ $< $(LIBC)

$(SETUIDCHECK): $(BUILDDIR)/bin/setuidcheck/setuidcheck.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CXX) $(LDFLAGS) -o $@ $< $(LIBC)

$(EXECPROBE): $(BUILDDIR)/bin/execprobe/execprobe.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CXX) $(LDFLAGS) -o $@ $< $(LIBC)

$(TRAVERSEPROBE): $(BUILDDIR)/bin/traverseprobe/traverseprobe.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CXX) $(LDFLAGS) -o $@ $< $(LIBC)

$(MKSTATPROBE): $(BUILDDIR)/bin/mkstatprobe/mkstatprobe.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CXX) $(LDFLAGS) -o $@ $< $(LIBC)

$(OPKG): $(BUILDDIR)/bin/opkg/opkg.o $(BUILDDIR)/bin/opkg/opkg_d.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $^

$(DPROBE): $(BUILDDIR)/bin/dprobe/dprobe.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $^

$(PING): $(BUILDDIR)/bin/ping/ping.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $< $(LIBC)

$(NSLOOKUP): $(BUILDDIR)/bin/nslookup/nslookup.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $< $(LIBC)

$(CURL): $(BUILDDIR)/bin/curl/curl.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $< $(LIBC)

$(FETCH): $(BUILDDIR)/bin/fetch/fetch.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $< $(LIBC)

$(FBINFO): $(BUILDDIR)/bin/fbinfo/fbinfo.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $< $(LIBC)

$(XORG_SMOKE): $(BUILDDIR)/bin/xorg-smoke/xorg_smoke.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $< $(LIBC)

$(DESKTOP_SESSION): $(BUILDDIR)/bin/desktop-session/desktop_session.o $(LIBC) | $(OUTDIR)
	@echo "  LD      $@"
	@$(CC) $(LDFLAGS) -o $@ $< $(LIBC)

clean:
	@echo "  CLEAN"
	@rm -rf $(BUILDDIR)
