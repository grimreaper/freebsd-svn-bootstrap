/*
 * Copyright (c) 1990, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)fifo_vnops.c	8.10 (Berkeley) 5/27/95
 * $Id$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/namei.h>
#include <sys/unistd.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/un.h>
#include <miscfs/fifofs/fifo.h>

/*
 * This structure is associated with the FIFO vnode and stores
 * the state associated with the FIFO.
 */
struct fifoinfo {
	struct socket	*fi_readsock;
	struct socket	*fi_writesock;
	long		fi_readers;
	long		fi_writers;
};

static int	fifo_ebadf __P((void));
static int	fifo_print __P((struct vop_print_args *));

vop_t **fifo_vnodeop_p;
static struct vnodeopv_entry_desc fifo_vnodeop_entries[] = {
	{ &vop_default_desc, (vop_t *)vn_default_error },
	{ &vop_lookup_desc, (vop_t *)fifo_lookup },	/* lookup */
	{ &vop_create_desc, (vop_t *)fifo_create },	/* create */
	{ &vop_mknod_desc, (vop_t *)fifo_mknod },	/* mknod */
	{ &vop_open_desc, (vop_t *)fifo_open },		/* open */
	{ &vop_close_desc, (vop_t *)fifo_close },	/* close */
	{ &vop_access_desc, (vop_t *)fifo_access },	/* access */
	{ &vop_getattr_desc, (vop_t *)fifo_getattr },	/* getattr */
	{ &vop_setattr_desc, (vop_t *)fifo_setattr },	/* setattr */
	{ &vop_read_desc, (vop_t *)fifo_read },		/* read */
	{ &vop_write_desc, (vop_t *)fifo_write },	/* write */
	{ &vop_lease_desc, (vop_t *)fifo_lease_check },	/* lease */
	{ &vop_ioctl_desc, (vop_t *)fifo_ioctl },	/* ioctl */
	{ &vop_select_desc, (vop_t *)fifo_select },	/* select */
	{ &vop_revoke_desc, (vop_t *)fifo_revoke },	/* revoke */
	{ &vop_mmap_desc, (vop_t *)fifo_mmap },		/* mmap */
	{ &vop_fsync_desc, (vop_t *)fifo_fsync },	/* fsync */
	{ &vop_seek_desc, (vop_t *)fifo_seek },		/* seek */
	{ &vop_remove_desc, (vop_t *)fifo_remove },	/* remove */
	{ &vop_link_desc, (vop_t *)fifo_link },		/* link */
	{ &vop_rename_desc, (vop_t *)fifo_rename },	/* rename */
	{ &vop_mkdir_desc, (vop_t *)fifo_mkdir },	/* mkdir */
	{ &vop_rmdir_desc, (vop_t *)fifo_rmdir },	/* rmdir */
	{ &vop_symlink_desc, (vop_t *)fifo_symlink },	/* symlink */
	{ &vop_readdir_desc, (vop_t *)fifo_readdir },	/* readdir */
	{ &vop_readlink_desc, (vop_t *)fifo_readlink },	/* readlink */
	{ &vop_abortop_desc, (vop_t *)fifo_abortop },	/* abortop */
	{ &vop_inactive_desc, (vop_t *)fifo_inactive },	/* inactive */
	{ &vop_reclaim_desc, (vop_t *)fifo_reclaim },	/* reclaim */
	{ &vop_lock_desc, (vop_t *)fifo_lock },		/* lock */
	{ &vop_unlock_desc, (vop_t *)fifo_unlock },	/* unlock */
	{ &vop_bmap_desc, (vop_t *)fifo_bmap },		/* bmap */
	{ &vop_strategy_desc, (vop_t *)fifo_strategy },	/* strategy */
	{ &vop_print_desc, (vop_t *)fifo_print },	/* print */
	{ &vop_islocked_desc, (vop_t *)fifo_islocked },	/* islocked */
	{ &vop_pathconf_desc, (vop_t *)fifo_pathconf },	/* pathconf */
	{ &vop_advlock_desc, (vop_t *)fifo_advlock },	/* advlock */
	{ &vop_blkatoff_desc, (vop_t *)fifo_blkatoff },	/* blkatoff */
	{ &vop_valloc_desc, (vop_t *)fifo_valloc },	/* valloc */
	{ &vop_vfree_desc, (vop_t *)fifo_vfree },	/* vfree */
	{ &vop_truncate_desc, (vop_t *)fifo_truncate },	/* truncate */
	{ &vop_update_desc, (vop_t *)fifo_update },	/* update */
	{ &vop_bwrite_desc, (vop_t *)fifo_bwrite },	/* bwrite */
	{ NULL, NULL }
};
static struct vnodeopv_desc fifo_vnodeop_opv_desc =
	{ &fifo_vnodeop_p, fifo_vnodeop_entries };

