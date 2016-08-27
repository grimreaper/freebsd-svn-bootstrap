/*-
 * Copyright (c) 2016 Landon Fuller <landonf@FreeBSD.org>
 * Copyright (c) 2010, Broadcom Corporation.
 * All rights reserved.
 * 
 * This file is derived from the siutils.c source distributed with the
 * Asus RT-N16 firmware source code release.
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $Id: siutils.c,v 1.821.2.48 2011-02-11 20:59:28 Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/systm.h>

#include <dev/bhnd/bhnd.h>

#include <dev/bhnd/cores/chipc/chipcreg.h>
#include <dev/bhnd/cores/chipc/chipcvar.h>

#include <dev/bhnd/cores/pmu/bhnd_pmuvar.h>
#include <dev/bhnd/cores/pmu/bhnd_pmureg.h>

#include "bhnd_chipc_if.h"

#include "bhnd_pwrctl_private.h"

/*
 * ChipCommon Power Control.
 * 
 * Provides a bhnd_pmu_if-compatible interface to device clocking and
 * power management on non-PMU chipsets.
 */

typedef enum {
	BHND_PWRCTL_WAR_UP,	/**< apply attach/resume workarounds */
	BHND_PWRCTL_WAR_RUN,	/**< apply running workarounds */
	BHND_PWRCTL_WAR_DOWN,	/**< apply detach/suspend workarounds */
} bhnd_pwrctl_wars;

static int	bhnd_pwrctl_updateclk(struct bhnd_pwrctl_softc *sc,
		    bhnd_pwrctl_wars wars);

static struct bhnd_device_quirk pwrctl_quirks[];


/* Supported parent core device identifiers */
static const struct bhnd_device pwrctl_devices[] = {
	BHND_DEVICE(BCM, CC, "ChipCommon Power Control", pwrctl_quirks),
	BHND_DEVICE_END
};

/* Device quirks table */
static struct bhnd_device_quirk pwrctl_quirks[] = {
	BHND_CORE_QUIRK	(HWREV_LTE(5),		PWRCTL_QUIRK_PCICLK_CTL),
	BHND_CORE_QUIRK	(HWREV_RANGE(6, 9),	PWRCTL_QUIRK_SLOWCLK_CTL),
	BHND_CORE_QUIRK	(HWREV_RANGE(10, 19),	PWRCTL_QUIRK_INSTACLK_CTL),

	BHND_DEVICE_QUIRK_END
};

static int
bhnd_pwrctl_probe(device_t dev)
{
	const struct bhnd_device	*id;
	struct chipc_caps		*ccaps;
	device_t			 chipc;

	/* Look for compatible chipc parent */
	chipc = device_get_parent(dev);
	if (device_get_devclass(chipc) != devclass_find("bhnd_chipc"))
		return (ENXIO);

	if (device_get_driver(chipc) != &bhnd_chipc_driver)
		return (ENXIO);

	/* Verify chipc capability flags */
	ccaps = BHND_CHIPC_GET_CAPS(chipc);
	if (ccaps->pmu || !ccaps->pwr_ctrl)
		return (ENXIO);

	/* Check for chipc device match */
	id = bhnd_device_lookup(chipc, pwrctl_devices,
	    sizeof(pwrctl_devices[0]));
	if (id == NULL)
		return (ENXIO);

	device_set_desc(dev, id->desc);
	return (BUS_PROBE_NOWILDCARD);
}

