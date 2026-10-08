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
 * The FreeBSD side of the Zhouyi NPU on the CIX Sky1: the ACPI device
 * (CIXH4000), powered with its cores, handed to the Linux driver as a
 * platform device.  In place of CIX's sky1/sky1.c, which does the same
 * through Linux's ACPI power domains and runtime PM.
 *
 * Its clock is an SCMI performance domain ("perf" in _DSD "power-domains"):
 * dev.aipu.N.freq sets it from dev.aipu.N.freq_levels, as CIX's devfreq
 * does with the userspace governor.  The firmware starts it at the top.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <dev/acpica/acpivar.h>

#include <arm64/cix/sky1_scmi.h>

#include "acpi_if.h"
#include "aipu_freebsd.h"

/* The NPU's cores: power-only ACPI children, each with power resources. */
#define	AIPU_FBSD_NCORES	3
#define	AIPU_FBSD_MAXLEVELS	16

static char *aipu_fbsd_acpi_ids[] = { "CIXH4000", NULL };

struct aipu_fbsd_softc {
	device_t	dev;
	bool		powered;
	uint32_t	perf;		/* SCMI performance domain */
	int		nlevels;	/* its levels, 0 without DVFS */
	uint32_t	levels[AIPU_FBSD_MAXLEVELS];	/* kHz */
};

int
aipu_fbsd_get_u32_prop(device_t dev, const char *name, uint32_t *vals,
    size_t nvals)
{
	const ACPI_OBJECT *obj, *e;
	uint32_t i;

	if (ACPI_FAILURE(ACPI_GET_PROPERTY(device_get_parent(dev), dev, name,
	    &obj)))
		return (-1);
	switch (obj->Type) {
	case ACPI_TYPE_INTEGER:
		if (vals != NULL && nvals > 0)
			vals[0] = obj->Integer.Value;
		return (1);
	case ACPI_TYPE_PACKAGE:
		for (i = 0; i < obj->Package.Count; i++) {
			e = &obj->Package.Elements[i];
			if (e->Type != ACPI_TYPE_INTEGER)
				return (-1);
			if (vals != NULL && i < nvals)
				vals[i] = e->Integer.Value;
		}
		return (obj->Package.Count);
	default:
		return (-1);
	}
}

/*
 * The SCMI performance domain named "perf": "power-domains" is a package of
 * (reference, domain) pairs, "power-domain-names" their names.
 */
static int
aipu_fbsd_perf_domain(device_t dev, uint32_t *domain)
{
	const ACPI_OBJECT *pds, *names, *e;
	device_t bus = device_get_parent(dev);
	uint32_t i, k;

	if (ACPI_FAILURE(ACPI_GET_PROPERTY(bus, dev, "power-domains", &pds)) ||
	    pds->Type != ACPI_TYPE_PACKAGE ||
	    ACPI_FAILURE(ACPI_GET_PROPERTY(bus, dev, "power-domain-names",
	    &names)))
		return (ENOENT);
	for (i = 0, k = 0; i + 1 < pds->Package.Count; i++) {
		e = &pds->Package.Elements[i];
		if (e->Type != ACPI_TYPE_LOCAL_REFERENCE)
			continue;
		e++;
		if (e->Type != ACPI_TYPE_INTEGER)
			return (ENOENT);
		/* The k-th pair's name: a string, or a package of them. */
		if ((names->Type == ACPI_TYPE_STRING && k == 0 &&
		    strcmp(names->String.Pointer, "perf") == 0) ||
		    (names->Type == ACPI_TYPE_PACKAGE &&
		    k < names->Package.Count &&
		    names->Package.Elements[k].Type == ACPI_TYPE_STRING &&
		    strcmp(names->Package.Elements[k].String.Pointer,
		    "perf") == 0)) {
			*domain = e->Integer.Value;
			return (0);
		}
		k++;
	}
	return (ENOENT);
}

static int
aipu_fbsd_freq_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct aipu_fbsd_softc *sc = arg1;
	uint32_t khz;
	int error, i, mhz;

	if ((error = sky1_scmi_perf_get(sc->perf, &khz)) != 0)
		return (error);
	mhz = khz / 1000;
	error = sysctl_handle_int(oidp, &mhz, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	for (i = 0; i < sc->nlevels; i++)
		if (sc->levels[i] / 1000 == (uint32_t)mhz)
			return (sky1_scmi_perf_set(sc->perf, sc->levels[i]));
	return (EINVAL);
}

static int
aipu_fbsd_levels_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct aipu_fbsd_softc *sc = arg1;
	struct sbuf sb;
	int error, i;

	sbuf_new_for_sysctl(&sb, NULL, 64, req);
	for (i = 0; i < sc->nlevels; i++)
		sbuf_printf(&sb, "%s%u", i > 0 ? " " : "", sc->levels[i] / 1000);
	error = sbuf_finish(&sb);
	sbuf_delete(&sb);
	return (error);
}

