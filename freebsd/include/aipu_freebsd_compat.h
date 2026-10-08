/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * What the Zhouyi NPU driver needs from Linux that LinuxKPI lacks, included
 * before every source file.
 */

#ifndef _AIPU_FREEBSD_COMPAT_H_
#define	_AIPU_FREEBSD_COMPAT_H_

/* Not for the FreeBSD bus glue, which includes no Linux headers. */
#ifndef AIPU_FBSD_BUS

#include <linux/ioport.h>
#include <linux/pci.h>		/* IORESOURCE_MEM, as LinuxKPI's platform bus */
#include <linux/printk.h>
#include <linux/version.h>

/* The kernel API level: drm-kmod's (LINUXKPI_VERSION). */
#ifndef KERNEL_VERSION
#define	KERNEL_VERSION(a, b, c)	(((a) << 16) + ((b) << 8) + (c))
#endif
#ifndef LINUX_VERSION_CODE
#define	LINUX_VERSION_CODE	KERNEL_VERSION(LINUXKPI_VERSION / 10000, \
				    LINUXKPI_VERSION / 100 % 100, 0)
#endif

/* Linux's values; LinuxKPI's request_irq() looks at IRQF_SHARED only. */
#define	IRQF_PROBE_SHARED	0x00000100
#define	IRQF_ONESHOT		0x00002000

/* kmem_cache_create(): the driver checks for NULL itself. */
#define	SLAB_PANIC		0

/*
 * The NPU's registers are the FreeBSD device's bus resource already:
 * nothing to claim.
 */
#define	request_mem_region(start, n, name)	((void *)1)
#define	release_mem_region(start, n)		do { } while (0)

struct device;
struct device_link;

/* ACPI _DSD integer properties (aipu_freebsd.c). */
int	device_property_count_u32(struct device *dev, const char *propname);
int	device_property_read_u32(struct device *dev, const char *propname,
	    u32 *val);
int	device_property_read_u32_array(struct device *dev, const char *propname,
	    u32 *val, size_t nval);

/*
 * DMA buffers, mapped as Linux maps them for a device that does not snoop
 * the CPU caches: write-combined (Normal non-cacheable) everywhere
 * (aipu_freebsd.c).
 */
#include <linux/dma-mapping.h>
#include <linux/mm.h>
void	*aipu_fbsd_dma_alloc_attrs(struct device *dev, size_t size,
	    dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs);
void	aipu_fbsd_dma_free_attrs(struct device *dev, size_t size, void *va,
	    dma_addr_t dma_handle, unsigned long attrs);
int	aipu_fbsd_dma_mmap_attrs(struct device *dev, struct vm_area_struct *vma,
	    void *va, dma_addr_t dma_handle, size_t size, unsigned long attrs);
#define	dma_alloc_attrs		aipu_fbsd_dma_alloc_attrs
#define	dma_free_attrs		aipu_fbsd_dma_free_attrs
#define	dma_mmap_attrs		aipu_fbsd_dma_mmap_attrs
#define	pgprot_dmacoherent(prot)	pgprot_writecombine(prot)

/*
 * For the driver's own-IOVA (IOMMU) paths, which map page by page: never
 * taken on FreeBSD (no IOMMU group).
 */
static inline int
vm_insert_page(struct vm_area_struct *vma __unused, unsigned long addr __unused,
    struct page *page __unused)
{
	return (-ENXIO);
}

/* The process (thread group) of a task. */
#include <linux/sched.h>
#define	task_tgid_nr(task)	((task)->task_thread->td_proc->p_pid)

/* The data cache line, for the driver's own cache maintenance. */
#include <machine/cpufunc.h>
#define	cache_line_size()	((int)dcache_line_size)

#endif /* !AIPU_FBSD_BUS */
#endif /* _AIPU_FREEBSD_COMPAT_H_ */
