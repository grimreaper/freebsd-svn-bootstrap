/*-
 * Copyright (c) 2004 Tim J. Robbins.
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
 *
 * $FreeBSD$
 */

#ifndef _MBLOCAL_H_
#define	_MBLOCAL_H_

#include <stddef.h>	/* XXX for rune_t */

/*
 * Conversion function pointers for current encoding.
 */
extern size_t (*__mbrtowc)(wchar_t * __restrict, const char * __restrict,
    size_t, mbstate_t * __restrict);
extern int (*__mbsinit)(const mbstate_t *);
extern size_t (*__mbsrtowcs)(wchar_t * __restrict, const char ** __restrict,
    size_t, mbstate_t * __restrict);
extern size_t (*__wcrtomb)(char * __restrict, wchar_t, mbstate_t * __restrict);
extern size_t (*__wcsrtombs)(char * __restrict, const wchar_t ** __restrict,
    size_t, mbstate_t * __restrict);

/*
 * Conversion functions for "NONE"/C/POSIX encoding.
 */
extern size_t _none_mbrtowc(wchar_t * __restrict, const char * __restrict,
    size_t, mbstate_t * __restrict);
extern int _none_mbsinit(const mbstate_t *);
extern size_t _none_mbsrtowcs(wchar_t * __restrict, const char ** __restrict,
    size_t, mbstate_t * __restrict);
extern size_t _none_wcrtomb(char * __restrict, wchar_t,
    mbstate_t * __restrict);
extern size_t _none_wcsrtombs(char * __restrict, const wchar_t ** __restrict,
    size_t, mbstate_t * __restrict);

extern size_t __mbsrtowcs_std(wchar_t * __restrict, const char ** __restrict,
    size_t, mbstate_t * __restrict);
extern size_t __wcsrtombs_std(char * __restrict, const wchar_t ** __restrict,
    size_t, mbstate_t * __restrict);

/*
 * Rune emulation functions.
 */
extern rune_t __emulated_sgetrune(const char *, size_t, const char **);
extern int __emulated_sputrune(rune_t, char *, size_t, char **);

#endif	/* _MBLOCAL_H_ */
