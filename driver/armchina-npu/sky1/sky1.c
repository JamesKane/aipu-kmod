// SPDX-License-Identifier: GPL-2.0
/* Copyright 2024 Cix Technology Group Co., Ltd. All Rights Reserved.*/
/**
 * SoC: CIX SKY1 platform
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/devfreq.h>
#include <linux/devfreq-event.h>
#include <linux/scmi_protocol.h>
#include <linux/debugfs.h>
#include <linux/acpi.h>
#include <linux/version.h>
#include "armchina_aipu_soc.h"
#include "cix_sky1_soc.h"

#define NPU_CORE_ACPI_NAME_PREFIX       "CRE"

#define aipu_to_scmi_pd(pd) container_of(pd, struct aipu_scmi_perf_domain, genpd)
struct aipu_scmi_perf_domain {
	struct generic_pm_domain genpd;
	const struct scmi_perf_proto_ops *perf_ops;
	const struct scmi_protocol_handle *ph;
	const struct scmi_perf_domain_info *info;
	u32 domain_id;
};

int CIX_NPU_PD_NUM = CIX_NPU_PD_MAX_NUM;

static const char *cix_npu_pd_names[CIX_NPU_PD_MAX_NUM] = {
	"pd_core0", "pd_core1", "pd_core2",
};

static struct aipu_soc sky1 = {
	.priv = NULL,
};

static struct cix_aipu_priv *cix_aipu_priv;

static int aipu_scmi_device_set_freq(struct device *dev, unsigned long freq)
{
	struct generic_pm_domain *genpd = pd_to_genpd(dev->pm_domain);
	struct aipu_scmi_perf_domain *pd = aipu_to_scmi_pd(genpd);
	int ret;

	if (!pd->info->set_perf)
		return 0;

	if (!freq)
		return -EINVAL;

	ret = pd->perf_ops->freq_set(pd->ph, pd->domain_id, freq, false);
	return ret;
}

static unsigned long aipu_scmi_device_get_freq(struct device *dev)
{
	struct generic_pm_domain *genpd = pd_to_genpd(dev->pm_domain);
	struct aipu_scmi_perf_domain *pd = aipu_to_scmi_pd(genpd);
	unsigned long rate;
	int ret;

	ret = pd->perf_ops->freq_get(pd->ph, pd->domain_id, &rate, false);
	if (ret)
		return 0;

	return rate;
}

static int aipu_scmi_device_opp_table_parse(struct device *dev)
{
	struct generic_pm_domain *genpd = pd_to_genpd(dev->pm_domain);
	struct aipu_scmi_perf_domain *pd = aipu_to_scmi_pd(genpd);
	const struct scmi_perf_proto_ops *perf_ops;
	const struct scmi_protocol_handle *ph;
	int ret;

	perf_ops = pd->perf_ops;
	ph = pd->ph;
#if (KERNEL_VERSION(6, 7, 0) <= LINUX_VERSION_CODE)
	ret = perf_ops->device_opps_add(ph, dev, pd->domain_id);
#else
	ret = perf_ops->device_opps_add(ph, dev);
#endif
	if (ret) {
		dev_err(dev, "failed to add opps to the device\n");
		return ret;
	}

	return 0;
}

static void sky1_core_cnt_update(struct platform_device *p_dev)
{
	struct aipu_priv *aipu = dev_get_drvdata(&p_dev->dev);
	struct aipu_partition *partition;
	int idx;

	if (!aipu || aipu->version != AIPU_ISA_VERSION_ZHOUYI_V3)
		return;

	for (idx = 0; idx < aipu->partition_cnt; idx++) {
		partition = &aipu->partitions[idx];
		if (CIX_NPU_PD_NUM < partition->clusters[0].core_cnt) {
			dev_dbg(&p_dev->dev,
				 "update partition #%d en_core_cnt: %u -> %d\n",
				 idx,
				 atomic_read(&partition->clusters[0].en_core_cnt),
				 CIX_NPU_PD_NUM);
			partition->ops->enable_core_cnt(partition, 0, CIX_NPU_PD_NUM);
		}
	}
}

static struct cix_aipu_priv* sky1_priv_init(struct device *dev)
{
	cix_aipu_priv = devm_kzalloc(dev, sizeof(*cix_aipu_priv), GFP_KERNEL);
	if (!cix_aipu_priv)
		return ERR_PTR(-ENOMEM);
	return cix_aipu_priv;
}

static void remove_debugfs_dir(const char *name)
{
    struct dentry *dentry;
    struct dentry *parent = debugfs_lookup("opp", NULL);

    if (IS_ERR_OR_NULL(parent)) {
        pr_err("Failed to lookup opp debugfs directory\n");
        return;
    }

    dentry = debugfs_lookup(name, parent);
    if (IS_ERR_OR_NULL(dentry)) {
        pr_err("Failed to lookup %s debugfs directory\n", name);
        dput(parent);
        return;
    }

    debugfs_remove_recursive(dentry);

    dput(dentry);
    dput(parent);
}

#ifdef CONFIG_ENABLE_DEVFREQ
static int sky1_npu_devfreq_target(struct device *dev, unsigned long *freq, u32 flags)
{
    struct dev_pm_opp *opp;
    unsigned long pre_freq;
    unsigned long target_freq = *freq;
    int ret;

    opp = devfreq_recommended_opp(dev, freq, flags);
    if (IS_ERR(opp)) {
        dev_err(dev, "Failed to get recommended opp instance\n");
        ret = PTR_ERR(opp);
        return ret;
    }
    dev_pm_opp_put(opp);
    pre_freq = aipu_scmi_device_get_freq(cix_aipu_priv->opp_pmdomain);
    ret = aipu_scmi_device_set_freq(cix_aipu_priv->opp_pmdomain, *freq);

    dev_dbg(dev, "%s: target=%ld, previous=%ld, current=%ld.",
                    __func__, target_freq, pre_freq, *freq);

    return ret;
}

static int sky1_npu_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
    *freq = aipu_scmi_device_get_freq(cix_aipu_priv->opp_pmdomain);
    dev_dbg(dev, "%s: %ld", __func__, *freq);

    return 0;
}

static int sky1_npu_devfreq_get_dev_status(struct device *dev,
                            struct devfreq_dev_status *stat)
{
    dev_dbg(dev, "%s\n", __func__);

    stat->current_frequency = aipu_scmi_device_get_freq(cix_aipu_priv->opp_pmdomain);

    return 0;
}

static int sky1_npu_devfreq_init(struct device *dev, struct cix_aipu_priv *cix_aipu_priv)
{
    struct dev_pm_opp *opp;
    struct devfreq_dev_profile *profile;
    unsigned long freq;
    int opp_count;
    int i;
    int ret;

    dev_dbg(dev, "%s\n", __func__);

    profile = &(cix_aipu_priv->devfreq_profile);

#ifdef CONFIG_ARM_SCMI_SUPPORT_DT_ACPI
	cix_aipu_priv->opp_pmdomain = fwnode_dev_pm_domain_attach_by_name(dev, "perf");
#else
  	cix_aipu_priv->opp_pmdomain = dev_pm_domain_attach_by_name(dev, "perf");
#endif

    if (IS_ERR_OR_NULL(cix_aipu_priv->opp_pmdomain)) {
        dev_err(dev, "Failed to get perf domain");
        return -EFAULT;
    }
    cix_aipu_priv->opp_dl = device_link_add(dev, cix_aipu_priv->opp_pmdomain,
                            DL_FLAG_RPM_ACTIVE |
                            DL_FLAG_PM_RUNTIME |
                            DL_FLAG_STATELESS);
    if (IS_ERR_OR_NULL(cix_aipu_priv->opp_dl)) {
        ret = -ENODEV;
        goto detach_opp;
    }

    /* Add opps to opp power domain. */
    ret = aipu_scmi_device_opp_table_parse(cix_aipu_priv->opp_pmdomain);
    if (ret) {
        dev_err(dev, "Failed to add opps to the device");
        ret = -ENODEV;
        goto unlink_opp;
    }
    opp_count = dev_pm_opp_get_opp_count(cix_aipu_priv->opp_pmdomain);
    if (opp_count <= 0) {
        dev_err(dev, "Failed to get opps count.");
        ret = -EINVAL;
        goto unlink_opp;
    }
    profile->freq_table = kmalloc_array(opp_count, sizeof(unsigned long), GFP_KERNEL);
    for (i = 0, freq = 0; i < opp_count; i++, freq++) {
        opp = dev_pm_opp_find_freq_ceil(cix_aipu_priv->opp_pmdomain, &freq);
        if (IS_ERR(opp))
            break;
        dev_pm_opp_put(opp);
        profile->freq_table[i] = freq;

        /* Add opps to dev, since register devfreq device as dev */
        ret = dev_pm_opp_add(dev, freq, 0);
        if (ret) {
            dev_err(dev, "Failed to add opp %lu Hz", freq);
            while (i-- > 0) {
                dev_pm_opp_remove(dev, profile->freq_table[i]);
            }
            ret = -ENODEV;
            goto free_table;
        }
    }

    profile->max_state = i;
    profile->polling_ms = 50;
    profile->target = sky1_npu_devfreq_target;
    profile->get_dev_status = sky1_npu_devfreq_get_dev_status;
    profile->get_cur_freq = sky1_npu_devfreq_get_cur_freq;

    cix_aipu_priv->devfreq = devm_devfreq_add_device(dev, profile, DEVFREQ_GOV_USERSPACE, NULL);
    if (IS_ERR(cix_aipu_priv->devfreq)) {
        dev_err(dev, "Failed to add devfreq device");
        ret = PTR_ERR(cix_aipu_priv->devfreq);
        goto remove_table;
    }

    ret = devm_devfreq_register_opp_notifier(dev, cix_aipu_priv->devfreq);
    if (ret < 0) {
        dev_err(dev, "Failed to register opp notifier");
        goto remove_device;
    }

    return ret;

