/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 James Kane
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * The Linux side of the glue: the platform device the Zhouyi driver attaches
 * to, its platform driver (in place of CIX's sky1/sky1.c), and what it needs
 * from Linux that LinuxKPI lacks: _DSD properties, and DMA buffers mapped as
 * Linux maps them for a device that does not snoop the CPU caches.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <vm/vm.h>
#include <vm/pmap.h>

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "armchina_aipu_soc.h"
#include "aipu_priv.h"
#include "aipu_partition.h"
#include "cix_sky1_soc.h"
#include "aipu_freebsd.h"

/* Properties (ACPI _DSD) */

int
device_property_count_u32(struct device *dev, const char *propname)
{
	int n;

	n = aipu_fbsd_get_u32_prop(dev->bsddev, propname, NULL, 0);
	return (n < 0 ? -EINVAL : n);
}

int
device_property_read_u32_array(struct device *dev, const char *propname,
    u32 *val, size_t nval)
{
	int n;

	n = aipu_fbsd_get_u32_prop(dev->bsddev, propname, val, nval);
	if (n < 0)
		return (-EINVAL);
	return (n < nval ? -EOVERFLOW : 0);
}

int
device_property_read_u32(struct device *dev, const char *propname, u32 *val)
{
	return (device_property_read_u32_array(dev, propname, val, 1));
}

/*
 * DMA buffers.  The NPU does not snoop the CPU caches (_CCA 0): Linux maps
 * its buffers non-cacheable, for the kernel and (pgprot_dmacoherent) for
 * user space.  LinuxKPI's dma_alloc_coherent() maps them cacheable: make
 * every mapping of the pages write-combined (Normal non-cacheable), as
 * komeda's glue does.  The buffers are contiguous; their DMA address is the
 * IOMMU's, so the pages are found from the kernel mapping.
 */

static int
aipu_fbsd_set_memattr(void *va, size_t size, vm_memattr_t ma)
{
	vm_offset_t off;

	for (off = 0; off < round_page(size); off += PAGE_SIZE)
		pmap_page_set_memattr(PHYS_TO_VM_PAGE(
		    pmap_kextract((vm_offset_t)va + off)), ma);
	return (pmap_change_attr(va, round_page(size), ma));
}

void *
aipu_fbsd_dma_alloc_attrs(struct device *dev, size_t size,
    dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs __unused)
{
	void *va;

	va = dma_alloc_coherent(dev, size, dma_handle, gfp);
	if (va != NULL && aipu_fbsd_set_memattr(va, size,
	    VM_MEMATTR_WRITE_COMBINING) != 0) {
		(void)aipu_fbsd_set_memattr(va, size, VM_MEMATTR_DEFAULT);
		dma_free_coherent(dev, size, va, *dma_handle);
		va = NULL;
	}
	return (va);
}

void
aipu_fbsd_dma_free_attrs(struct device *dev, size_t size, void *va,
    dma_addr_t dma_handle, unsigned long attrs __unused)
{
	(void)aipu_fbsd_set_memattr(va, size, VM_MEMATTR_DEFAULT);
	dma_free_coherent(dev, size, va, dma_handle);
}

int
aipu_fbsd_dma_mmap_attrs(struct device *dev __unused,
    struct vm_area_struct *vma, void *va, dma_addr_t dma_handle __unused,
    size_t size, unsigned long attrs __unused)
{
	unsigned long len, off;

	off = vma->vm_pgoff << PAGE_SHIFT;
	len = vma->vm_end - vma->vm_start;
	if (off >= round_page(size) || len > round_page(size) - off)
		return (-ENXIO);
	/*
	 * Record the range: LinuxKPI maps it once mmap() returns (with no VM
	 * object yet, remap_pfn_range() cannot), with vm_page_prot's
	 * attribute.
	 */
	return (io_remap_pfn_range(vma, vma->vm_start,
	    (pmap_kextract((vm_offset_t)va) + off) >> PAGE_SHIFT, len,
	    vma->vm_page_prot));
}

/* Runtime PM: the NPU stays powered while attached (aipu_freebsd_bus.c). */

int
sky1_npu_pm_runtime_get_sync(struct device *dev __unused)
{
	return (0);
}

int
sky1_npu_pm_runtime_put(struct device *dev __unused)
{
	return (0);
}

/* The platform driver */

/* As CIX's: no clock operations, the firmware's clock. */
static struct aipu_soc aipu_fbsd_soc;
static struct aipu_soc_operations aipu_fbsd_ops;

static int
aipu_fbsd_probe(struct platform_device *pdev)
{
	struct aipu_partition *partition;
	struct aipu_priv *aipu;
	u32 mask, ncores;
	int error, i;

	/*
	 * As sky1_npu_probe(): core_mask 1 means one core; 0 and 2, none;
	 * 3 (and no property), all three.
	 */
	mask = 3;
	(void)device_property_read_u32(&pdev->dev, "core_mask", &mask);
	if (mask == 0 || mask == 2)
		return (-ENODEV);
	ncores = mask == 1 ? 1 : 3;
	dev_info(&pdev->dev, "%u NPU cores\n", ncores);

	error = armchina_aipu_probe(pdev, &aipu_fbsd_soc, &aipu_fbsd_ops);
	if (error != 0)
		return (error);

	/* As sky1_core_cnt_update(): no more cores than are powered. */
	aipu = platform_get_drvdata(pdev);
	if (aipu != NULL && aipu->version == AIPU_ISA_VERSION_ZHOUYI_V3) {
		for (i = 0; i < aipu->partition_cnt; i++) {
			partition = &aipu->partitions[i];
			if (ncores < partition->clusters[0].core_cnt)
				partition->ops->enable_core_cnt(partition, 0,
				    ncores);
		}
	}
	return (0);
}

static void
aipu_fbsd_remove(struct platform_device *pdev)
{
	armchina_aipu_remove(pdev);
}

static const struct platform_device_id aipu_fbsd_ids[] = {
	{ "armchina" },
	{ }
};

static struct platform_driver aipu_fbsd_driver = {
	.probe = aipu_fbsd_probe,
	.remove = aipu_fbsd_remove,
	.id_table = aipu_fbsd_ids,
	.driver = {
		.name = "armchina",
	},
};

static int __init
aipu_fbsd_init(void)
{
	return (platform_driver_register(&aipu_fbsd_driver));
}
module_init(aipu_fbsd_init);

static void __exit
aipu_fbsd_exit(void)
{
	platform_driver_unregister(&aipu_fbsd_driver);
}
module_exit(aipu_fbsd_exit);

/* The platform device, for the FreeBSD device aipu_freebsd_bus.c attached */

static struct platform_device *aipu_fbsd_pdev;

static void
aipu_fbsd_pdev_release(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	kfree(pdev->resource);
	kfree(pdev);
}

int
aipu_fbsd_linux_attach(device_t dev, uint64_t pa, uint64_t size, int irq)
{
	struct platform_device *pdev;
	struct resource *res;
	int error;

	linux_set_current(curthread);
	pdev = kzalloc(sizeof(*pdev), GFP_KERNEL);
	res = kcalloc(2, sizeof(*res), GFP_KERNEL);
	res[0].start = pa;
	res[0].end = pa + size - 1;
	res[0].flags = IORESOURCE_MEM;
	/* LinuxKPI's request_irq() takes FreeBSD interrupt numbers. */
	res[1].start = res[1].end = irq;
	res[1].flags = IORESOURCE_IRQ;
	pdev->name = "armchina";
	pdev->id = PLATFORM_DEVID_NONE;
	pdev->resource = res;
	pdev->num_resources = 2;
	/*
	 * LinuxKPI names it and sets up DMA through the ACPI device: the
	 * IOMMU's busdma tag, which translates (IORT named component).
	 */
	pdev->dev.bsddev = dev;
	pdev->dev.release = aipu_fbsd_pdev_release;
	error = platform_device_register(pdev);
	if (error != 0) {
		platform_device_put(pdev);
		return (error);
	}
	aipu_fbsd_pdev = pdev;
	return (0);
}

void
aipu_fbsd_linux_detach(void)
{
	linux_set_current(curthread);
	if (aipu_fbsd_pdev != NULL)
		platform_device_unregister(aipu_fbsd_pdev);
	aipu_fbsd_pdev = NULL;
}

/* For aipu_linux.ko: a Linux program's descriptor, ours or not. */
bool
aipu_fbsd_is_aipu_fd(int fd)
{
	struct linux_file *filp;
	struct vnode *vp;
	bool ours;

	if ((filp = linux_fget(fd)) == NULL)
		return (false);
	vp = filp->f_vnode;
	ours = vp != NULL && vp->v_type == VCHR && vp->v_rdev != NULL &&
	    strcmp(devtoname(vp->v_rdev), "aipu") == 0;
	fput(filp);
	return (ours);
}

MODULE_LICENSE("GPL v2");
