/*-
 * Copyright (C) 2011,2015 by Nathan Whitehorn. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/priv.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/tty.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_bus.h>

#include "opal.h"
#include "uart_if.h"

struct uart_opal_softc {
	device_t dev;
	phandle_t node;
	int vtermid;

	struct tty *tp;
	struct resource *irqres;
	int irqrid;
	struct callout callout;
	void *sc_icookie;
	int polltime;

	struct mtx sc_mtx;
	int protocol;

	char opal_inbuf[16];
	uint64_t inbuflen;
	uint8_t outseqno;
};

static struct uart_opal_softc	*console_sc = NULL;
#if defined(KDB)
static int			alt_break_state;
#endif

enum {
	OPAL_RAW, OPAL_HVSI
};

#define VS_DATA_PACKET_HEADER		0xff
#define VS_CONTROL_PACKET_HEADER	0xfe
#define  VSV_SET_MODEM_CTL		0x01
#define  VSV_MODEM_CTL_UPDATE		0x02
#define  VSV_RENEGOTIATE_CONNECTION	0x03
#define VS_QUERY_PACKET_HEADER		0xfd
#define  VSV_SEND_VERSION_NUMBER	0x01
#define  VSV_SEND_MODEM_CTL_STATUS	0x02
#define VS_QUERY_RESPONSE_PACKET_HEADER	0xfc

static int uart_opal_probe(device_t dev);
static int uart_opal_attach(device_t dev);
static void uart_opal_intr(void *v);

static device_method_t uart_opal_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		uart_opal_probe),
	DEVMETHOD(device_attach,	uart_opal_attach),

	DEVMETHOD_END
};

static driver_t uart_opal_driver = {
	"uart",
	uart_opal_methods,
	sizeof(struct uart_opal_softc),
};
 
DRIVER_MODULE(uart_opal, opalcons, uart_opal_driver, uart_devclass, 0, 0);

static cn_probe_t uart_opal_cnprobe;
static cn_init_t uart_opal_cninit;
static cn_term_t uart_opal_cnterm;
static cn_getc_t uart_opal_cngetc;
static cn_putc_t uart_opal_cnputc;
static cn_grab_t uart_opal_cngrab;
static cn_ungrab_t uart_opal_cnungrab;

CONSOLE_DRIVER(uart_opal);

static void uart_opal_ttyoutwakeup(struct tty *tp);

static struct ttydevsw uart_opal_tty_class = {
	.tsw_flags	= TF_INITLOCK|TF_CALLOUT,
	.tsw_outwakeup	= uart_opal_ttyoutwakeup,
};

static int
uart_opal_probe_node(struct uart_opal_softc *sc)
{
	phandle_t node = sc->node;
	uint32_t reg;
	char buf[64];

	sc->inbuflen = 0;
	sc->outseqno = 0;

	if (OF_getprop(node, "device_type", buf, sizeof(buf)) <= 0)
		return (ENXIO);
	if (strcmp(buf, "serial") != 0)
		return (ENXIO);

	reg = -1;
	OF_getprop(node, "reg", &reg, sizeof(reg));
	if (reg == -1)
		return (ENXIO);
	sc->vtermid = reg;
	sc->node = node;

	if (OF_getprop(node, "compatible", buf, sizeof(buf)) <= 0)
		return (ENXIO);
	if (strcmp(buf, "ibm,opal-console-raw") == 0) {
		sc->protocol = OPAL_RAW;
		return (0);
	} else if (strcmp(buf, "ibm,opal-console-hvsi") == 0) {
		sc->protocol = OPAL_HVSI;
		return (0);
	}

	return (ENXIO);
}

static int
uart_opal_probe(device_t dev)
{
	struct uart_opal_softc sc;
	int err;

	sc.node = ofw_bus_get_node(dev);
	err = uart_opal_probe_node(&sc);
	if (err != 0)
		return (err);

	device_set_desc(dev, "OPAL Serial Port");

	return (err);
}

static void
uart_opal_cnprobe(struct consdev *cp)
{
	char buf[64];
	phandle_t input, chosen;
	static struct uart_opal_softc sc;

	if (opal_check() != 0)
		goto fail;

	if ((chosen = OF_finddevice("/chosen")) == -1)
		goto fail;

	/* Check if OF has an active stdin/stdout */
	if (OF_getprop(chosen, "linux,stdout-path", buf, sizeof(buf)) <= 0)
		goto fail;
	
	input = OF_finddevice(buf);
	if (input == -1)
		goto fail;

	sc.node = input;
	if (uart_opal_probe_node(&sc) != 0)
		goto fail;
	mtx_init(&sc.sc_mtx, "uart_opal", NULL, MTX_SPIN | MTX_QUIET |
	    MTX_NOWITNESS);

	cp->cn_pri = CN_NORMAL;
	console_sc = &sc;
	return;
	
fail:
	cp->cn_pri = CN_DEAD;
	return;
}

static int
uart_opal_attach(device_t dev)
{
	struct uart_opal_softc *sc;
	int unit;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->node = ofw_bus_get_node(dev);
	uart_opal_probe_node(sc);

	unit = device_get_unit(dev);
	sc->tp = tty_alloc(&uart_opal_tty_class, sc);
	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL,
	    MTX_SPIN | MTX_QUIET | MTX_NOWITNESS);

	if (console_sc != NULL && console_sc->vtermid == sc->vtermid) {
		sc->outseqno = console_sc->outseqno;
		console_sc = sc;
		sprintf(uart_opal_consdev.cn_name, "ttyu%r", unit);
		tty_init_console(sc->tp, 0);
	}

	sc->irqrid = 0;
	sc->irqres = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irqrid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->irqres != NULL) {
		bus_setup_intr(dev, sc->irqres, INTR_TYPE_TTY | INTR_MPSAFE,
		    NULL, uart_opal_intr, sc, &sc->sc_icookie);
	} else {
		callout_init(&sc->callout, CALLOUT_MPSAFE);
		sc->polltime = hz / 20;
		if (sc->polltime < 1)
			sc->polltime = 1;
		callout_reset(&sc->callout, sc->polltime, uart_opal_intr, sc);
	}

	tty_makedev(sc->tp, NULL, "u%r", unit);

	return (0);
}