remove_device:
    devm_devfreq_remove_device(dev, cix_aipu_priv->devfreq);
    cix_aipu_priv->devfreq = NULL;
remove_table:
    dev_pm_opp_remove_table(dev);
    profile->max_state = 0;
free_table:
    kfree(profile->freq_table);
    profile->freq_table = NULL;
unlink_opp:
    device_link_del(cix_aipu_priv->opp_dl);
    cix_aipu_priv->opp_dl = NULL;
detach_opp:
    dev_pm_domain_detach(cix_aipu_priv->opp_pmdomain, true);

    return ret;
}

static int sky1_npu_devfreq_remove(struct device *dev, struct cix_aipu_priv *cix_aipu_priv)
{
    int i = 0;
    int opp_count;
    struct devfreq_dev_profile *profile;
    const char *opp_pd_name;

    profile = &(cix_aipu_priv->devfreq_profile);
    opp_count = dev_pm_opp_get_opp_count(cix_aipu_priv->opp_pmdomain);

    if (cix_aipu_priv->devfreq) {
        devm_devfreq_unregister_opp_notifier(dev, cix_aipu_priv->devfreq);
        devm_devfreq_remove_device(dev, cix_aipu_priv->devfreq);
        devm_kfree(dev, cix_aipu_priv->devfreq->data);
        cix_aipu_priv->devfreq = NULL;
    }

    for (i = 0; i < opp_count; i++) {
        if (profile->freq_table[i]) {
            dev_pm_opp_remove(dev, profile->freq_table[i]);
        }
    }

    dev_pm_opp_remove_table(dev);
    cix_aipu_priv->devfreq_profile.max_state = 0;
    kfree(cix_aipu_priv->devfreq_profile.freq_table);

    /*
     * sky1_npu_devfreq_init() also populated the perf pmdomain's OPP
     * table via scmi_device_opp_table_parse(). The pmdomain device is
     * persistent (it lives in SCMI), so without this remove the next
     * probe sees every frequency added by SCMI as a duplicate and the
     * OPP core warns "_opp_is_duplicate: duplicate OPPs detected".
     * Capture the dev_name before detaching, then remove the OPP table
     * (which on most kernels also tears down the debugfs entry) before
     * tearing down the device link to the supplier.
     */
    opp_pd_name = cix_aipu_priv->opp_pmdomain ?
                  kstrdup(dev_name(cix_aipu_priv->opp_pmdomain), GFP_KERNEL) :
                  NULL;
    if (cix_aipu_priv->opp_pmdomain)
        dev_pm_opp_remove_table(cix_aipu_priv->opp_pmdomain);

    if (cix_aipu_priv->opp_dl) {
        device_link_del(cix_aipu_priv->opp_dl);
        cix_aipu_priv->opp_dl = NULL;
    }
    if (cix_aipu_priv->opp_pmdomain) {
        dev_pm_domain_detach(cix_aipu_priv->opp_pmdomain, true);
        cix_aipu_priv->opp_pmdomain = NULL;
    }

    /*
     * Defensive: if the kernel did not auto-remove the supplier's OPP
     * debugfs entry (older kernels can leak it), drop it by its real
     * name. The previously-hardcoded "genpd:3:14260000.aipu" only
     * matched DT and failed silently on ACPI ("Failed to lookup...").
     */
    if (opp_pd_name) {
        remove_debugfs_dir(opp_pd_name);
        kfree(opp_pd_name);
    }

    return 0;
}
#endif

