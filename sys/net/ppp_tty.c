/*
 * ppp_tty.c - Point-to-Point Protocol (PPP) driver for asynchronous
 * 	       tty devices.
 *
 * Copyright (c) 1989 Carnegie Mellon University.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by Carnegie Mellon University.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Drew D. Perkins
 * Carnegie Mellon University
 * 4910 Forbes Ave.
 * Pittsburgh, PA 15213
 * (412) 268-8576
 * ddp@andrew.cmu.edu
 *
 * Based on:
 *	@(#)if_sl.c	7.6.1.2 (Berkeley) 2/15/89
 *
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Serial Line interface
 *
 * Rick Adams
 * Center for Seismic Studies
 * 1300 N 17th Street, Suite 1450
 * Arlington, Virginia 22209
 * (703)276-7900
 * rick@seismo.ARPA
 * seismo!rick
 *
 * Pounded on heavily by Chris Torek (chris@mimsy.umd.edu, umcp-cs!chris).
 * Converted to 4.3BSD Beta by Chris Torek.
 * Other changes made at Berkeley, based in part on code by Kirk Smith.
 *
 * Converted to 4.3BSD+ 386BSD by Brad Parker (brad@cayman.com)
 * Added VJ tcp header compression; more unified ioctls
 *
 * Extensively modified by Paul Mackerras (paulus@cs.anu.edu.au).
 * Cleaned up a lot of the mbuf-related code to fix bugs that
 * caused system crashes and packet corruption.  Changed pppstart
 * so that it doesn't just give up with a "collision" if the whole
 * packet doesn't fit in the output ring buffer.
 *
 * Added priority queueing for interactive IP packets, following
 * the model of if_sl.c, plus hooks for bpf.
 * Paul Mackerras (paulus@cs.anu.edu.au).
 */

/* $FreeBSD$ */

#include "opt_ppp.h"		/* XXX for ppp_defs.h */

#define VJC			/* XXX for ppp_defs.h */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/uio.h>

#ifdef PPP_FILTER
#include <net/bpf.h>
#endif
#include <net/if_ppp.h>
#include <net/if_pppvar.h>

static int	pppopen(struct cdev *dev, struct tty *tp);
static int	pppclose(struct tty *tp, int flag);
static int	pppread(struct tty *tp, struct uio *uio, int flag);
static int	pppwrite(struct tty *tp, struct uio *uio, int flag);
static int	ppptioctl(struct tty *tp, u_long cmd, caddr_t data,
					int flag, struct thread *td);
static int	pppinput(int c, struct tty *tp);
static int	pppstart(struct tty *tp);

static u_short	pppfcs(u_short fcs, u_char *cp, int len);
static void	pppasyncstart(struct ppp_softc *);
static void	pppasyncctlp(struct ppp_softc *);
static void	pppasyncrelinq(struct ppp_softc *);
static void	pppasyncsetmtu(struct ppp_softc *);
static void	ppp_timeout(void *);
static void	pppgetm(struct ppp_softc *sc);
static void	ppplogchar(struct ppp_softc *, int);

/* XXX called from if_ppp.c - layering violation */
void		pppasyncattach(void *);
void		pppasyncdetach(void);

/*
 * Some useful mbuf macros not in mbuf.h.
 */
#define M_IS_CLUSTER(m)	((m)->m_flags & M_EXT)

#define M_DATASTART(m)	\
	(M_IS_CLUSTER(m) ? (m)->m_ext.ext_buf : \
	    (m)->m_flags & M_PKTHDR ? (m)->m_pktdat : (m)->m_dat)

#define M_DATASIZE(m)	\
	(M_IS_CLUSTER(m) ? (m)->m_ext.ext_size : \
	    (m)->m_flags & M_PKTHDR ? MHLEN: MLEN)

/*
 * Does c need to be escaped?
 */
#define ESCAPE_P(c)	(sc->sc_asyncmap[(c) >> 5] & (1 << ((c) & 0x1F)))

/*
 * Procedures for using an async tty interface for PPP.
 */

/* This is a FreeBSD-2.X kernel. */
#define CCOUNT(q)	((q)->c_cc)
#define PPP_LOWAT	100	/* Process more output when < LOWAT on queue */
#define	PPP_HIWAT	400	/* Don't start a new packet if HIWAT on que */

/*
 * Define the PPP line discipline.
 */

static struct linesw pppdisc = {
	pppopen,	pppclose,	pppread,	pppwrite,
	ppptioctl,	pppinput,	pppstart,	ttymodem
};

void
pppasyncattach(dummy)
    void *dummy;
{
    ldisc_register(PPPDISC, &pppdisc);
}