static void
uart_opal_cninit(struct consdev *cp)
{

	strcpy(cp->cn_name, "opalcons");
}

static void
uart_opal_cnterm(struct consdev *cp)
{
}

static int
uart_opal_get(struct uart_opal_softc *sc, void *buffer, size_t bufsize)
{
	int err;
	int hdr = 0;

	if (sc->protocol == OPAL_RAW) {
		uint64_t len = bufsize;
		uint64_t olen = (uint64_t)&len;
		uint64_t obuf = (uint64_t)buffer;

		if (pmap_bootstrapped) {
			olen = vtophys(&len);
			obuf = vtophys(buffer);
		}

		err = opal_call(OPAL_CONSOLE_READ, sc->vtermid, olen, obuf);
		if (err != OPAL_SUCCESS)
			return (-1);

		bufsize = len;
	} else {
		uart_lock(&sc->sc_mtx);
		if (sc->inbuflen == 0) {
			err = opal_call(OPAL_CONSOLE_READ, sc->vtermid,
			    &sc->inbuflen, sc->opal_inbuf);
			if (err != OPAL_SUCCESS) {
				uart_unlock(&sc->sc_mtx);
				return (-1);
			}
			hdr = 1; 
		}

		if (sc->inbuflen == 0) {
			uart_unlock(&sc->sc_mtx);
			return (0);
		}

		if (bufsize > sc->inbuflen)
			bufsize = sc->inbuflen;

		if (hdr == 1) {
			sc->inbuflen = sc->inbuflen - 4;
			/* The HVSI protocol has a 4 byte header, skip it */
			memmove(&sc->opal_inbuf[0], &sc->opal_inbuf[4],
			    sc->inbuflen);
		}

		memcpy(buffer, sc->opal_inbuf, bufsize);
		sc->inbuflen -= bufsize;
		if (sc->inbuflen > 0)
			memmove(&sc->opal_inbuf[0], &sc->opal_inbuf[bufsize],
			    sc->inbuflen);

		uart_unlock(&sc->sc_mtx);
	}

	return (bufsize);
}

static int
uart_opal_put(struct uart_opal_softc *sc, void *buffer, size_t bufsize)
{
	uint16_t seqno;
	uint64_t len = bufsize;
	char	cbuf[16];
	int	err;
	uint64_t olen = (uint64_t)&len;
	uint64_t obuf = (uint64_t)cbuf;

	if (pmap_bootstrapped)
		olen = vtophys(&len);

	if (sc->protocol == OPAL_RAW) {
		if (pmap_bootstrapped)
			obuf = vtophys(buffer);
		else
			obuf = (uint64_t)(buffer);

		err = opal_call(OPAL_CONSOLE_WRITE, sc->vtermid, olen, obuf);
	} else {
		if (pmap_bootstrapped)
			obuf = vtophys(cbuf);
		uart_lock(&sc->sc_mtx);
		if (bufsize > 12)
			bufsize = 12;
		seqno = sc->outseqno++;
		cbuf[0] = VS_DATA_PACKET_HEADER;
		cbuf[1] = 4 + bufsize; /* total length */
		cbuf[2] = (seqno >> 8) & 0xff;
		cbuf[3] = seqno & 0xff;
		memcpy(&cbuf[4], buffer, bufsize);
		len = 4 + bufsize;
		err = opal_call(OPAL_CONSOLE_WRITE, sc->vtermid, olen, obuf);
		uart_unlock(&sc->sc_mtx);

		len -= 4;
	}

#if 0
	if (err != OPAL_SUCCESS)
		len = 0;
#endif

	return (len);
}

static int
uart_opal_cngetc(struct consdev *cp)
{
	unsigned char c;
	int retval;

	retval = uart_opal_get(console_sc, &c, 1);
	if (retval != 1)
		return (-1);
#if defined(KDB)
	kdb_alt_break(c, &alt_break_state);
#endif

	return (c);
}

static void
uart_opal_cnputc(struct consdev *cp, int c)
{
	unsigned char ch = c;
	uart_opal_put(console_sc, &ch, 1);
}

static void
uart_opal_cngrab(struct consdev *cp)
{
}

static void
uart_opal_cnungrab(struct consdev *cp)
{
}

static void
uart_opal_ttyoutwakeup(struct tty *tp)
{
	struct uart_opal_softc *sc;
	char buffer[8];
	int len;

	sc = tty_softc(tp);
	
	while ((len = ttydisc_getc(tp, buffer, sizeof(buffer))) != 0)
		uart_opal_put(sc, buffer, len);
}

static void
uart_opal_intr(void *v)
{
	struct uart_opal_softc *sc = v;
	struct tty *tp = sc->tp;
	unsigned char c;
	int len;

	tty_lock(tp);
	while ((len = uart_opal_get(sc, &c, 1)) > 0)
		ttydisc_rint(tp, c, 0);
	ttydisc_rint_done(tp);
	tty_unlock(tp);

	if (sc->irqres == NULL)
		callout_reset(&sc->callout, sc->polltime, uart_opal_intr, sc);
}

