# aipu-kmod

The Arm China Zhouyi NPU driver (`aipu`, `/dev/aipu`) for FreeBSD, on the
CIX Sky1 (Orange Pi 6 Plus, Radxa Orion O6): CIX's Linux driver through
LinuxKPI, out of tree.

- `driver/`: CIX's driver 6.2.0-1 (GPL-2.0;
  [cix_opensource__npu_driver](https://github.com/cixtech/cix_opensource__npu_driver),
  branch `cix_mainline_dev`), imported unmodified, then changed only where
  FreeBSD needs it (`#ifdef __FreeBSD__`) or where it is a bug everywhere.
- `freebsd/`: the glue (BSD-2-Clause), in place of CIX's `sky1/sky1.c`:
  the ACPI device (`CIXH4000`) and its cores powered through their ACPI
  power resources, the platform device the driver attaches to, ACPI `_DSD`
  properties, and DMA buffers mapped write-combined, as Linux maps them for
  a device that does not snoop the CPU caches.
- `tools/aiputest/`: a test of `/dev/aipu` (capabilities, a buffer
  allocated, mapped, written, read back and freed).

## Memory

The driver allocates its buffers through the DMA API, under a 3 GB limit
(the Zhouyi v3's), and maps them for user space. The SMMU translates
underneath, through the IOMMU's busdma tag (the NPU is an IORT named
component): the driver's own IOMMU paths, which reach into Linux's private
IOVA structures, are never taken (no IOMMU group).

## Building

Against a freebsd-src tree with arm64 IOMMU support (named components, the
`sky1-iommu` branch) and a drm-kmod checkout (for `dmabuf.ko`):

    tools/build.sh [objdir]

It needs `hw.iommu.dma=1` and `hw.smmu.bypass_named=0` for translation; the
driver works untranslated too.

## State

Phase 1 of the plan (AbyssBSD `docs/boards/orangepi-6-plus/npu.md`): the
driver attaches, reports the NPU (Zhouyi v3, one cluster of three cores,
4 MB of GM), and buffers allocate, map and free. No job has run yet: that
needs Arm China's user-mode driver and a compiled graph (phase 2). No
devfreq: the firmware's clock.
