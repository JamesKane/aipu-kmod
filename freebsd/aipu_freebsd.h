/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Between the FreeBSD bus glue (aipu_freebsd_bus.c) and the Linux side of
 * the glue (aipu_freebsd.c): FreeBSD types only.
 */

#ifndef _AIPU_FREEBSD_H_
#define	_AIPU_FREEBSD_H_

/* aipu_freebsd.c */
int	aipu_fbsd_linux_attach(device_t dev, uint64_t pa, uint64_t size,
	    int irq);
void	aipu_fbsd_linux_detach(void);

/*
 * aipu_freebsd_bus.c: an integer or integer-array _DSD property of dev,
 * up to nvals of them into vals (which may be NULL): the number it has,
 * or -1 if it has none.
 */
int	aipu_fbsd_get_u32_prop(device_t dev, const char *name, uint32_t *vals,
	    size_t nvals);

#endif /* _AIPU_FREEBSD_H_ */