int sky1_npu_pm_runtime_get_sync(struct device *dev)
{
#ifdef CONFIG_PM
	int ret = 0;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_info(dev, "PM runtime get sync failed! ret = %d", ret);
		return ret;
	}

	return ret;
#else /* !CONFIG_PM  */
	return 0;
#endif /* CONFIG_PM */
}

int sky1_npu_pm_runtime_put(struct device *dev)
{
#ifdef CONFIG_PM
	int ret = 0;

#if KERNEL_VERSION(7, 0, 0) > LINUX_VERSION_CODE
	ret = pm_runtime_put(dev);
	if (ret < 0)
		dev_err(dev, "PM runtime put failed! ret=%d", ret);
#else
	pm_runtime_put(dev);
#endif

	return ret;
#else /* !CONFIG_PM  */
	return 0;
#endif /* CONFIG_PM */
}

static int sky1_npu_attach_pd(struct device *dev, struct aipu_soc *soc)
{
	int i = 0;
	int ret;
	struct device_link *link;

	for (i = 0; i < CIX_NPU_PD_NUM; i++) {
		dev_dbg(dev, "%s\n", cix_npu_pd_names[i]);

		cix_aipu_priv->pd_core[i] = dev_pm_domain_attach_by_name(dev, cix_npu_pd_names[i]);
		if (IS_ERR(cix_aipu_priv->pd_core[i])) {
			dev_err(dev, "failed to get pd %s\n", cix_npu_pd_names[i]);
			ret = PTR_ERR(cix_aipu_priv->pd_core[i]);
			cix_aipu_priv->pd_core[i] = NULL;
			goto err_detach;
		}

		link = device_link_add(dev, cix_aipu_priv->pd_core[i],
				DL_FLAG_STATELESS |
				DL_FLAG_PM_RUNTIME |
				DL_FLAG_RPM_ACTIVE);
		if (!link) {
			dev_err(dev, "Failed to add device_link to npu pd.\n");
			dev_pm_domain_detach(cix_aipu_priv->pd_core[i], true);
			cix_aipu_priv->pd_core[i] = NULL;
			ret = -EINVAL;
			goto err_detach;
		}
	}

	return 0;

err_detach:
	/* Unwind the domains and links already attached for cores [0, i). */
	while (i-- > 0) {
		device_link_remove(dev, cix_aipu_priv->pd_core[i]);
		dev_pm_domain_detach(cix_aipu_priv->pd_core[i], true);
		cix_aipu_priv->pd_core[i] = NULL;
	}

	return ret;
}

