/*      $NetBSD: exec_prot_support.c,v 1.1 2011/07/18 23:16:09 jym Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jean-Yves Migeon.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: exec_prot_support.c,v 1.1 2011/07/18 23:16:09 jym Exp $");

#include <stdlib.h>
#include <sys/sysctl.h>

#include "../../common/exec_prot.h"

/*
 * Support for executable space protection has always been erratic under i386.
 * Originally IA-32 can't do per-page execute permission, so it is
 * implemented using different executable segments for %cs (code segment).
 * This only allows coarse grained protection, especially when memory starts
 * being fragmented.
 * Later, PAE was introduced together with a NX/XD bit in the page table
 * entry to offer per-page permission.
 */
int
exec_prot_support(void)
{
	int pae;
	size_t pae_len = sizeof(pae);

	if (sysctlbyname("machdep.pae", &pae, &pae_len, NULL, 0) == -1)
		return PARTIAL_XP;

	if (pae == 1) {
		if (system("cpuctl identify 0 | grep -q NOX") == 0 ||
		    system("cpuctl identify 0 | grep -q XD") == 0)
			return PERPAGE_XP;
	}
	
	return PARTIAL_XP;
}
