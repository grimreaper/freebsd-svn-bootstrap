/*
 * Copyright (C) 2004, 2005, 2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000, 2001  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: assert_p.h,v 1.14 2007/06/19 23:47:22 tbox Exp $ */

#ifndef LWRES_ASSERT_P_H
#define LWRES_ASSERT_P_H 1

/*! \file */

#include <assert.h>		/* Required for assert() prototype. */

#define REQUIRE(x)		assert(x)
#define INSIST(x)		assert(x)

#define UNUSED(x)		((void)(x))

#define SPACE_OK(b, s)		(LWRES_BUFFER_AVAILABLECOUNT(b) >= (s))
#define SPACE_REMAINING(b, s)	(LWRES_BUFFER_REMAINING(b) >= (s))

#endif /* LWRES_ASSERT_P_H */
