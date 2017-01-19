/*	$NetBSD: tmpfs.h,v 1.26 2007/02/22 06:37:00 thorpej Exp $	*/

/*-
 * Copyright (c) 2005, 2006 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Julio M. Merino Vidal, developed as part of Google's Summer of Code
 * 2005 program.
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
 *
 * $FreeBSD$
 */

#ifndef _FS_TMPFS_TMPFS_H_
#define _FS_TMPFS_TMPFS_H_

#include <sys/dirent.h>
#include <sys/mount.h>
#include <sys/queue.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/tree.h>
#include <sys/vmmeter.h>
#include <vm/swap_pager.h>

MALLOC_DECLARE(M_TMPFSMNT);
MALLOC_DECLARE(M_TMPFSNAME);

/*
 * Internal representation of a tmpfs directory entry.
 */

LIST_HEAD(tmpfs_dir_duphead, tmpfs_dirent);

struct tmpfs_dirent {
	/*
	 * Depending on td_cookie flag entry can be of 3 types:
	 * - regular -- no hash collisions, stored in RB-Tree
	 * - duphead -- synthetic linked list head for dup entries
	 * - dup -- stored in linked list instead of RB-Tree
	 */
	union {
		/* regular and duphead entry types */
		RB_ENTRY(tmpfs_dirent)		td_entries;

		/* dup entry type */
		struct {
			LIST_ENTRY(tmpfs_dirent) entries;
			LIST_ENTRY(tmpfs_dirent) index_entries;
		} td_dup;
	} uh;

	uint32_t			td_cookie;
	uint32_t			td_hash;
	u_int				td_namelen;

	/*
	 * Pointer to the node this entry refers to.  In case this field
	 * is NULL, the node is a whiteout.
	 */
	struct tmpfs_node *		td_node;

	union {
		/*
		 * The name of the entry, allocated from a string pool.  This
		 * string is not required to be zero-terminated.
		 */
		char *			td_name;	/* regular, dup */
		struct tmpfs_dir_duphead td_duphead;	/* duphead */
	} ud;
};

/*
 * A directory in tmpfs holds a collection of directory entries, which
 * in turn point to other files (which can be directories themselves).
 *
 * In tmpfs, this collection is managed by a RB-Tree, whose head is
 * defined by the struct tmpfs_dir type.
 *
 * It is important to notice that directories do not have entries for . and
 * .. as other file systems do.  These can be generated when requested
 * based on information available by other means, such as the pointer to
 * the node itself in the former case or the pointer to the parent directory
 * in the latter case.  This is done to simplify tmpfs's code and, more
 * importantly, to remove redundancy.
 */
RB_HEAD(tmpfs_dir, tmpfs_dirent);

/*
 * Each entry in a directory has a cookie that identifies it.  Cookies
 * supersede offsets within directories because, given how tmpfs stores
 * directories in memory, there is no such thing as an offset.
 *
 * The '.', '..' and the end of directory markers have fixed cookies which
 * cannot collide with the cookies generated by other entries.  The cookies
 * for the other entries are generated based on the file name hash value or
 * unique number in case of name hash collision.
 *
 * To preserve compatibility cookies are limited to 31 bits.
 */

#define	TMPFS_DIRCOOKIE_DOT		0
#define	TMPFS_DIRCOOKIE_DOTDOT		1
#define	TMPFS_DIRCOOKIE_EOF		2
#define	TMPFS_DIRCOOKIE_MASK		((off_t)0x3fffffffU)
#define	TMPFS_DIRCOOKIE_MIN		((off_t)0x00000004U)
#define	TMPFS_DIRCOOKIE_DUP		((off_t)0x40000000U)
#define	TMPFS_DIRCOOKIE_DUPHEAD		((off_t)0x80000000U)
#define	TMPFS_DIRCOOKIE_DUP_MIN		TMPFS_DIRCOOKIE_DUP
#define	TMPFS_DIRCOOKIE_DUP_MAX		\
	(TMPFS_DIRCOOKIE_DUP | TMPFS_DIRCOOKIE_MASK)

/*
 * Internal representation of a tmpfs file system node.
 *
 * This structure is splitted in two parts: one holds attributes common
 * to all file types and the other holds data that is only applicable to
 * a particular type.  The code must be careful to only access those
 * attributes that are actually allowed by the node's type.
 *
 * Below is the key of locks used to protected the fields in the following
 * structures.
 * (v)  vnode lock in exclusive mode
 * (vi) vnode lock in exclusive mode, or vnode lock in shared vnode and
 *	tn_interlock
 * (i)  tn_interlock
 * (m)  tmpfs_mount tm_allnode_lock
 * (c)  stable after creation
 */
