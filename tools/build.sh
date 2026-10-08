#!/bin/sh
# Cross-build aipu.ko for arm64 on macOS, against a freebsd-src tree's
# GENERIC-HMP-IOMMU kernel and a drm-kmod checkout (for dma-buf).
# Usage: tools/build.sh [objdir]   (log: <objdir>/build.log)
X=/Users/jkane/Projects/OS/build/xbin
SRC=/Users/jkane/Projects/OS/freebsd-src
KMOD=$(cd "$(dirname "$0")/.." && pwd)
OBJ=${1:-/Users/jkane/Projects/OS/build/aipuobj}
KERN=/Users/jkane/Projects/OS/build/fbobj$SRC/arm64.aarch64/sys/GENERIC-HMP-IOMMU
mkdir -p "$OBJ"
cd $SRC || exit 1
MAKEOBJDIRPREFIX=/Users/jkane/Projects/OS/build/fbobj NM=$X/llvm-nm XNM=$X/llvm-nm \
python3 tools/build/make.py --cross-bindir=$X TARGET=arm64 TARGET_ARCH=aarch64 \
 -DWITHOUT_CLANG_BOOTSTRAP -DWITHOUT_LLD_BOOTSTRAP -DWITHOUT_LLVM_BINUTILS_BOOTSTRAP -DWITHOUT_ELFTOOLCHAIN_BOOTSTRAP \
 NM=$X/llvm-nm XNM=$X/llvm-nm OBJCOPY=$X/llvm-objcopy XOBJCOPY=$X/llvm-objcopy SIZE=$X/llvm-size \
 buildenv BUILDENV_SHELL="make -k -C $KMOD -j16 DRMKMOD=/Users/jkane/Projects/OS/drm-kmod SYSDIR=$SRC/sys KERNBUILDDIR=$KERN MAKEOBJDIRPREFIX=$OBJ DEBUG_FLAGS=-g" \
 > "$OBJ/build.log" 2>&1 || { grep -E ' error: |Error code|\.error' "$OBJ/build.log" | head -40; exit 1; }
echo built
