#!/bin/bash
#
# Obelisk OS - Cross-Compiler Build Script
# From Axioms, Order.
#
# This script builds a cross-compiler toolchain for x86_64-elf.
#

set -e

# Configuration
export PREFIX="$HOME/opt/cross"
export TARGET=x86_64-elf
export PATH="$PREFIX/bin:$PATH"

# Versions
BINUTILS_VERSION=2.41
GCC_VERSION=13.2.0

# Directories
WORKDIR="$HOME/src/cross-build"
mkdir -p "$WORKDIR"
cd "$WORKDIR"

echo "=========================================="
echo "Obelisk OS Cross-Compiler Build"
echo "=========================================="
echo ""
echo "Target: $TARGET"
echo "Prefix: $PREFIX"
echo ""

# Check dependencies
echo "Checking dependencies..."
for cmd in wget tar make gcc g++ bison flex gmp mpfr mpc texinfo; do
    if ! command -v $cmd &> /dev/null; then
        echo "Error: $cmd is required but not installed."
        exit 1
    fi
done
echo "All dependencies found."
echo ""

# Download sources
download_if_missing() {
    local url=$1
    local file=$(basename $url)

    if [ ! -f "$file" ]; then
        echo "Downloading $file..."
        wget -q "$url"
    else
        echo "$file already exists, skipping download."
    fi
}

echo "Downloading sources..."
download_if_missing "https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VERSION}.tar.xz"
download_if_missing "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz"

# Extract sources
extract_if_needed() {
    local archive=$1
    local dir=${archive%.tar.xz}

    if [ ! -d "$dir" ]; then
        echo "Extracting $archive..."
        tar xf "$archive"
    else
        echo "$dir already exists, skipping extraction."
    fi
}

echo ""
echo "Extracting sources..."
extract_if_needed "binutils-${BINUTILS_VERSION}.tar.xz"
extract_if_needed "gcc-${GCC_VERSION}.tar.xz"

# Build binutils
echo ""
echo "Building binutils..."
mkdir -p build-binutils
cd build-binutils

../binutils-${BINUTILS_VERSION}/configure \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --with-sysroot \
    --disable-nls \
    --disable-werror

make -j$(nproc)
make install

cd ..

# Build GCC
echo ""
echo "Building GCC (this may take a while)..."
mkdir -p build-gcc
cd build-gcc

../gcc-${GCC_VERSION}/configure \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --disable-nls \
    --enable-languages=c \
    --without-headers

make -j$(nproc) all-gcc
make -j$(nproc) all-target-libgcc
make install-gcc
make install-target-libgcc

cd ..

echo ""
echo "=========================================="
echo "Cross-compiler built successfully!"
echo "=========================================="
echo ""
echo "Add the following to your shell profile:"
echo "  export PATH=\"$PREFIX/bin:\$PATH\""
echo ""
echo "Then you can build Obelisk OS with:"
echo "  make"
echo ""
echo "From Axioms, Order."