void
pppasyncdetach()
{
    ldisc_deregister(PPPDISC);
}

/*
 * Line specific open routine for async tty devices.
 * Attach the given tty to the first available ppp unit.
 * Called from device open routine or ttioctl() at >= splsofttty()
 */
/* ARGSUSED */
static int
pppopen(dev, tp)
    struct cdev *dev;
    register struct tty *tp;
{
    struct thread *td = curthread;		/* XXX */
    register struct ppp_softc *sc;
    int error, s;

    if ((error = suser(td)) != 0)
	return (error);

    s = spltty();

    tp->t_hotchar = PPP_FLAG;

    if ((sc = pppalloc(td->td_proc->p_pid)) == NULL) {
	splx(s);
	return ENXIO;
    }

    if (sc->sc_relinq)
	(*sc->sc_relinq)(sc);	/* get previous owner to relinquish the unit */

    sc->sc_ilen = 0;
    sc->sc_m = NULL;
    bzero(sc->sc_asyncmap, sizeof(sc->sc_asyncmap));
    sc->sc_asyncmap[0] = 0xffffffff;
    sc->sc_asyncmap[3] = 0x60000000;
    sc->sc_rasyncmap = 0;
    sc->sc_devp = (void *) tp;
    sc->sc_start = pppasyncstart;
    sc->sc_ctlp = pppasyncctlp;
    sc->sc_relinq = pppasyncrelinq;
    sc->sc_setmtu = pppasyncsetmtu;
    sc->sc_outm = NULL;
    pppgetm(sc);
    sc->sc_if.if_flags |= IFF_RUNNING;
    getmicrotime(&sc->sc_if.if_lastchange);
    sc->sc_if.if_baudrate = tp->t_ospeed;

    tp->t_hotchar = PPP_FLAG;
    ttyflush(tp, FREAD | FWRITE);

    /*
     * Pre-allocate cblocks to the "just right" amount.  The 1 byte t_canq
     * allocation helps avoid the need for select and/or FIONREAD.
     * We also pass 1 byte tokens through t_canq...
     */
    clist_alloc_cblocks(&tp->t_canq, 1, 1);
    clist_alloc_cblocks(&tp->t_outq, sc->sc_if.if_mtu + PPP_HIWAT,
			sc->sc_if.if_mtu + PPP_HIWAT);
    clist_alloc_cblocks(&tp->t_rawq, 0, 0);

    splx(s);

    return (0);
}

/*
 * Line specific close routine, called from device close routine
 * and from ttioctl at >= splsofttty().
 * Detach the tty from the ppp unit.
 * Mimics part of tty_close().
 */
static int
pppclose(tp, flag)
    struct tty *tp;
    int flag;
{
    register struct ppp_softc *sc;
    int s;

    s = spltty();
    ttyflush(tp, FREAD | FWRITE);
    clist_free_cblocks(&tp->t_canq);
    clist_free_cblocks(&tp->t_outq);
    sc = ppp_for_tty(tp);
    if (sc != NULL) {
	    pppasyncrelinq(sc);
	    pppdealloc(sc);
    }
    splx(s);
    return 0;
}

/*
 * Relinquish the interface unit to another device.
 */
static void
pppasyncrelinq(sc)
    struct ppp_softc *sc;
{
    int s;

    s = spltty();
    if (sc->sc_outm) {
	m_freem(sc->sc_outm);
	sc->sc_outm = NULL;
    }
    if (sc->sc_m) {
	m_freem(sc->sc_m);
	sc->sc_m = NULL;
    }
    if (sc->sc_flags & SC_TIMEOUT) {
	untimeout(ppp_timeout, (void *) sc, sc->sc_ch);
	sc->sc_flags &= ~SC_TIMEOUT;
    }
    splx(s);
}

/*
 * This gets called from the upper layer to notify a mtu change
 */
static void
pppasyncsetmtu(sc)
register struct ppp_softc *sc;
{
    register struct tty *tp = (struct tty *) sc->sc_devp;
    int s;

    s = spltty();
    if (tp != NULL)
	clist_alloc_cblocks(&tp->t_outq, sc->sc_if.if_mtu + PPP_HIWAT,
			     sc->sc_if.if_mtu + PPP_HIWAT);
    splx(s);
}

/*
 * Line specific (tty) read routine.
 * called at zero spl from the device driver in the response to user-level
 * reads on the tty file descriptor (ie: pppd).
 */
