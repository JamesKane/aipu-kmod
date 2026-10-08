/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * LinuxKPI's linux/iommu.h, and what the driver's own-IOVA (IOMMU) paths
 * need to compile.  They never run on FreeBSD: iommu_group_get() finds no
 * group, so the driver allocates through the DMA API, which the IOMMU's
 * busdma tag translates.
 */

#ifndef _AIPU_LINUX_IOMMU_H_
#define	_AIPU_LINUX_IOMMU_H_

#include <linux/device.h>
#include <linux/scatterlist.h>

#define	__IOMMU_DOMAIN_PAGING	(1U << 0)
#define	__IOMMU_DOMAIN_DMA_API	(1U << 1)
#define	__IOMMU_DOMAIN_PT	(1U << 2)
#define	__IOMMU_DOMAIN_DMA_FQ	(1U << 3)

#define	IOMMU_DOMAIN_BLOCKED	(0U)
#define	IOMMU_DOMAIN_IDENTITY	(__IOMMU_DOMAIN_PT)
#define	IOMMU_DOMAIN_UNMANAGED	(__IOMMU_DOMAIN_PAGING)
#define	IOMMU_DOMAIN_DMA	(__IOMMU_DOMAIN_PAGING | __IOMMU_DOMAIN_DMA_API)
#define	IOMMU_DOMAIN_DMA_FQ	(__IOMMU_DOMAIN_PAGING | __IOMMU_DOMAIN_DMA_API | __IOMMU_DOMAIN_DMA_FQ)

#define	IOMMU_READ	(1 << 0)
#define	IOMMU_WRITE	(1 << 1)

struct iommu_group;

struct iommu_domain {
	unsigned int	type;
	void		*iova_cookie;
};

static inline struct iommu_domain *
iommu_get_domain_for_dev(struct device *dev __unused)
{
	return (NULL);
}

static inline struct iommu_group *
iommu_group_get(struct device *dev __unused)
{
	return (NULL);
}

static inline void
iommu_group_put(struct iommu_group *group __unused)
{
}

static inline ssize_t
iommu_map_sg(struct iommu_domain *domain __unused, unsigned long iova __unused,
    struct scatterlist *sg __unused, unsigned int nents __unused,
    int prot __unused, gfp_t gfp __unused)
{
	return (0);
}

static inline size_t
iommu_unmap(struct iommu_domain *domain __unused, unsigned long iova __unused,
    size_t size __unused)
{
	return (0);
}

static inline phys_addr_t
iommu_iova_to_phys(struct iommu_domain *domain __unused,
    dma_addr_t iova __unused)
{
	return (0);
}

#endif /* _AIPU_LINUX_IOMMU_H_ */