/* DVFS, if the firmware describes it: the levels, and the sysctls. */
static void
aipu_fbsd_dvfs_attach(struct aipu_fbsd_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *kids;
	int error;

	if (aipu_fbsd_perf_domain(sc->dev, &sc->perf) != 0)
		return;
	error = sky1_scmi_perf_levels(sc->perf, sc->levels,
	    AIPU_FBSD_MAXLEVELS, &sc->nlevels);
	if (error != 0 || sc->nlevels == 0) {
		device_printf(sc->dev, "no levels for performance domain %u "
		    "(%d)\n", sc->perf, error);
		sc->nlevels = 0;
		return;
	}
	sc->nlevels = MIN(sc->nlevels, AIPU_FBSD_MAXLEVELS);
	ctx = device_get_sysctl_ctx(sc->dev);
	kids = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev));
	SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "freq",
	    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, 0,
	    aipu_fbsd_freq_sysctl, "I", "NPU clock (MHz), one of freq_levels");
	SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "freq_levels",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    aipu_fbsd_levels_sysctl, "A", "NPU clock levels (MHz)");
}

/*
 * The NPU, then each core: their _PR0 power resources (_ON releases each
 * from reset through the firmware's DMRP method).
 */
static int
aipu_fbsd_power(struct aipu_fbsd_softc *sc, int state)
{
	ACPI_HANDLE h, core;
	ACPI_STATUS st;
	char name[5];
	int i;

	h = acpi_get_handle(sc->dev);
	if (state == ACPI_STATE_D0) {
		st = acpi_pwr_switch_consumer(h, state);
		if (ACPI_FAILURE(st)) {
			device_printf(sc->dev, "cannot power the NPU: %s\n",
			    AcpiFormatException(st));
			return (ENXIO);
		}
	}
	for (i = 0; i < AIPU_FBSD_NCORES; i++) {
		snprintf(name, sizeof(name), "CRE%d", i);
		if (ACPI_FAILURE(AcpiGetHandle(h, name, &core)))
			continue;
		st = acpi_pwr_switch_consumer(core, state);
		if (ACPI_FAILURE(st) && state == ACPI_STATE_D0) {
			device_printf(sc->dev, "cannot power %s: %s\n", name,
			    AcpiFormatException(st));
			return (ENXIO);
		}
	}
	if (state != ACPI_STATE_D0)
		(void)acpi_pwr_switch_consumer(h, state);
	return (0);
}

static int
aipu_fbsd_probe(device_t dev)
{
	int rv;

	rv = ACPI_ID_PROBE(device_get_parent(dev), dev, aipu_fbsd_acpi_ids,
	    NULL);
	if (rv <= 0)
		device_set_desc(dev, "Arm China Zhouyi NPU (CIX Sky1)");
	return (rv);
}

static int
aipu_fbsd_attach(device_t dev)
{
	struct aipu_fbsd_softc *sc = device_get_softc(dev);
	rman_res_t pa, size, irq, n;
	int error;

	sc->dev = dev;
	/*
	 * The resources stay the bus's: Linux's driver maps the registers
	 * itself, and LinuxKPI's request_irq() takes the FreeBSD interrupt
	 * number.
	 */
	if (bus_get_resource(dev, SYS_RES_MEMORY, 0, &pa, &size) != 0 ||
	    bus_get_resource(dev, SYS_RES_IRQ, 0, &irq, &n) != 0) {
		device_printf(dev, "no registers or interrupt\n");
		return (ENXIO);
	}
	if ((error = aipu_fbsd_power(sc, ACPI_STATE_D0)) != 0)
		return (error);
	sc->powered = true;

	error = -aipu_fbsd_linux_attach(dev, pa, size, irq);
	if (error == 0)
		aipu_fbsd_dvfs_attach(sc);
	if (error != 0) {
		aipu_fbsd_linux_detach();
		(void)aipu_fbsd_power(sc, ACPI_STATE_D3);
		sc->powered = false;
	}
	return (error);
}

static int
aipu_fbsd_detach(device_t dev)
{
	struct aipu_fbsd_softc *sc = device_get_softc(dev);

	aipu_fbsd_linux_detach();
	if (sc->powered)
		(void)aipu_fbsd_power(sc, ACPI_STATE_D3);
	sc->powered = false;
	return (0);
}

static device_method_t aipu_fbsd_methods[] = {
	DEVMETHOD(device_probe,		aipu_fbsd_probe),
	DEVMETHOD(device_attach,	aipu_fbsd_attach),
	DEVMETHOD(device_detach,	aipu_fbsd_detach),
	DEVMETHOD_END
};

static driver_t aipu_fbsd_driver = {
	"aipu",
	aipu_fbsd_methods,
	sizeof(struct aipu_fbsd_softc),
};

DRIVER_MODULE(aipu, acpi, aipu_fbsd_driver, 0, 0);
ACPI_PNP_INFO(aipu_fbsd_acpi_ids);
MODULE_DEPEND(aipu, acpi, 1, 1, 1);
MODULE_DEPEND(aipu, dmabuf, 1, 1, 1);
MODULE_DEPEND(aipu, linuxkpi, 1, 1, 1);
MODULE_VERSION(aipu, 1);