/*
 * Reverse the per-core attach performed by sky1_npu_probe() (ACPI) and
 * sky1_npu_attach_pd() (DT). Safe to call after a partial attach: cores
 * left as NULL are skipped. Per-core runtime PM references taken via
 * pm_runtime_resume_and_get() are NOT dropped here -- on the normal
 * removal path runtime_suspend has already balanced them, and on the
 * probe error path the caller drops them before calling here.
 */
static void sky1_npu_teardown_cores(struct device *dev)
{
	int i;

	for (i = 0; i < CIX_NPU_PD_NUM; i++) {
		if (!cix_aipu_priv->pd_core[i])
			continue;

		if (has_acpi_companion(dev)) {
			/* Detach the ACPI PM domain and power the core off. */
			dev_pm_domain_detach(cix_aipu_priv->pd_core[i], true);
			/* Balance probe's pm_runtime_enable(). */
			pm_runtime_disable(cix_aipu_priv->pd_core[i]);
			/* Balance probe's bus_find_device_by_fwnode(). */
			put_device(cix_aipu_priv->pd_core[i]);
		} else {
			dev_dbg(dev, "%s\n", cix_npu_pd_names[i]);
			/* Drop the stateless link before the domain it refers to. */
			device_link_remove(dev, cix_aipu_priv->pd_core[i]);
			dev_pm_domain_detach(cix_aipu_priv->pd_core[i], true);
		}
		cix_aipu_priv->pd_core[i] = NULL;
	}
}

