/*-
 * Copyright (c) 1999 Robert N. M. Watson
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
 *	$FreeBSD$
 */
/*
 * acl_get_file - syscall wrapper for retrieving ACL by filename
 */

#include <sys/types.h>
#include <sys/acl.h>
#include <sys/errno.h>
#include <stdlib.h>

acl_t
acl_get_file(const char *path_p, acl_type_t type)
{
	struct acl	*aclp;
	int	error;

	aclp = acl_init(MAX_ACL_ENTRIES);
	if (!aclp) {
		return (0);
	}

	error = acl_syscall_get_file(path_p, type, aclp);
	if (error) {
		acl_free(aclp);
		return (0);
	}

	return (aclp);
}


acl_t
acl_get_fd(int fd, acl_type_t type)
{
	struct acl	*aclp;
	int	error;

	aclp = acl_init(MAX_ACL_ENTRIES);
	if (!aclp) {
		return (0);
	}

	error = acl_syscall_get_fd(fd, type, aclp);
	if (error) {
		acl_free(aclp);
		return (0);
	}

	return (aclp);
}