static int
bhnd_pwrctl_attach(device_t dev)
{
	struct bhnd_pwrctl_softc	*sc;
	const struct bhnd_chipid	*cid;
	struct chipc_softc		*chipc_sc;
	bhnd_devclass_t			 hostb_class;
	device_t			 hostb_dev;
	int				 error;

	sc = device_get_softc(dev);

	/* TODO: Need further testing on actual PWRCTL hardware */
	device_printf(dev, "WARNING: Using untested PWRCTL support\n");

	sc->dev = dev;
	sc->chipc_dev = device_get_parent(dev);
	sc->quirks = bhnd_device_quirks(sc->chipc_dev, pwrctl_devices,
	    sizeof(pwrctl_devices[0]));

	/* On devices that lack a slow clock source, HT must always be
	 * enabled. */
	hostb_class = BHND_DEVCLASS_INVALID;
	hostb_dev = bhnd_find_hostb_device(device_get_parent(sc->chipc_dev));
	if (hostb_dev != NULL)
		hostb_class = bhnd_get_class(hostb_dev);

	cid = bhnd_get_chipid(sc->chipc_dev);
	switch (cid->chip_id) {
	case BHND_CHIPID_BCM4311:
		if (cid->chip_rev <= 1 && hostb_class == BHND_DEVCLASS_PCI)
			sc->quirks |= PWRCTL_QUIRK_FORCE_HT;
		break;

	case BHND_CHIPID_BCM4321:
		if (hostb_class == BHND_DEVCLASS_PCIE ||
		    hostb_class == BHND_DEVCLASS_PCI)
			sc->quirks |= PWRCTL_QUIRK_FORCE_HT;
		break;

	case BHND_CHIPID_BCM4716:
		if (hostb_class == BHND_DEVCLASS_PCIE)
			sc->quirks |= PWRCTL_QUIRK_FORCE_HT;
		break;
	}

	/* Fetch core register block from ChipCommon parent */
	chipc_sc = device_get_softc(sc->chipc_dev);
	sc->res = chipc_sc->core;

	PWRCTL_LOCK_INIT(sc);
	STAILQ_INIT(&sc->clkres_list);

	/* Initialize power control */
	PWRCTL_LOCK(sc);

	if ((error = bhnd_pwrctl_init(sc))) {
		PWRCTL_UNLOCK(sc);
		goto cleanup;
	}

	/* Apply default clock transitions */
	if ((error = bhnd_pwrctl_updateclk(sc, BHND_PWRCTL_WAR_UP))) {
		PWRCTL_UNLOCK(sc);
		goto cleanup;
	}

	PWRCTL_UNLOCK(sc);

	return (0);

cleanup:
	PWRCTL_LOCK_DESTROY(sc);
	return (error);
}

static int
bhnd_pwrctl_detach(device_t dev)
{
	struct bhnd_pwrctl_softc	*sc;
	struct bhnd_pwrctl_clkres	*clkres, *crnext;
	int				 error;

	sc = device_get_softc(dev);

	if ((error = bhnd_pwrctl_setclk(sc, BHND_CLOCK_DYN)))
		return (error);

	STAILQ_FOREACH_SAFE(clkres, &sc->clkres_list, cr_link, crnext)
		free(clkres, M_DEVBUF);

	PWRCTL_LOCK_DESTROY(sc);
	return (0);
}

static int
bhnd_pwrctl_suspend(device_t dev)
{
	struct bhnd_pwrctl_softc	*sc;
	int				 error;

	sc = device_get_softc(dev);

	/* Update clock state */
	PWRCTL_LOCK(sc);
	error = bhnd_pwrctl_updateclk(sc, BHND_PWRCTL_WAR_DOWN);
	PWRCTL_UNLOCK(sc);

	return (error);
}

static int
bhnd_pwrctl_resume(device_t dev)
{
	struct bhnd_pwrctl_softc	*sc;
	int				 error;

	sc = device_get_softc(dev);

	PWRCTL_LOCK(sc);

	/* Re-initialize power control registers */
	if ((error = bhnd_pwrctl_init(sc))) {
		device_printf(sc->dev, "PWRCTL init failed: %d\n", error);
		goto cleanup;
	}

	/* Restore clock state */
	if ((error = bhnd_pwrctl_updateclk(sc, BHND_PWRCTL_WAR_UP))) {
		device_printf(sc->dev, "clock state restore failed: %d\n",
		    error);
		goto cleanup;
	}

cleanup:
	PWRCTL_UNLOCK(sc);
	return (error);
}

/**
 * Find the clock reservation associated with @p pinfo, if any.
 * 
 * @param sc Driver instance state.
 * @param pinfo PMU info for device.
 */
