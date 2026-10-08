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
- `freebsd/aipu_linux.c` (`aipu_linux.ko`, BSD-2-Clause): `/dev/aipu` for
  Linux programs under the Linuxulator, such as CIX's binary-only `libnoe`:
  their ioctls, in Linux's encoding, run as FreeBSD's. mmap and poll need
  nothing more.
- `tools/aiputest/`: a test of `/dev/aipu` (capabilities, a buffer
  allocated, mapped, written, read back and freed).

## Memory

The driver allocates its buffers through the DMA API, under a 3 GB limit
(the Zhouyi v3's), and maps them for user space. The SMMU translates
underneath, through the IOMMU's busdma tag (the NPU is an IORT named
component): the driver's own IOMMU paths, which reach into Linux's private
IOVA structures, are never taken (no IOMMU group).

## Building

Against a freebsd-src tree with arm64 IOMMU support (named components) and
Sky1's SCMI performance protocol (`sky1_scmi`), both on the `sky1-iommu`
branch, and a drm-kmod checkout (for `dmabuf.ko`):

    tools/build.sh [objdir]

It needs `hw.iommu.dma=1` and `hw.smmu.bypass_named=0` for translation; the
driver works untranslated too. Translated, the SMMU must map DMA as Normal
memory (`sky1-iommu` 2427574f67): mapped as Device memory, as FreeBSD's SMMU
driver did, the NPU runs ten times slower.

## Clock

The NPU's clock is an SCMI performance domain (`"perf"` in its `_DSD`
`power-domains`), which the firmware starts at its top level:

    sysctl dev.aipu.0.freq_levels      # 400 600 800 1200 (MHz)
    sysctl dev.aipu.0.freq=800

as CIX's driver offers through devfreq's userspace governor. MobileNetV2
takes 1.46, 1.05, 0.84 and 0.72 ms at those levels.

## State

The plan is in AbyssBSD `docs/boards/orangepi-6-plus/npu.md`. The driver
attaches, reports the NPU (Zhouyi v3, one cluster of three cores, 4 MB of
GM), and runs inference: CIX's MobileNetV2 in about 7.3 ms, through Arm
China's user-mode driver built for FreeBSD
([aipu-umd](https://github.com/JamesKane/aipu-umd)), or under the
Linuxulator through CIX's `libnoe` (`kldload aipu_linux`).