static int
pppread(tp, uio, flag)
    register struct tty *tp;
    struct uio *uio;
    int flag;
{
    register struct ppp_softc *sc = ppp_for_tty(tp);
    struct mbuf *m, *m0;
    register int s;
    int error = 0;

    if (sc == NULL)
	return 0;
    /*
     * Loop waiting for input, checking that nothing disasterous
     * happens in the meantime.
     */
    s = spltty();
    for (;;) {
	if (tp != (struct tty *) sc->sc_devp || tp->t_line != PPPDISC) {
	    splx(s);
	    return 0;
	}
	if (sc->sc_inq.ifq_head != NULL)
	    break;
	if ((tp->t_state & TS_CONNECTED) == 0) {
	    splx(s);
	    return 0;		/* end of file */
	}
	if (tp->t_state & TS_ASYNC || flag & O_NONBLOCK) {
	    splx(s);
	    return (EWOULDBLOCK);
	}
	error = ttysleep(tp, TSA_HUP_OR_INPUT(tp), TTIPRI | PCATCH, "pppin", 0);
	if (error) {
	    splx(s);
	    return error;
	}
    }

    /* Pull place-holder byte out of canonical queue */
    getc(&tp->t_canq);

    /* Get the packet from the input queue */
    IF_DEQUEUE(&sc->sc_inq, m0);
    splx(s);

    for (m = m0; m && uio->uio_resid; m = m->m_next)
	if ((error = uiomove(mtod(m, u_char *), m->m_len, uio)) != 0)
	    break;
    m_freem(m0);
    return (error);
}

/*
 * Line specific (tty) write routine.
 * called at zero spl from the device driver in the response to user-level
 * writes on the tty file descriptor (ie: pppd).
 */
static int
pppwrite(tp, uio, flag)
    register struct tty *tp;
    struct uio *uio;
    int flag;
{
    register struct ppp_softc *sc = ppp_for_tty(tp);
    struct mbuf *m, *m0, **mp;
    struct sockaddr dst;
    int len, error, s;

    if ((tp->t_state & TS_CONNECTED) == 0)
	return 0;		/* wrote 0 bytes */
    if (tp->t_line != PPPDISC)
	return (EINVAL);
    if (sc == NULL)
	return EIO;
    if (uio->uio_resid > sc->sc_if.if_mtu + PPP_HDRLEN ||
	uio->uio_resid < PPP_HDRLEN)
	return (EMSGSIZE);

    s = spltty();
    for (mp = &m0; uio->uio_resid; mp = &m->m_next) {
	MGET(m, M_TRYWAIT, MT_DATA);
	if ((*mp = m) == NULL) {
	    m_freem(m0);
	    splx(s);
	    return (ENOBUFS);
	}
	m->m_len = 0;
	if (uio->uio_resid >= MCLBYTES / 2)
	    MCLGET(m, M_DONTWAIT);
	len = M_TRAILINGSPACE(m);
	if (len > uio->uio_resid)
	    len = uio->uio_resid;
	if ((error = uiomove(mtod(m, u_char *), len, uio)) != 0) {
	    m_freem(m0);
	    splx(s);
	    return (error);
	}
	m->m_len = len;
    }
    dst.sa_family = AF_UNSPEC;
    bcopy(mtod(m0, u_char *), dst.sa_data, PPP_HDRLEN);
    m0->m_data += PPP_HDRLEN;
    m0->m_len -= PPP_HDRLEN;

    /* call the upper layer to "transmit" it... */
    error = pppoutput(&sc->sc_if, m0, &dst, (struct rtentry *)0);
    splx(s);
    return (error);
}

/*
 * Line specific (tty) ioctl routine.
 * This discipline requires that tty device drivers call
 * the line specific l_ioctl routine from their ioctl routines.
 */
/* ARGSUSED */
static int
ppptioctl(tp, cmd, data, flag, td)
    struct tty *tp;
    u_long cmd;
    caddr_t data;
    int flag;
    struct thread *td;
{
    struct ppp_softc *sc = ppp_for_tty(tp);
    int error, s;

    if (sc == NULL || tp != (struct tty *) sc->sc_devp)
	return (ENOIOCTL);

    error = 0;
    switch (cmd) {
    case PPPIOCSASYNCMAP:
	if ((error = suser(td)) != 0)
	    break;
	sc->sc_asyncmap[0] = *(u_int *)data;
	break;

    case PPPIOCGASYNCMAP:
	*(u_int *)data = sc->sc_asyncmap[0];
	break;

    case PPPIOCSRASYNCMAP:
	if ((error = suser(td)) != 0)
	    break;
	sc->sc_rasyncmap = *(u_int *)data;
	break;

    case PPPIOCGRASYNCMAP:
	*(u_int *)data = sc->sc_rasyncmap;
	break;

    case PPPIOCSXASYNCMAP:
	if ((error = suser(td)) != 0)
	    break;
	s = spltty();
	bcopy(data, sc->sc_asyncmap, sizeof(sc->sc_asyncmap));
	sc->sc_asyncmap[1] = 0;		    /* mustn't escape 0x20 - 0x3f */
	sc->sc_asyncmap[2] &= ~0x40000000;  /* mustn't escape 0x5e */
	sc->sc_asyncmap[3] |= 0x60000000;   /* must escape 0x7d, 0x7e */
	splx(s);
	break;

    case PPPIOCGXASYNCMAP:
	bcopy(sc->sc_asyncmap, data, sizeof(sc->sc_asyncmap));
	break;

    default:
	error = pppioctl(sc, cmd, data, flag, td);
	if (error == 0 && cmd == PPPIOCSMRU)
	    pppgetm(sc);
    }

