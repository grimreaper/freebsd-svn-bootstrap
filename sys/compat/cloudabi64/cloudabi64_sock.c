/*-
 * Copyright (c) 2015-2017 Nuxi, https://nuxi.nl/
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/syscallsubr.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <contrib/cloudabi/cloudabi64_types.h>

#include <compat/cloudabi/cloudabi_util.h>

#include <compat/cloudabi64/cloudabi64_proto.h>
#include <compat/cloudabi64/cloudabi64_util.h>

static MALLOC_DEFINE(M_SOCKET, "socket", "CloudABI socket");

int
cloudabi64_sys_sock_recv(struct thread *td,
    struct cloudabi64_sys_sock_recv_args *uap)
{
	struct sockaddr_storage ss;
	cloudabi64_recv_in_t ri;
	cloudabi64_recv_out_t ro = {};
	cloudabi64_iovec_t iovobj;
	struct msghdr msghdr = {};
	const cloudabi64_iovec_t *user_iov;
	size_t i;
	int error;

	error = copyin(uap->in, &ri, sizeof(ri));
	if (error != 0)
		return (error);

	/* Convert results in cloudabi_recv_in_t to struct msghdr. */
	if (ri.ri_data_len > UIO_MAXIOV)
		return (EINVAL);
	msghdr.msg_iovlen = ri.ri_data_len;
	msghdr.msg_iov = malloc(msghdr.msg_iovlen * sizeof(struct iovec),
	    M_SOCKET, M_WAITOK);
	user_iov = TO_PTR(ri.ri_data);
	for (i = 0; i < msghdr.msg_iovlen; i++) {
		error = copyin(&user_iov[i], &iovobj, sizeof(iovobj));
		if (error != 0) {
			free(msghdr.msg_iov, M_SOCKET);
			return (error);
		}
		msghdr.msg_iov[i].iov_base = TO_PTR(iovobj.buf);
		msghdr.msg_iov[i].iov_len = iovobj.buf_len;
	}
	msghdr.msg_name = &ss;
	msghdr.msg_namelen = sizeof(ss);
	if (ri.ri_flags & CLOUDABI_MSG_PEEK)
		msghdr.msg_flags |= MSG_PEEK;
	if (ri.ri_flags & CLOUDABI_MSG_WAITALL)
		msghdr.msg_flags |= MSG_WAITALL;

	/* TODO(ed): Add file descriptor passing. */
	error = kern_recvit(td, uap->sock, &msghdr, UIO_SYSSPACE, NULL);
	free(msghdr.msg_iov, M_SOCKET);
	if (error != 0)
		return (error);

	/* Convert results in msghdr to cloudabi_recv_out_t. */
	ro.ro_datalen = td->td_retval[0];
	cloudabi_convert_sockaddr((struct sockaddr *)&ss,
	    MIN(msghdr.msg_namelen, sizeof(ss)), &ro.ro_peername);
	td->td_retval[0] = 0;
	return (copyout(&ro, uap->out, sizeof(ro)));
}

int
cloudabi64_sys_sock_send(struct thread *td,
    struct cloudabi64_sys_sock_send_args *uap)
{
	cloudabi64_send_in_t si;
	cloudabi64_send_out_t so = {};
	cloudabi64_ciovec_t iovobj;
	struct iovec *iov;
	const cloudabi64_ciovec_t *user_iov;
	size_t datalen, i;
	int error;

	error = copyin(uap->in, &si, sizeof(si));
	if (error != 0)
		return (error);

	/* Convert iovecs to native format. */
	if (si.si_data_len > UIO_MAXIOV)
		return (EINVAL);
	iov = malloc(si.si_data_len * sizeof(struct iovec),
	    M_SOCKET, M_WAITOK);
	user_iov = TO_PTR(si.si_data);
	for (i = 0; i < si.si_data_len; i++) {
		error = copyin(&user_iov[i], &iovobj, sizeof(iovobj));
		if (error != 0) {
			free(iov, M_SOCKET);
			return (error);
		}
		iov[i].iov_base = TO_PTR(iovobj.buf);
		iov[i].iov_len = iovobj.buf_len;
	}

	error = cloudabi_sock_send(td, uap->sock, iov, si.si_data_len,
	    TO_PTR(si.si_fds), si.si_fds_len, si.si_flags, &datalen);
	free(iov, M_SOCKET);
	if (error != 0)
		return (error);

	so.so_datalen = datalen;
	return (copyout(&so, uap->out, sizeof(so)));
}