VNODEOP_SET(fifo_vnodeop_opv_desc);

/*
 * Trivial lookup routine that always fails.
 */
/* ARGSUSED */
int
fifo_lookup(ap)
	struct vop_lookup_args /* {
		struct vnode * a_dvp;
		struct vnode ** a_vpp;
		struct componentname * a_cnp;
	} */ *ap;
{

	*ap->a_vpp = NULL;
	return (ENOTDIR);
}

/*
 * Open called to set up a new instance of a fifo or
 * to find an active instance of a fifo.
 */
/* ARGSUSED */
int
fifo_open(ap)
	struct vop_open_args /* {
		struct vnode *a_vp;
		int  a_mode;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct fifoinfo *fip;
	struct proc *p = ap->a_p;
	struct socket *rso, *wso;
	int error;
	static char openstr[] = "fifo";

	if ((fip = vp->v_fifoinfo) == NULL) {
		MALLOC(fip, struct fifoinfo *, sizeof(*fip), M_VNODE, M_WAITOK);
		vp->v_fifoinfo = fip;
		error = socreate(AF_LOCAL, &rso, SOCK_STREAM, 0, ap->a_p);
		if (error) {
			free(fip, M_VNODE);
			vp->v_fifoinfo = NULL;
			return (error);
		}
		fip->fi_readsock = rso;
		error = socreate(AF_LOCAL, &wso, SOCK_STREAM, 0, ap->a_p);
		if (error) {
			(void)soclose(rso);
			free(fip, M_VNODE);
			vp->v_fifoinfo = NULL;
			return (error);
		}
		fip->fi_writesock = wso;
		error = unp_connect2(wso, rso);
		if (error) {
			(void)soclose(wso);
			(void)soclose(rso);
			free(fip, M_VNODE);
			vp->v_fifoinfo = NULL;
			return (error);
		}
		fip->fi_readers = fip->fi_writers = 0;
		wso->so_state |= SS_CANTRCVMORE;
		rso->so_state |= SS_CANTSENDMORE;
	}
	if (ap->a_mode & FREAD) {
		fip->fi_readers++;
		if (fip->fi_readers == 1) {
			fip->fi_writesock->so_state &= ~SS_CANTSENDMORE;
			if (fip->fi_writers > 0)
				wakeup((caddr_t)&fip->fi_writers);
		}
	}
	if (ap->a_mode & FWRITE) {
		fip->fi_writers++;
		if (fip->fi_writers == 1) {
			fip->fi_readsock->so_state &= ~SS_CANTRCVMORE;
			if (fip->fi_readers > 0)
				wakeup((caddr_t)&fip->fi_readers);
		}
	}
	if ((ap->a_mode & FREAD) && (ap->a_mode & O_NONBLOCK) == 0) {
		while (fip->fi_writers == 0) {
			VOP_UNLOCK(vp, 0, p);
			error = tsleep((caddr_t)&fip->fi_readers,
			    PCATCH | PSOCK, openstr, 0);
			vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);
			if (error)
				goto bad;
		}
	}
	if (ap->a_mode & FWRITE) {
		if (ap->a_mode & O_NONBLOCK) {
			if (fip->fi_readers == 0) {
				error = ENXIO;
				goto bad;
			}
		} else {
			while (fip->fi_readers == 0) {
				VOP_UNLOCK(vp, 0, p);
				error = tsleep((caddr_t)&fip->fi_writers,
				    PCATCH | PSOCK, openstr, 0);
				vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);
				if (error)
					goto bad;
			}
		}
	}
	return (0);
bad:
	VOP_CLOSE(vp, ap->a_mode, ap->a_cred, p);
	return (error);
}

/*
 * Vnode op for read
 */