struct tmpfs_node {
	/*
	 * Doubly-linked list entry which links all existing nodes for
	 * a single file system.  This is provided to ease the removal
	 * of all nodes during the unmount operation, and to support
	 * the implementation of VOP_VNTOCNP().
	 */
	LIST_ENTRY(tmpfs_node)	tn_entries;	/* (m) */

	/*
	 * The node's type.  Any of 'VBLK', 'VCHR', 'VDIR', 'VFIFO',
	 * 'VLNK', 'VREG' and 'VSOCK' is allowed.  The usage of vnode
	 * types instead of a custom enumeration is to make things simpler
	 * and faster, as we do not need to convert between two types.
	 */
	enum vtype		tn_type;	/* (c) */

	/* Node identifier. */
	ino_t			tn_id;		/* (c) */

	/*
	 * Node's internal status.  This is used by several file system
	 * operations to do modifications to the node in a delayed
	 * fashion.
	 */
	int			tn_status;	/* (vi) */
#define	TMPFS_NODE_ACCESSED	(1 << 1)
#define	TMPFS_NODE_MODIFIED	(1 << 2)
#define	TMPFS_NODE_CHANGED	(1 << 3)

	/*
	 * The node size.  It does not necessarily match the real amount
	 * of memory consumed by it.
	 */
	off_t			tn_size;	/* (v) */

	/* Generic node attributes. */
	uid_t			tn_uid;		/* (v) */
	gid_t			tn_gid;		/* (v) */
	mode_t			tn_mode;	/* (v) */
	u_long			tn_flags;	/* (v) */
	nlink_t			tn_links;	/* (v) */
	struct timespec		tn_atime;	/* (vi) */
	struct timespec		tn_mtime;	/* (vi) */
	struct timespec		tn_ctime;	/* (vi) */
	struct timespec		tn_birthtime;	/* (v) */
	unsigned long		tn_gen;		/* (c) */

	/*
	 * As there is a single vnode for each active file within the
	 * system, care has to be taken to avoid allocating more than one
	 * vnode per file.  In order to do this, a bidirectional association
	 * is kept between vnodes and nodes.
	 *
	 * Whenever a vnode is allocated, its v_data field is updated to
	 * point to the node it references.  At the same time, the node's
	 * tn_vnode field is modified to point to the new vnode representing
	 * it.  Further attempts to allocate a vnode for this same node will
	 * result in returning a new reference to the value stored in
	 * tn_vnode.
	 *
	 * May be NULL when the node is unused (that is, no vnode has been
	 * allocated for it or it has been reclaimed).
	 */
	struct vnode *		tn_vnode;	/* (i) */

	/*
	 * Interlock to protect tn_vpstate, and tn_status under shared
	 * vnode lock.
	 */
	struct mtx	tn_interlock;

	/*
	 * Identify if current node has vnode assiocate with
	 * or allocating vnode.
	 */
	int		tn_vpstate;		/* (i) */

	/* misc data field for different tn_type node */
	union {
		/* Valid when tn_type == VBLK || tn_type == VCHR. */
		dev_t			tn_rdev;	/* (c) */

		/* Valid when tn_type == VDIR. */
		struct tn_dir {
			/*
			 * Pointer to the parent directory.  The root
			 * directory has a pointer to itself in this field;
			 * this property identifies the root node.
			 */
			struct tmpfs_node *	tn_parent;

			/*
			 * Head of a tree that links the contents of
			 * the directory together.
			 */
			struct tmpfs_dir	tn_dirhead;

			/*
			 * Head of a list the contains fake directory entries
			 * heads, i.e. entries with TMPFS_DIRCOOKIE_DUPHEAD
			 * flag.
			 */
			struct tmpfs_dir_duphead tn_dupindex;

			/*
			 * Number and pointer of the first directory entry
			 * returned by the readdir operation if it were
			 * called again to continue reading data from the
			 * same directory as before.  This is used to speed
			 * up reads of long directories, assuming that no
			 * more than one read is in progress at a given time.
			 * Otherwise, these values are discarded.
			 */
			off_t			tn_readdir_lastn;
			struct tmpfs_dirent *	tn_readdir_lastp;
		} tn_dir;

		/* Valid when tn_type == VLNK. */
		/* The link's target, allocated from a string pool. */
		char *			tn_link;	/* (c) */

