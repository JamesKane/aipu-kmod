/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Linux's IOVA allocator, for the driver's own-IOVA (IOMMU) paths to
 * compile.  They never run on FreeBSD: iommu_group_get() finds no group,
 * so the driver allocates through the DMA API, which the IOMMU's busdma
 * tag translates.
 */

#ifndef _AIPU_LINUX_IOVA_H_
#define	_AIPU_LINUX_IOVA_H_

#include <linux/types.h>

struct iova {
	unsigned long	pfn_hi;
	unsigned long	pfn_lo;
};

struct iova_domain {
	unsigned long	granule;
};

static inline void
init_iova_domain(struct iova_domain *iovad, unsigned long granule,
    unsigned long start_pfn)
{
	iovad->granule = granule;
}

static inline void
put_iova_domain(struct iova_domain *iovad __unused)
{
}

static inline struct iova *
alloc_iova(struct iova_domain *iovad __unused, unsigned long size __unused,
    unsigned long limit_pfn __unused, bool size_aligned __unused)
{
	return (NULL);
}

static inline struct iova *
reserve_iova(struct iova_domain *iovad __unused, unsigned long pfn_lo __unused,
    unsigned long pfn_hi __unused)
{
	return (NULL);
}

static inline void
free_iova(struct iova_domain *iovad __unused, unsigned long pfn __unused)
{
}

static inline void
__free_iova(struct iova_domain *iovad __unused, struct iova *iova __unused)
{
}

#endif /* _AIPU_LINUX_IOVA_H_ */