/* ARGSUSED */
int
fifo_read(ap)
	struct vop_read_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int  a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	struct uio *uio = ap->a_uio;
	struct socket *rso = ap->a_vp->v_fifoinfo->fi_readsock;
	struct proc *p = uio->uio_procp;
	int error, startresid;

#ifdef DIAGNOSTIC
	if (uio->uio_rw != UIO_READ)
		panic("fifo_read mode");
#endif
	if (uio->uio_resid == 0)
		return (0);
	if (ap->a_ioflag & IO_NDELAY)
		rso->so_state |= SS_NBIO;
	startresid = uio->uio_resid;
	VOP_UNLOCK(ap->a_vp, 0, p);
	error = soreceive(rso, (struct mbuf **)0, uio, (struct mbuf **)0,
	    (struct mbuf **)0, (int *)0);
	vn_lock(ap->a_vp, LK_EXCLUSIVE | LK_RETRY, p);
	/*
	 * Clear EOF indication after first such return.
	 */
	if (uio->uio_resid == startresid)
		rso->so_state &= ~SS_CANTRCVMORE;
	if (ap->a_ioflag & IO_NDELAY)
		rso->so_state &= ~SS_NBIO;
	return (error);
}

/*
 * Vnode op for write
 */
/* ARGSUSED */
int
fifo_write(ap)
	struct vop_write_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int  a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	struct socket *wso = ap->a_vp->v_fifoinfo->fi_writesock;
	struct proc *p = ap->a_uio->uio_procp;
	int error;

#ifdef DIAGNOSTIC
	if (ap->a_uio->uio_rw != UIO_WRITE)
		panic("fifo_write mode");
#endif
	if (ap->a_ioflag & IO_NDELAY)
		wso->so_state |= SS_NBIO;
	VOP_UNLOCK(ap->a_vp, 0, p);
	error = sosend(wso, (struct mbuf *)0, ap->a_uio, 0, (struct mbuf *)0, 0);
	vn_lock(ap->a_vp, LK_EXCLUSIVE | LK_RETRY, p);
	if (ap->a_ioflag & IO_NDELAY)
		wso->so_state &= ~SS_NBIO;
	return (error);
}

/*
 * Device ioctl operation.
 */
/* ARGSUSED */
int
fifo_ioctl(ap)
	struct vop_ioctl_args /* {
		struct vnode *a_vp;
		int  a_command;
		caddr_t  a_data;
		int  a_fflag;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
	struct file filetmp;
	int error;

	if (ap->a_command == FIONBIO)
		return (0);
	if (ap->a_fflag & FREAD) {
		filetmp.f_data = (caddr_t)ap->a_vp->v_fifoinfo->fi_readsock;
		error = soo_ioctl(&filetmp, ap->a_command, ap->a_data, ap->a_p);
		if (error)
			return (error);
	}
	if (ap->a_fflag & FWRITE) {
		filetmp.f_data = (caddr_t)ap->a_vp->v_fifoinfo->fi_writesock;
		error = soo_ioctl(&filetmp, ap->a_command, ap->a_data, ap->a_p);
		if (error)
			return (error);
	}
	return (0);
}

/* ARGSUSED */
int
fifo_select(ap)
	struct vop_select_args /* {
		struct vnode *a_vp;
		int  a_which;
		int  a_fflags;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
	struct file filetmp;
	int ready;

	if (ap->a_fflags & FREAD) {
		filetmp.f_data = (caddr_t)ap->a_vp->v_fifoinfo->fi_readsock;
		ready = soo_select(&filetmp, ap->a_which, ap->a_p);
		if (ready)
			return (ready);
	}
	if (ap->a_fflags & FWRITE) {
		filetmp.f_data = (caddr_t)ap->a_vp->v_fifoinfo->fi_writesock;
		ready = soo_select(&filetmp, ap->a_which, ap->a_p);
		if (ready)
			return (ready);
	}
	return (0);
}

int
fifo_inactive(ap)
	struct vop_inactive_args /* {
		struct vnode *a_vp;
		struct proc *a_p;
	} */ *ap;
{

	VOP_UNLOCK(ap->a_vp, 0, ap->a_p);
	return (0);
}