		/* Valid when tn_type == VREG. */
		struct tn_reg {
			/*
			 * The contents of regular files stored in a
			 * tmpfs file system are represented by a
			 * single anonymous memory object (aobj, for
			 * short).  The aobj provides direct access to
			 * any position within the file.  It is a task
			 * of the memory management subsystem to issue
			 * the required page ins or page outs whenever
			 * a position within the file is accessed.
			 */
			vm_object_t		tn_aobj;	/* (c) */
		} tn_reg;
	} tn_spec;	/* (v) */
};
LIST_HEAD(tmpfs_node_list, tmpfs_node);

#define tn_rdev tn_spec.tn_rdev
#define tn_dir tn_spec.tn_dir
#define tn_link tn_spec.tn_link
#define tn_reg tn_spec.tn_reg
#define tn_fifo tn_spec.tn_fifo

#define TMPFS_NODE_LOCK(node) mtx_lock(&(node)->tn_interlock)
#define TMPFS_NODE_UNLOCK(node) mtx_unlock(&(node)->tn_interlock)
#define TMPFS_NODE_MTX(node) (&(node)->tn_interlock)
#define	TMPFS_NODE_ASSERT_LOCKED(node) mtx_assert(TMPFS_NODE_MTX(node), \
    MA_OWNED)

#ifdef INVARIANTS
#define TMPFS_ASSERT_LOCKED(node) do {					\
		MPASS((node) != NULL);					\
		MPASS((node)->tn_vnode != NULL);			\
		ASSERT_VOP_LOCKED((node)->tn_vnode, "tmpfs assert");	\
	} while (0)
#else
#define TMPFS_ASSERT_LOCKED(node) (void)0
#endif

#define TMPFS_VNODE_ALLOCATING	1
#define TMPFS_VNODE_WANT	2
#define TMPFS_VNODE_DOOMED	4
#define	TMPFS_VNODE_WRECLAIM	8

/*
 * Internal representation of a tmpfs mount point.
 */
struct tmpfs_mount {
	/*
	 * Maximum number of memory pages available for use by the file
	 * system, set during mount time.  This variable must never be
	 * used directly as it may be bigger than the current amount of
	 * free memory; in the extreme case, it will hold the ULONG_MAX
	 * value.
	 */
	u_long			tm_pages_max;

	/* Number of pages in use by the file system. */
	u_long			tm_pages_used;

	/*
	 * Pointer to the node representing the root directory of this
	 * file system.
	 */
	struct tmpfs_node *	tm_root;

	struct mount *		tm_mnt;

	/*
	 * Maximum number of possible nodes for this file system; set
	 * during mount time.  We need a hard limit on the maximum number
	 * of nodes to avoid allocating too much of them; their objects
	 * cannot be released until the file system is unmounted.
	 * Otherwise, we could easily run out of memory by creating lots
	 * of empty files and then simply removing them.
	 */
	ino_t			tm_nodes_max;

	/* unrhdr used to allocate inode numbers */
	struct unrhdr *		tm_ino_unr;

	/* Number of nodes currently that are in use. */
	ino_t			tm_nodes_inuse;

	/* maximum representable file size */
	u_int64_t		tm_maxfilesize;

	/*
	 * The used list contains all nodes that are currently used by
	 * the file system; i.e., they refer to existing files.
	 */
	struct tmpfs_node_list	tm_nodes_used;

	/* All node lock to protect the node list and tmp_pages_used. */
	struct mtx		tm_allnode_lock;

	/* Zones used to store file system meta data, per tmpfs mount. */
	uma_zone_t		tm_dirent_pool;
	uma_zone_t		tm_node_pool;

	/* Read-only status. */
	int			tm_ronly;
};
#define	TMPFS_LOCK(tm) mtx_lock(&(tm)->tm_allnode_lock)
#define	TMPFS_UNLOCK(tm) mtx_unlock(&(tm)->tm_allnode_lock)
#define	TMPFS_MP_ASSERT_LOCKED(tm) mtx_assert(&(tm)->tm_allnode_lock, MA_OWNED)

/*
 * This structure maps a file identifier to a tmpfs node.  Used by the
 * NFS code.
 */
struct tmpfs_fid {
	uint16_t		tf_len;
	uint16_t		tf_pad;
	ino_t			tf_id;
	unsigned long		tf_gen;
};

#ifdef _KERNEL
/*
 * Prototypes for tmpfs_subr.c.
 */

int	tmpfs_alloc_node(struct mount *mp, struct tmpfs_mount *, enum vtype,
	    uid_t uid, gid_t gid, mode_t mode, struct tmpfs_node *,
	    char *, dev_t, struct tmpfs_node **);