static int sky1_npu_detach_pd(struct device *dev, struct aipu_soc *soc)
{
	dev_dbg(dev, "%s\n", __func__);

	/*
	 * On the normal removal path the PM core resumes the NPU device before
	 * unbinding and our runtime_suspend callback (sky1_npu_runtime_suspend)
	 * then balances every per-core pm_runtime_get_sync() with a matching
	 * pm_runtime_put(), so each core's usage_count is already 0 here.
	 * sky1_npu_teardown_cores() handles the remaining bookkeeping
	 * (pm_runtime_disable on ACPI, domain detach, device ref).
	 */
	sky1_npu_teardown_cores(dev);

	return 0;
}

static struct aipu_soc_operations sky1_ops = {
	.start_bw_profiling = NULL,
	.stop_bw_profiling = NULL,
	.read_profiling_reg = NULL,
	.enable_clk = NULL,
	.disable_clk = NULL,
	.is_clk_enabled = NULL,
	.is_aipu_irq = NULL,
};

static int sky1_npu_probe(struct platform_device *p_dev)
{
	int ret;
	int i = 0;
	u32 mask = 3;
	struct fwnode_handle *child;

	ret = device_property_read_u32(&p_dev->dev, "core_mask", &mask);
	if (mask == 0x1)
	{
		CIX_NPU_PD_NUM = 1;
	} else if ((mask == 0x0) || (mask == 0x2)) {
		return 0;
	}

	dev_info(&p_dev->dev, "%s: NPU core num is %d\n", __func__, CIX_NPU_PD_NUM);

	sky1_priv_init(&p_dev->dev);

	sky1.priv = cix_aipu_priv;
	dev_set_drvdata(&p_dev->dev, cix_aipu_priv);

    if (has_acpi_companion(&p_dev->dev)) {
#ifdef	CONFIG_ACPI
		p_dev->dev.power.ignore_children = true;
        fwnode_for_each_child_node(p_dev->dev.fwnode, child) {
            if (is_acpi_data_node(child)) {
                continue;
            }
            if (!strncmp(acpi_device_bid(to_acpi_device_node(child)),
                            NPU_CORE_ACPI_NAME_PREFIX, ACPI_NAMESEG_SIZE - 1)) {

				if (i == CIX_NPU_PD_NUM)
					break;

				cix_aipu_priv->pd_core[i] = bus_find_device_by_fwnode(&platform_bus_type, child);
                pm_runtime_enable(cix_aipu_priv->pd_core[i]);
				dev_pm_domain_attach(cix_aipu_priv->pd_core[i], true);
				dev_pm_set_driver_flags(cix_aipu_priv->pd_core[i], DPM_FLAG_NO_DIRECT_COMPLETE);
				ACPI_COMPANION(cix_aipu_priv->pd_core[i])->power.flags.ignore_parent = true;

                i++;
            }
        }
#endif
    } else {
		ret = sky1_npu_attach_pd(&p_dev->dev, &sky1);
		if (ret) {
			dev_err(&p_dev->dev, "aipu attach pd failed, ret: %d\n", ret);
			return ret;
		}
    }

#ifdef CONFIG_ENABLE_DEVFREQ
	ret = sky1_npu_devfreq_init(&p_dev->dev, cix_aipu_priv);
	if (ret) {
		dev_err(&p_dev->dev, "aipu devfreq init failed, ret: %d\n", ret);
		goto devfreq_init_failed;
	}
#endif

#ifdef CONFIG_PM
	pm_runtime_get_noresume(&p_dev->dev);
	pm_runtime_set_active(&p_dev->dev);
    pm_runtime_enable(&p_dev->dev);
#endif /* CONFIG_PM */

    if (has_acpi_companion(&p_dev->dev)) {
		for (i = 0; i < CIX_NPU_PD_NUM; i++) {
			ret = pm_runtime_resume_and_get(cix_aipu_priv->pd_core[i]);
			if (ret < 0)
				goto err_core_get;
		}
    }

    ret = armchina_aipu_probe(p_dev, &sky1, &sky1_ops);
    if (ret) {
		dev_err(&p_dev->dev, "aipu real probe failed, ret: %d\n", ret);
		i = CIX_NPU_PD_NUM;
		goto err_core_get;
	}

	sky1_core_cnt_update(p_dev);

	dev_err(&p_dev->dev, "%s: armchina_aipu_probe done\n", __func__); //TODO dbg

#ifdef CONFIG_PM
    sky1_npu_pm_runtime_put(&p_dev->dev);
#endif /* CONFIG_PM */

    return 0;

err_core_get:
	/*
	 * Drop the per-core runtime PM references taken above. On the
	 * pm_runtime_resume_and_get() failure path 'i' is the number of cores
	 * that succeeded (the failing call already dropped its own ref); on
	 * the armchina_aipu_probe() failure path all cores succeeded.
	 */
	if (has_acpi_companion(&p_dev->dev)) {
		while (i-- > 0)
			pm_runtime_put_sync(cix_aipu_priv->pd_core[i]);
	}

#ifdef CONFIG_PM
	/* Undo the NPU device runtime PM setup done just above. */
	pm_runtime_disable(&p_dev->dev);
	pm_runtime_put_noidle(&p_dev->dev);
#endif /* CONFIG_PM */

#ifdef CONFIG_ENABLE_DEVFREQ
	sky1_npu_devfreq_remove(&p_dev->dev, cix_aipu_priv);
#endif

devfreq_init_failed:
	sky1_npu_teardown_cores(&p_dev->dev);

	return ret;
}

