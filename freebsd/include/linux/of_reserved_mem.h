/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Devicetree reserved memory: none under ACPI.  The driver allocates
 * through the DMA API then.
 */

#ifndef _AIPU_LINUX_OF_RESERVED_MEM_H_
#define	_AIPU_LINUX_OF_RESERVED_MEM_H_

#include <linux/device.h>
#include <linux/errno.h>

static inline int
of_reserved_mem_device_init_by_idx(struct device *dev __unused,
    struct device_node *np __unused, int idx __unused)
{
	return (-ENODEV);
}

static inline void
of_reserved_mem_device_release(struct device *dev __unused)
{
}

#endif /* _AIPU_LINUX_OF_RESERVED_MEM_H_ */