void	tmpfs_free_node(struct tmpfs_mount *, struct tmpfs_node *);
int	tmpfs_alloc_dirent(struct tmpfs_mount *, struct tmpfs_node *,
	    const char *, u_int, struct tmpfs_dirent **);
void	tmpfs_free_dirent(struct tmpfs_mount *, struct tmpfs_dirent *);
void	tmpfs_dirent_init(struct tmpfs_dirent *, const char *, u_int);
void	tmpfs_destroy_vobject(struct vnode *vp, vm_object_t obj);
int	tmpfs_alloc_vp(struct mount *, struct tmpfs_node *, int,
	    struct vnode **);
void	tmpfs_free_vp(struct vnode *);
int	tmpfs_alloc_file(struct vnode *, struct vnode **, struct vattr *,
	    struct componentname *, char *);
void	tmpfs_check_mtime(struct vnode *);
void	tmpfs_dir_attach(struct vnode *, struct tmpfs_dirent *);
void	tmpfs_dir_detach(struct vnode *, struct tmpfs_dirent *);
void	tmpfs_dir_destroy(struct tmpfs_mount *, struct tmpfs_node *);
struct tmpfs_dirent *	tmpfs_dir_lookup(struct tmpfs_node *node,
			    struct tmpfs_node *f,
			    struct componentname *cnp);
int	tmpfs_dir_getdents(struct tmpfs_node *, struct uio *, int,
	    u_long *, int *);
int	tmpfs_dir_whiteout_add(struct vnode *, struct componentname *);
void	tmpfs_dir_whiteout_remove(struct vnode *, struct componentname *);
int	tmpfs_reg_resize(struct vnode *, off_t, boolean_t);
int	tmpfs_chflags(struct vnode *, u_long, struct ucred *, struct thread *);
int	tmpfs_chmod(struct vnode *, mode_t, struct ucred *, struct thread *);
int	tmpfs_chown(struct vnode *, uid_t, gid_t, struct ucred *,
	    struct thread *);
int	tmpfs_chsize(struct vnode *, u_quad_t, struct ucred *, struct thread *);
int	tmpfs_chtimes(struct vnode *, struct vattr *, struct ucred *cred,
	    struct thread *);
void	tmpfs_itimes(struct vnode *, const struct timespec *,
	    const struct timespec *);

void	tmpfs_set_status(struct tmpfs_node *node, int status);
void	tmpfs_update(struct vnode *);
int	tmpfs_truncate(struct vnode *, off_t);

/*
 * Convenience macros to simplify some logical expressions.
 */
#define IMPLIES(a, b) (!(a) || (b))
#define IFF(a, b) (IMPLIES(a, b) && IMPLIES(b, a))

/*
 * Checks that the directory entry pointed by 'de' matches the name 'name'
 * with a length of 'len'.
 */
#define TMPFS_DIRENT_MATCHES(de, name, len) \
    (de->td_namelen == len && \
    bcmp((de)->ud.td_name, (name), (de)->td_namelen) == 0)

/*
 * Ensures that the node pointed by 'node' is a directory and that its
 * contents are consistent with respect to directories.
 */
#define TMPFS_VALIDATE_DIR(node) do { \
	MPASS((node)->tn_type == VDIR); \
	MPASS((node)->tn_size % sizeof(struct tmpfs_dirent) == 0); \
} while (0)

/*
 * Amount of memory pages to reserve for the system (e.g., to not use by
 * tmpfs).
 */
#define TMPFS_PAGES_MINRESERVED		(4 * 1024 * 1024 / PAGE_SIZE)

size_t tmpfs_mem_avail(void);

size_t tmpfs_pages_used(struct tmpfs_mount *tmp);

#endif

/*
 * Macros/functions to convert from generic data structures to tmpfs
 * specific ones.
 */

static inline struct tmpfs_mount *
VFS_TO_TMPFS(struct mount *mp)
{
	struct tmpfs_mount *tmp;

	MPASS(mp != NULL && mp->mnt_data != NULL);
	tmp = (struct tmpfs_mount *)mp->mnt_data;
	return (tmp);
}

static inline struct tmpfs_node *
VP_TO_TMPFS_NODE(struct vnode *vp)
{
	struct tmpfs_node *node;

	MPASS(vp != NULL && vp->v_data != NULL);
	node = (struct tmpfs_node *)vp->v_data;
	return (node);
}

static inline struct tmpfs_node *
VP_TO_TMPFS_DIR(struct vnode *vp)
{
	struct tmpfs_node *node;

	node = VP_TO_TMPFS_NODE(vp);
	TMPFS_VALIDATE_DIR(node);
	return (node);
}

#endif /* _FS_TMPFS_TMPFS_H_ */