static struct bhnd_pwrctl_clkres *
bhnd_pwrctl_find_res(struct bhnd_pwrctl_softc *sc, 
    struct bhnd_core_pmu_info *pinfo)
{
	struct bhnd_pwrctl_clkres *clkres;

	PWRCTL_LOCK_ASSERT(sc, MA_OWNED);

	STAILQ_FOREACH(clkres, &sc->clkres_list, cr_link) {
		if (clkres->owner == pinfo->pm_dev)
			return (clkres);
	}

	/* not found */
	return (NULL);
}

/**
 * Enumerate all active clock requests, compute the minimum required clock,
 * and issue any required clock transition.
 * 
 * @param sc Driver instance state.
 * @param wars Work-around state.
 */
static int
bhnd_pwrctl_updateclk(struct bhnd_pwrctl_softc *sc, bhnd_pwrctl_wars wars)
{
	struct bhnd_pwrctl_clkres	*clkres;
	bhnd_clock			 clock;

	PWRCTL_LOCK_ASSERT(sc, MA_OWNED);

	/* Default clock target */
	clock = BHND_CLOCK_DYN;

	/* Apply quirk-specific overrides to the clock target */
	switch (wars) {
	case BHND_PWRCTL_WAR_UP:
		/* Force HT clock */
		if (PWRCTL_QUIRK(sc, FORCE_HT))
			clock = BHND_CLOCK_HT;
		break;

	case BHND_PWRCTL_WAR_RUN:
		/* Cannot transition clock if FORCE_HT */
		if (PWRCTL_QUIRK(sc, FORCE_HT))
			return (0);
		break;

	case BHND_PWRCTL_WAR_DOWN:
		/* Leave default clock unmodified to permit
		 * transition back to BHND_CLOCK_DYN on FORCE_HT devices. */
		break;
	}

	/* Determine required clock */
	STAILQ_FOREACH(clkres, &sc->clkres_list, cr_link)
		clock = bhnd_clock_max(clock, clkres->clock);

	/* Map to supported clock setting */
	switch (clock) {
	case BHND_CLOCK_DYN:
	case BHND_CLOCK_ILP:
		clock = BHND_CLOCK_DYN;
		break;
	case BHND_CLOCK_ALP:
		/* In theory FORCE_ALP is supported by the hardware, but
		 * there are currently no known use-cases for it; mapping
		 * to HT is still valid, and allows us to punt on determing
		 * where FORCE_ALP is supported and functional */
		clock = BHND_CLOCK_HT;
		break;
	case BHND_CLOCK_HT:
		break;
	default:
		device_printf(sc->dev, "unknown clock: %#x\n", clock);
		return (ENODEV);
	}

	/* Issue transition */
	return (bhnd_pwrctl_setclk(sc, clock));
}

static int
bhnd_pwrctl_core_req_clock(device_t dev, struct bhnd_core_pmu_info *pinfo,
    bhnd_clock clock)
{
	struct bhnd_pwrctl_softc	*sc;
	struct bhnd_pwrctl_clkres	*clkres;
	int				 error;

	sc = device_get_softc(dev);
	error = 0;

	PWRCTL_LOCK(sc);

	clkres = bhnd_pwrctl_find_res(sc, pinfo);

	/* BHND_CLOCK_DYN discards the clock reservation entirely */
	if (clock == BHND_CLOCK_DYN) {
		/* nothing to clean up? */
		if (clkres == NULL) {
			PWRCTL_UNLOCK(sc);
			return (0);
		}

		/* drop reservation and apply clock transition */
		STAILQ_REMOVE(&sc->clkres_list, clkres,
		    bhnd_pwrctl_clkres, cr_link);

		if ((error = bhnd_pwrctl_updateclk(sc, BHND_PWRCTL_WAR_RUN))) {
			device_printf(dev, "clock transition failed: %d\n",
			    error);

			/* restore reservation */
			STAILQ_INSERT_TAIL(&sc->clkres_list, clkres, cr_link);

			PWRCTL_UNLOCK(sc);
			return (error);
		}

		/* deallocate orphaned reservation */
		free(clkres, M_DEVBUF);

		PWRCTL_UNLOCK(sc);
		return (0);
	}

	/* create (or update) reservation */
	if (clkres == NULL) {
		clkres = malloc(sizeof(struct bhnd_pwrctl_clkres), M_DEVBUF,
		    M_NOWAIT);
		if (clkres == NULL)
			return (ENOMEM);

		clkres->owner = pinfo->pm_dev;
		clkres->clock = clock;

		STAILQ_INSERT_TAIL(&sc->clkres_list, clkres, cr_link);
	} else {
		KASSERT(clkres->owner == pinfo->pm_dev, ("invalid owner"));
		clkres->clock = clock;
	}

	/* apply clock transition */
	error = bhnd_pwrctl_updateclk(sc, BHND_PWRCTL_WAR_RUN);
	if (error) {
		STAILQ_REMOVE(&sc->clkres_list, clkres, bhnd_pwrctl_clkres,
		    cr_link);
		free(clkres, M_DEVBUF);
	}

	PWRCTL_UNLOCK(sc);
	return (error);
}