    return error;
}

/*
 * FCS lookup table as calculated by genfcstab.
 */
static u_short fcstab[256] = {
	0x0000,	0x1189,	0x2312,	0x329b,	0x4624,	0x57ad,	0x6536,	0x74bf,
	0x8c48,	0x9dc1,	0xaf5a,	0xbed3,	0xca6c,	0xdbe5,	0xe97e,	0xf8f7,
	0x1081,	0x0108,	0x3393,	0x221a,	0x56a5,	0x472c,	0x75b7,	0x643e,
	0x9cc9,	0x8d40,	0xbfdb,	0xae52,	0xdaed,	0xcb64,	0xf9ff,	0xe876,
	0x2102,	0x308b,	0x0210,	0x1399,	0x6726,	0x76af,	0x4434,	0x55bd,
	0xad4a,	0xbcc3,	0x8e58,	0x9fd1,	0xeb6e,	0xfae7,	0xc87c,	0xd9f5,
	0x3183,	0x200a,	0x1291,	0x0318,	0x77a7,	0x662e,	0x54b5,	0x453c,
	0xbdcb,	0xac42,	0x9ed9,	0x8f50,	0xfbef,	0xea66,	0xd8fd,	0xc974,
	0x4204,	0x538d,	0x6116,	0x709f,	0x0420,	0x15a9,	0x2732,	0x36bb,
	0xce4c,	0xdfc5,	0xed5e,	0xfcd7,	0x8868,	0x99e1,	0xab7a,	0xbaf3,
	0x5285,	0x430c,	0x7197,	0x601e,	0x14a1,	0x0528,	0x37b3,	0x263a,
	0xdecd,	0xcf44,	0xfddf,	0xec56,	0x98e9,	0x8960,	0xbbfb,	0xaa72,
	0x6306,	0x728f,	0x4014,	0x519d,	0x2522,	0x34ab,	0x0630,	0x17b9,
	0xef4e,	0xfec7,	0xcc5c,	0xddd5,	0xa96a,	0xb8e3,	0x8a78,	0x9bf1,
	0x7387,	0x620e,	0x5095,	0x411c,	0x35a3,	0x242a,	0x16b1,	0x0738,
	0xffcf,	0xee46,	0xdcdd,	0xcd54,	0xb9eb,	0xa862,	0x9af9,	0x8b70,
	0x8408,	0x9581,	0xa71a,	0xb693,	0xc22c,	0xd3a5,	0xe13e,	0xf0b7,
	0x0840,	0x19c9,	0x2b52,	0x3adb,	0x4e64,	0x5fed,	0x6d76,	0x7cff,
	0x9489,	0x8500,	0xb79b,	0xa612,	0xd2ad,	0xc324,	0xf1bf,	0xe036,
	0x18c1,	0x0948,	0x3bd3,	0x2a5a,	0x5ee5,	0x4f6c,	0x7df7,	0x6c7e,
	0xa50a,	0xb483,	0x8618,	0x9791,	0xe32e,	0xf2a7,	0xc03c,	0xd1b5,
	0x2942,	0x38cb,	0x0a50,	0x1bd9,	0x6f66,	0x7eef,	0x4c74,	0x5dfd,
	0xb58b,	0xa402,	0x9699,	0x8710,	0xf3af,	0xe226,	0xd0bd,	0xc134,
	0x39c3,	0x284a,	0x1ad1,	0x0b58,	0x7fe7,	0x6e6e,	0x5cf5,	0x4d7c,
	0xc60c,	0xd785,	0xe51e,	0xf497,	0x8028,	0x91a1,	0xa33a,	0xb2b3,
	0x4a44,	0x5bcd,	0x6956,	0x78df,	0x0c60,	0x1de9,	0x2f72,	0x3efb,
	0xd68d,	0xc704,	0xf59f,	0xe416,	0x90a9,	0x8120,	0xb3bb,	0xa232,
	0x5ac5,	0x4b4c,	0x79d7,	0x685e,	0x1ce1,	0x0d68,	0x3ff3,	0x2e7a,
	0xe70e,	0xf687,	0xc41c,	0xd595,	0xa12a,	0xb0a3,	0x8238,	0x93b1,
	0x6b46,	0x7acf,	0x4854,	0x59dd,	0x2d62,	0x3ceb,	0x0e70,	0x1ff9,
	0xf78f,	0xe606,	0xd49d,	0xc514,	0xb1ab,	0xa022,	0x92b9,	0x8330,
	0x7bc7,	0x6a4e,	0x58d5,	0x495c,	0x3de3,	0x2c6a,	0x1ef1,	0x0f78
};

