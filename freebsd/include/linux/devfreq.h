/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * The types the driver's Sky1 header names (struct cix_aipu_priv).  No
 * devfreq yet: a fixed clock (CONFIG_ENABLE_DEVFREQ unset).
 */
#ifndef _AIPU_LINUX_DEVFREQ_H_
#define	_AIPU_LINUX_DEVFREQ_H_
struct devfreq;
struct devfreq_dev_profile {
	unsigned long	initial_freq;
	unsigned int	polling_ms;
};
#endif