static int
bhnd_pwrctl_core_req_ext_rsrc(device_t dev, struct bhnd_core_pmu_info *pinfo,
    u_int rsrc)
{
	/* HW does not support per-core external resources */
	return (ENODEV);
}

static int
bhnd_pwrctl_core_release_ext_rsrc(device_t dev,
    struct bhnd_core_pmu_info *pinfo, u_int rsrc)
{
	/* HW does not support per-core external resources */
	return (ENODEV);
}

static int
bhnd_pwrctl_core_en_clocks(device_t dev, struct bhnd_core_pmu_info *pinfo,
    uint32_t clocks)
{
	/* All supported clocks are already enabled by default (?) */
	clocks &= ~(BHND_CLOCK_DYN |
		    BHND_CLOCK_ILP |
		    BHND_CLOCK_ALP |
		    BHND_CLOCK_HT);

	if (clocks != 0) {
		device_printf(dev, "%s requested unknown clocks: %#x\n",
		    device_get_nameunit(pinfo->pm_dev), clocks);
		return (ENODEV);
	}

	return (0);
}

static int
bhnd_pwrctl_core_release(device_t dev, struct bhnd_core_pmu_info *pinfo)
{
	/* Requesting BHND_CLOCK_DYN releases any outstanding clock
	 * reservations */
	return (bhnd_pwrctl_core_req_clock(dev, pinfo, BHND_CLOCK_DYN));
}

static device_method_t bhnd_pwrctl_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			bhnd_pwrctl_probe),
	DEVMETHOD(device_attach,		bhnd_pwrctl_attach),
	DEVMETHOD(device_detach,		bhnd_pwrctl_detach),
	DEVMETHOD(device_suspend,		bhnd_pwrctl_suspend),
	DEVMETHOD(device_resume,		bhnd_pwrctl_resume),

	/* BHND PMU interface */
	DEVMETHOD(bhnd_pmu_core_req_clock,	bhnd_pwrctl_core_req_clock),
	DEVMETHOD(bhnd_pmu_core_en_clocks,	bhnd_pwrctl_core_en_clocks),
	DEVMETHOD(bhnd_pmu_core_req_ext_rsrc,	bhnd_pwrctl_core_req_ext_rsrc),
	DEVMETHOD(bhnd_pmu_core_release_ext_rsrc, bhnd_pwrctl_core_release_ext_rsrc),
	DEVMETHOD(bhnd_pmu_core_release,	bhnd_pwrctl_core_release),

	DEVMETHOD_END
};

DEFINE_CLASS_0(bhnd_pmu, bhnd_pwrctl_driver, bhnd_pwrctl_methods,
    sizeof(struct bhnd_pwrctl_softc));
EARLY_DRIVER_MODULE(bhnd_pwrctl, bhnd_chipc, bhnd_pwrctl_driver,
    bhnd_pmu_devclass, NULL, NULL, BUS_PASS_TIMER + BUS_PASS_ORDER_MIDDLE);

MODULE_DEPEND(bhnd_pwrctl, bhnd, 1, 1, 1);
MODULE_VERSION(bhnd_pwrctl, 1);