/*
 * Calculate a new FCS given the current FCS and the new data.
 */
static u_short
pppfcs(u_short fcs, u_char *cp, int len)
{
    while (len--)
	fcs = PPP_FCS(fcs, *cp++);
    return (fcs);
}

/*
 * This gets called at splsoftnet from if_ppp.c at various times
 * when there is data ready to be sent.
 */
static void
pppasyncstart(sc)
    register struct ppp_softc *sc;
{
    register struct tty *tp = (struct tty *) sc->sc_devp;
    register struct mbuf *m;
    register int len;
    register u_char *start, *stop, *cp;
    int n, ndone, done, idle;
    int s;

    idle = 0;
    /* XXX assumes atomic access to *tp although we're not at spltty(). */
    while (CCOUNT(&tp->t_outq) < PPP_HIWAT) {
	/*
	 * See if we have an existing packet partly sent.
	 * If not, get a new packet and start sending it.
	 */
	m = sc->sc_outm;
	if (m == NULL) {
	    /*
	     * Get another packet to be sent.
	     */
	    m = ppp_dequeue(sc);
	    if (m == NULL) {
		idle = 1;
		break;
	    }

	    /*
	     * The extra PPP_FLAG will start up a new packet, and thus
	     * will flush any accumulated garbage.  We do this whenever
	     * the line may have been idle for some time.
	     */
	    /* XXX as above. */
	    if (CCOUNT(&tp->t_outq) == 0) {
		++sc->sc_stats.ppp_obytes;
		(void) putc(PPP_FLAG, &tp->t_outq);
	    }

	    /* Calculate the FCS for the first mbuf's worth. */
	    sc->sc_outfcs = pppfcs(PPP_INITFCS, mtod(m, u_char *), m->m_len);
	    getmicrotime(&sc->sc_if.if_lastchange);
	}

	for (;;) {
	    start = mtod(m, u_char *);
	    len = m->m_len;
	    stop = start + len;
	    while (len > 0) {
		/*
		 * Find out how many bytes in the string we can
		 * handle without doing something special.
		 */
		for (cp = start; cp < stop; cp++)
		    if (ESCAPE_P(*cp))
			break;
		n = cp - start;
		if (n) {
		    /* NetBSD (0.9 or later), 4.3-Reno or similar. */
		    ndone = n - b_to_q(start, n, &tp->t_outq);
		    len -= ndone;
		    start += ndone;
		    sc->sc_stats.ppp_obytes += ndone;

		    if (ndone < n)
			break;	/* packet doesn't fit */
		}
		/*
		 * If there are characters left in the mbuf,
		 * the first one must be special.
		 * Put it out in a different form.
		 */
		if (len) {
		    s = spltty();
		    if (putc(PPP_ESCAPE, &tp->t_outq)) {
			splx(s);
			break;
		    }
		    if (putc(*start ^ PPP_TRANS, &tp->t_outq)) {
			(void) unputc(&tp->t_outq);
			splx(s);
			break;
		    }
		    splx(s);
		    sc->sc_stats.ppp_obytes += 2;
		    start++;
		    len--;
		}
	    }

	    /*
	     * If we didn't empty this mbuf, remember where we're up to.
	     * If we emptied the last mbuf, try to add the FCS and closing
	     * flag, and if we can't, leave sc_outm pointing to m, but with
	     * m->m_len == 0, to remind us to output the FCS and flag later.
	     */
	    done = len == 0;
	    if (done && m->m_next == NULL) {
		u_char *p, *q;
		int c;
		u_char endseq[8];

		/*
		 * We may have to escape the bytes in the FCS.
		 */
		p = endseq;
		c = ~sc->sc_outfcs & 0xFF;
		if (ESCAPE_P(c)) {
		    *p++ = PPP_ESCAPE;
		    *p++ = c ^ PPP_TRANS;
		} else
		    *p++ = c;
		c = (~sc->sc_outfcs >> 8) & 0xFF;
		if (ESCAPE_P(c)) {
		    *p++ = PPP_ESCAPE;
		    *p++ = c ^ PPP_TRANS;
		} else
		    *p++ = c;
		*p++ = PPP_FLAG;

		/*
		 * Try to output the FCS and flag.  If the bytes
		 * don't all fit, back out.
		 */
		s = spltty();
		for (q = endseq; q < p; ++q)
		    if (putc(*q, &tp->t_outq)) {
			done = 0;
			for (; q > endseq; --q)
			    unputc(&tp->t_outq);
			break;
		    }
		splx(s);
		if (done)
		    sc->sc_stats.ppp_obytes += q - endseq;
	    }

	    if (!done) {
		/* remember where we got to */
		m->m_data = start;
		m->m_len = len;
		break;
	    }

	    /* Finished with this mbuf; free it and move on. */
	    m = m_free(m);
	    if (m == NULL) {
		/* Finished a packet */
		break;
	    }
	    sc->sc_outfcs = pppfcs(sc->sc_outfcs, mtod(m, u_char *), m->m_len);
	}

	/*
	 * If m == NULL, we have finished a packet.
	 * If m != NULL, we've either done as much work this time
	 * as we need to, or else we've filled up the output queue.
	 */
	sc->sc_outm = m;
	if (m)
	    break;
    }

    /* Call pppstart to start output again if necessary. */
    s = spltty();
    pppstart(tp);

    /*
     * This timeout is needed for operation on a pseudo-tty,
     * because the pty code doesn't call pppstart after it has
     * drained the t_outq.
     */
    if (!idle && (sc->sc_flags & SC_TIMEOUT) == 0) {
	sc->sc_ch = timeout(ppp_timeout, (void *) sc, 1);
	sc->sc_flags |= SC_TIMEOUT;
    }

    splx(s);
}