#if KERNEL_VERSION(6, 11, 0) > LINUX_VERSION_CODE
static int sky1_npu_remove(struct platform_device *p_dev)
#else
static void sky1_npu_remove(struct platform_device *p_dev)
#endif
{
	dev_dbg(&p_dev->dev, "%s \n", __func__);
#ifdef CONFIG_ENABLE_DEVFREQ
	sky1_npu_devfreq_remove(&p_dev->dev, cix_aipu_priv);
#endif

	armchina_aipu_remove(p_dev);

	sky1_npu_detach_pd(&p_dev->dev, &sky1);

#ifdef CONFIG_PM
	pm_runtime_disable(&p_dev->dev);
#endif /* CONFIG_PM */

#if KERNEL_VERSION(6, 11, 0) > LINUX_VERSION_CODE
	return 0;
#endif
}

#ifdef CONFIG_PM
static int sky1_npu_runtime_suspend(struct device *dev)
{
	int ret = 0;
	struct platform_device *p_dev = to_platform_device(dev);
	pm_message_t state;
	state.event = 0;

	ret = armchina_aipu_suspend(p_dev, state);
	if (ret) {
		dev_err(dev, "aipu is busy, %s return %d\n", __func__, ret);
		return ret;
	}

	if (has_acpi_companion(dev)) {
		for (int i = 0; i < CIX_NPU_PD_NUM; i++) {
#if KERNEL_VERSION(7, 0, 0) > LINUX_VERSION_CODE
			ret = pm_runtime_put(cix_aipu_priv->pd_core[i]);
			if (ret < 0) {
				dev_err(cix_aipu_priv->pd_core[i], "NPU core PM runtime put failed! ret=%d", ret);
				return ret;
			}
#else
			pm_runtime_put(cix_aipu_priv->pd_core[i]);
#endif
		}
	}

	return ret;
}

