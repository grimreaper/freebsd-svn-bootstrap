/*-
 * Copyright (c) 2009 Robert N. M. Watson
 * All rights reserved.
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
 * When an interface has been detached but not yet freed, we set the various
 * ifnet function pointers to "ifdead" versions.  This prevents unexpected
 * calls from the network stack into the device driver after if_detach() has
 * returned.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>

static int
ifdead_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *sa,
    struct route *ro)
{

	m_freem(m);
	return (ENXIO);
}

static void
ifdead_input(struct ifnet *ifp, struct mbuf *m)
{

	m_freem(m);
}

static void
ifdead_start(struct ifnet *ifp)
{

}

static int
ifdead_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{

	return (ENXIO);
}

static void
ifdead_watchdog(struct ifnet *ifp)
{

}

static int
ifdead_resolvemulti(struct ifnet *ifp, struct sockaddr **llsa,
    struct sockaddr *sa)
{

	*llsa = NULL;
	return (ENXIO);
}

static void
ifdead_qflush(struct ifnet *ifp)
{

}

static int
ifdead_transmit(struct ifnet *ifp, struct mbuf *m)
{

	m_freem(m);
	return (ENXIO);
}

void
if_dead(struct ifnet *ifp)
{

	ifp->if_output = ifdead_output;
	ifp->if_input = ifdead_input;
	ifp->if_start = ifdead_start;
	ifp->if_ioctl = ifdead_ioctl;
	ifp->if_watchdog = ifdead_watchdog;
	ifp->if_resolvemulti = ifdead_resolvemulti;
	ifp->if_qflush = ifdead_qflush;
	ifp->if_transmit = ifdead_transmit;
}