/*
 * This gets called when a received packet is placed on
 * the inq, at splsoftnet. The pppd daemon is to be woken up to do a read().
 */
static void
pppasyncctlp(sc)
    struct ppp_softc *sc;
{
    struct tty *tp;
    int s;

    /* Put a placeholder byte in canq for ttselect()/ttnread(). */
    s = spltty();
    tp = (struct tty *) sc->sc_devp;
    putc(0, &tp->t_canq);
    ttwakeup(tp);
    splx(s);
}

/*
 * Start output on async tty interface.  If the transmit queue
 * has drained sufficiently, arrange for pppasyncstart to be
 * called later at splsoftnet.
 * Called at spltty or higher.
 */
static int
pppstart(tp)
    register struct tty *tp;
{
    register struct ppp_softc *sc = ppp_for_tty(tp);

    /*
     * Call output process whether or not there is any output.
     * We are being called in lieu of ttstart and must do what it would.
     */
    if (tp->t_oproc != NULL)
	(*tp->t_oproc)(tp);

    /*
     * If the transmit queue has drained and the tty has not hung up
     * or been disconnected from the ppp unit, then tell if_ppp.c that
     * we need more output.
     */
    if (CCOUNT(&tp->t_outq) < PPP_LOWAT
	&& !((tp->t_state & TS_CONNECTED) == 0)
	&& sc != NULL) {
	ppp_restart(sc);
    }

    return 0;
}

/*
 * Timeout routine - try to start some more output.
 */
static void
ppp_timeout(x)
    void *x;
{
    struct ppp_softc *sc = (struct ppp_softc *) x;
    struct tty *tp = (struct tty *) sc->sc_devp;
    int s;

    s = spltty();
    sc->sc_flags &= ~SC_TIMEOUT;
    pppstart(tp);
    splx(s);
}

/*
 * Allocate enough mbuf to handle current MRU.
 */
static void
pppgetm(sc)
    register struct ppp_softc *sc;
{
    struct mbuf *m, **mp;
    int len;

    mp = &sc->sc_m;
    for (len = sc->sc_mru + PPP_HDRLEN + PPP_FCSLEN; len > 0; ){
	if ((m = *mp) == NULL) {
	    MGETHDR(m, M_DONTWAIT, MT_DATA);
	    if (m == NULL)
		break;
	    *mp = m;
	    MCLGET(m, M_DONTWAIT);
	}
	len -= M_DATASIZE(m);
	mp = &m->m_next;
    }
}

/*
 * tty interface receiver interrupt.
 */
static unsigned paritytab[8] = {
    0x96696996, 0x69969669, 0x69969669, 0x96696996,
    0x69969669, 0x96696996, 0x96696996, 0x69969669
};

/*
 * Called when character is available from device driver.
 * Only guaranteed to be at splsofttty() or spltty()
 * This is safe to be called while the upper half's netisr is preempted.
 */