static int sky1_npu_runtime_resume(struct device *dev)
{
	int ret;
	struct platform_device *p_dev = to_platform_device(dev);

	if (has_acpi_companion(dev)) {
		for (int i = 0; i < CIX_NPU_PD_NUM; i++) {
			ret = pm_runtime_get_sync(cix_aipu_priv->pd_core[i]);
			if (ret < 0) {
				dev_err(cix_aipu_priv->pd_core[i], "NPU core PM runtime get sync failed! ret=%d", ret);
				return ret;
			}
		}
	}

	ret = armchina_aipu_resume(p_dev);
	if (ret)
		return ret;

	sky1_core_cnt_update(p_dev);

	return 0;
}

static void npu_complete(struct device *dev) {
	if (has_acpi_companion(dev))
		pm_request_resume(dev);
}

static const struct dev_pm_ops cix_sky1_npu_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(sky1_npu_runtime_suspend, sky1_npu_runtime_resume, NULL)
	.complete = npu_complete,
};
#endif /* CONFIG_PM */

#ifdef CONFIG_OF
static const struct of_device_id aipu_of_match[] = {
	{
		.compatible = "armchina,zhouyi-v1",
	},
	{
		.compatible = "armchina,zhouyi-v2",
	},
	{
		.compatible = "armchina,zhouyi-v3",
	},
	{
		.compatible = "armchina,zhouyi",
	},
	{ }
};

MODULE_DEVICE_TABLE(of, aipu_of_match);
#endif

static const struct acpi_device_id aipu_acpi_match[] = {
							{ .id = "CIXH4000", .driver_data = 0 },
							{ /* sentinel */ } };

MODULE_DEVICE_TABLE(acpi, aipu_acpi_match);

static struct platform_driver aipu_platform_driver = {
	.probe = sky1_npu_probe,
	.remove = sky1_npu_remove,
	.driver = {
		.name = "armchina",
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &cix_sky1_npu_pm_ops,
#endif /* CONFIG_PM */
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(aipu_of_match),
		.acpi_match_table = ACPI_PTR(aipu_acpi_match)
#endif
	},
};

module_platform_driver(aipu_platform_driver);
MODULE_LICENSE("GPL v2");