/*
 * This is a noop, simply returning what one has been given.
 */
int
fifo_bmap(ap)
	struct vop_bmap_args /* {
		struct vnode *a_vp;
		daddr_t  a_bn;
		struct vnode **a_vpp;
		daddr_t *a_bnp;
		int *a_runp;
		int *a_runb;
	} */ *ap;
{

	if (ap->a_vpp != NULL)
		*ap->a_vpp = ap->a_vp;
	if (ap->a_bnp != NULL)
		*ap->a_bnp = ap->a_bn;
	if (ap->a_runp != NULL)
		*ap->a_runp = 0;
	if (ap->a_runb != NULL)
		*ap->a_runb = 0;
	return (0);
}

/*
 * Device close routine
 */
/* ARGSUSED */
int
fifo_close(ap)
	struct vop_close_args /* {
		struct vnode *a_vp;
		int  a_fflag;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct fifoinfo *fip = vp->v_fifoinfo;
	int error1, error2;

	if (ap->a_fflag & FREAD) {
		fip->fi_readers--;
		if (fip->fi_readers == 0)
			socantsendmore(fip->fi_writesock);
	}
	if (ap->a_fflag & FWRITE) {
		fip->fi_writers--;
		if (fip->fi_writers == 0)
			socantrcvmore(fip->fi_readsock);
	}
	if (vp->v_usecount > 1)
		return (0);
	error1 = soclose(fip->fi_readsock);
	error2 = soclose(fip->fi_writesock);
	FREE(fip, M_VNODE);
	vp->v_fifoinfo = NULL;
	if (error1)
		return (error1);
	return (error2);
}


/*
 * Print out internal contents of a fifo vnode.
 */
int
fifo_printinfo(vp)
	struct vnode *vp;
{
	register struct fifoinfo *fip = vp->v_fifoinfo;

	printf(", fifo with %ld readers and %ld writers",
		fip->fi_readers, fip->fi_writers);
	return (0);
}

/*
 * Print out the contents of a fifo vnode.
 */
static int
fifo_print(ap)
	struct vop_print_args /* {
		struct vnode *a_vp;
	} */ *ap;
{

	printf("tag VT_NON");
	fifo_printinfo(ap->a_vp);
	printf("\n");
	return (0);
}

/*
 * Return POSIX pathconf information applicable to fifo's.
 */
int
fifo_pathconf(ap)
	struct vop_pathconf_args /* {
		struct vnode *a_vp;
		int a_name;
		int *a_retval;
	} */ *ap;
{

	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = LINK_MAX;
		return (0);
	case _PC_PIPE_BUF:
		*ap->a_retval = PIPE_BUF;
		return (0);
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		return (0);
	default:
		return (EINVAL);
	}
	/* NOTREACHED */
}

/*
 * Fifo failed operation
 */
static int
fifo_ebadf()
{

	return (EBADF);
}

/*
 * Fifo advisory byte-level locks.
 */
/* ARGSUSED */
int
fifo_advlock(ap)
	struct vop_advlock_args /* {
		struct vnode *a_vp;
		caddr_t  a_id;
		int  a_op;
		struct flock *a_fl;
		int  a_flags;
	} */ *ap;
{

	return (ap->a_flags & F_FLOCK ? EOPNOTSUPP : EINVAL);
}

/*
 * Fifo bad operation
 */
int
fifo_badop()
{

	panic("fifo_badop called");
	/* NOTREACHED */
}