static int
pppinput(c, tp)
    int c;
    register struct tty *tp;
{
    register struct ppp_softc *sc;
    struct mbuf *m;
    int ilen, s;

    sc = ppp_for_tty(tp);
    if (sc == NULL)
	return 0;

    ++tk_nin;
    ++sc->sc_stats.ppp_ibytes;

    if ((tp->t_state & TS_CONNECTED) == 0) {
	if (sc->sc_flags & SC_DEBUG)
	    if_printf(&sc->sc_if, "no carrier\n");
	goto flush;
    }

    if (c & TTY_ERRORMASK) {
	/* framing error or overrun on this char - abort packet */
	if (sc->sc_flags & SC_DEBUG)
	    if_printf(&sc->sc_if, "line error %x\n", c & TTY_ERRORMASK);
	goto flush;
    }

    c &= TTY_CHARMASK;

    /*
     * Handle software flow control of output.
     */
    if (tp->t_iflag & IXON) {
	if (c == tp->t_cc[VSTOP] && tp->t_cc[VSTOP] != _POSIX_VDISABLE) {
	    if ((tp->t_state & TS_TTSTOP) == 0) {
		tp->t_state |= TS_TTSTOP;
		tp->t_stop(tp, 0);
	    }
	    return 0;
	}
	if (c == tp->t_cc[VSTART] && tp->t_cc[VSTART] != _POSIX_VDISABLE) {
	    tp->t_state &= ~TS_TTSTOP;
	    if (tp->t_oproc != NULL)
		(*tp->t_oproc)(tp);
	    return 0;
	}
    }

    s = spltty();
    if (c & 0x80)
	sc->sc_flags |= SC_RCV_B7_1;
    else
	sc->sc_flags |= SC_RCV_B7_0;
    if (paritytab[c >> 5] & (1 << (c & 0x1F)))
	sc->sc_flags |= SC_RCV_ODDP;
    else
	sc->sc_flags |= SC_RCV_EVNP;
    splx(s);

    if (sc->sc_flags & SC_LOG_RAWIN)
	ppplogchar(sc, c);

    if (c == PPP_FLAG) {
	ilen = sc->sc_ilen;
	sc->sc_ilen = 0;

	if (sc->sc_rawin_count > 0) 
	    ppplogchar(sc, -1);

	/*
	 * If SC_ESCAPED is set, then we've seen the packet
	 * abort sequence "}~".
	 */
	if (sc->sc_flags & (SC_FLUSH | SC_ESCAPED)
	    || (ilen > 0 && sc->sc_fcs != PPP_GOODFCS)) {
	    s = spltty();
	    sc->sc_flags |= SC_PKTLOST;	/* note the dropped packet */
	    if ((sc->sc_flags & (SC_FLUSH | SC_ESCAPED)) == 0){
		if (sc->sc_flags & SC_DEBUG)
		    if_printf(&sc->sc_if, "bad fcs %x, pkt len %d\n",
			   sc->sc_fcs, ilen);
		sc->sc_if.if_ierrors++;
		sc->sc_stats.ppp_ierrors++;
	    } else
		sc->sc_flags &= ~(SC_FLUSH | SC_ESCAPED);
	    splx(s);
	    return 0;
	}

	if (ilen < PPP_HDRLEN + PPP_FCSLEN) {
	    if (ilen) {
		if (sc->sc_flags & SC_DEBUG)
		    if_printf(&sc->sc_if, "too short (%d)\n", ilen);
		s = spltty();
		sc->sc_if.if_ierrors++;
		sc->sc_stats.ppp_ierrors++;
		sc->sc_flags |= SC_PKTLOST;
		splx(s);
	    }
	    return 0;
	}

	/*
	 * Remove FCS trailer.  Somewhat painful...
	 */
	ilen -= 2;
	if (--sc->sc_mc->m_len == 0) {
	    for (m = sc->sc_m; m->m_next != sc->sc_mc; m = m->m_next)
		;
	    sc->sc_mc = m;
	}
	sc->sc_mc->m_len--;

	/* excise this mbuf chain */
	m = sc->sc_m;
	sc->sc_m = sc->sc_mc->m_next;
	sc->sc_mc->m_next = NULL;

	ppppktin(sc, m, sc->sc_flags & SC_PKTLOST);
	if (sc->sc_flags & SC_PKTLOST) {
	    s = spltty();
	    sc->sc_flags &= ~SC_PKTLOST;
	    splx(s);
	}

	pppgetm(sc);
	return 0;
    }

    if (sc->sc_flags & SC_FLUSH) {
	if (sc->sc_flags & SC_LOG_FLUSH)
	    ppplogchar(sc, c);
	return 0;
    }

    if (c < 0x20 && (sc->sc_rasyncmap & (1 << c)))
	return 0;

    s = spltty();
    if (sc->sc_flags & SC_ESCAPED) {
	sc->sc_flags &= ~SC_ESCAPED;
	c ^= PPP_TRANS;
    } else if (c == PPP_ESCAPE) {
	sc->sc_flags |= SC_ESCAPED;
	splx(s);
	return 0;
    }
    splx(s);

    /*
     * Initialize buffer on first octet received.
     * First octet could be address or protocol (when compressing
     * address/control).
     * Second octet is control.
     * Third octet is first or second (when compressing protocol)
     * octet of protocol.
     * Fourth octet is second octet of protocol.
     */
    if (sc->sc_ilen == 0) {
	/* reset the first input mbuf */
	if (sc->sc_m == NULL) {
	    pppgetm(sc);
	    if (sc->sc_m == NULL) {
		if (sc->sc_flags & SC_DEBUG)
		    if_printf(&sc->sc_if, "no input mbufs!\n");
		goto flush;
	    }
	}
	m = sc->sc_m;
	m->m_len = 0;
	m->m_data = M_DATASTART(sc->sc_m);
	sc->sc_mc = m;
	sc->sc_mp = mtod(m, char *);
	sc->sc_fcs = PPP_INITFCS;
	if (c != PPP_ALLSTATIONS) {
	    if (sc->sc_flags & SC_REJ_COMP_AC) {
		if (sc->sc_flags & SC_DEBUG)
		    if_printf(&sc->sc_if,
		              "garbage received: 0x%x (need 0xFF)\n", c);
		goto flush;
	    }
	    *sc->sc_mp++ = PPP_ALLSTATIONS;
	    *sc->sc_mp++ = PPP_UI;
	    sc->sc_ilen += 2;
	    m->m_len += 2;
	}
    }
    if (sc->sc_ilen == 1 && c != PPP_UI) {
	if (sc->sc_flags & SC_DEBUG)
	    if_printf(&sc->sc_if, "missing UI (0x3), got 0x%x\n", c);
	goto flush;
    }
    if (sc->sc_ilen == 2 && (c & 1) == 1) {
	/* a compressed protocol */
	*sc->sc_mp++ = 0;
	sc->sc_ilen++;
	sc->sc_mc->m_len++;
    }
    if (sc->sc_ilen == 3 && (c & 1) == 0) {
	if (sc->sc_flags & SC_DEBUG)
	    if_printf(&sc->sc_if, "bad protocol %x\n",
		      (sc->sc_mp[-1] << 8) + c);
	goto flush;
    }

    /* packet beyond configured mru? */
    if (++sc->sc_ilen > sc->sc_mru + PPP_HDRLEN + PPP_FCSLEN) {
	if (sc->sc_flags & SC_DEBUG)
	    if_printf(&sc->sc_if, "packet too big\n");
	goto flush;
    }

    /* is this mbuf full? */
    m = sc->sc_mc;
    if (M_TRAILINGSPACE(m) <= 0) {
	if (m->m_next == NULL) {
	    pppgetm(sc);
	    if (m->m_next == NULL) {
		if (sc->sc_flags & SC_DEBUG)
		    if_printf(&sc->sc_if, "too few input mbufs!\n");
		goto flush;
	    }
	}
	sc->sc_mc = m = m->m_next;
	m->m_len = 0;
	m->m_data = M_DATASTART(m);
	sc->sc_mp = mtod(m, char *);
    }

    ++m->m_len;
    *sc->sc_mp++ = c;
    sc->sc_fcs = PPP_FCS(sc->sc_fcs, c);
    return 0;

 flush:
    if (!(sc->sc_flags & SC_FLUSH)) {
	s = spltty();
	sc->sc_if.if_ierrors++;
	sc->sc_stats.ppp_ierrors++;
	sc->sc_flags |= SC_FLUSH;
	splx(s);
	if (sc->sc_flags & SC_LOG_FLUSH)
	    ppplogchar(sc, c);
    }
    return 0;
}

#define MAX_DUMP_BYTES	128

static void
ppplogchar(sc, c)
    struct ppp_softc *sc;
    int c;
{
    if (c >= 0)
	sc->sc_rawin[sc->sc_rawin_count++] = c;
    if (sc->sc_rawin_count >= sizeof(sc->sc_rawin)
	|| (c < 0 && sc->sc_rawin_count > 0)) {
	printf("%s input: %*D", sc->sc_if.if_xname,
		sc->sc_rawin_count, sc->sc_rawin, " ");
	sc->sc_rawin_count = 0;
    }
}
