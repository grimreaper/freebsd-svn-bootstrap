/* THIS FILE PRODUCED AUTOMATICALLY */
void  devfs_sinit(void *junk) /*proto*/;
devnm_p dev_findname(dn_p dir,char *name) /*proto*/;
int	dev_finddir(char *orig_path, dn_p dirnode, int create, dn_p *dn_pp) /*proto*/;
int	dev_add_name(char *name, dn_p dirnode, devnm_p back, dn_p dnp, devnm_p *devnm_pp) /*proto*/;
int	dev_add_node(int entrytype, union typeinfo *by, dn_p proto, dn_p *dn_pp) /*proto*/;
int	dev_touch(devnm_p key)		/* update the node for this dev */ /*proto*/;
void	devfs_dn_free(dn_p dnp) /*proto*/;
int devfs_add_fronts(devnm_p parent,devnm_p child) /*proto*/;
void	dev_remove_dev(devnm_p devnmp) /*proto*/;
int dev_dup_plane(struct devfsmount *devfs_mp_p) /*proto*/;
void  devfs_free_plane(struct devfsmount *devfs_mp_p) /*proto*/;
int dev_dup_entry(dn_p parent, devnm_p back, devnm_p *dnm_pp, struct devfsmount *dvm) /*proto*/;
void dev_free_name(devnm_p devnmp) /*proto*/;
int devfs_vntodn(struct vnode *vn_p, dn_p *dn_pp) /*proto*/;
int devfs_dntovn(dn_p dnp, struct vnode **vn_pp) /*proto*/;
int get_cdev_major_num(caddr_t addr)	/*proto*/;
int get_bdev_major_num(caddr_t addr)	/*proto*/;
int dev_add_entry(char *name, dn_p parent, int type, union typeinfo *by, devnm_p *nm_pp) /*proto*/ ;
int devfs_init(void) /*proto*/;
int devfs_mount( struct mount *mp, char *path, caddr_t data, struct nameidata *ndp, struct proc *p) /*proto*/;
int mountdevfs( struct mount *mp, struct proc *p) /*proto*/;
int devfs_start(struct mount *mp, int flags, struct proc *p) /*proto*/;
int devfs_unmount( struct mount *mp, int mntflags, struct proc *p) /*proto*/;
int devfs_root(struct mount *mp, struct vnode **vpp) /*proto*/;
int devfs_quotactl( struct mount *mp, int cmds, uid_t uid, caddr_t arg, struct proc *p) /*proto*/;
int devfs_statfs( struct mount *mp, struct statfs *sbp, struct proc *p) /*proto*/;
int devfs_sync(struct mount *mp, int waitfor,struct ucred *cred,struct proc *p) /*proto*/;
int devfs_vget(struct mount *mp, ino_t ino,struct vnode **vpp) /*proto*/;
int devfs_fhtovp (struct mount *mp, struct fid *fhp, struct mbuf *nam, struct vnode **vpp, int *exflagsp, struct ucred **credanonp) /*proto*/;
int devfs_vptofh (struct vnode *vp, struct fid *fhp) /*proto*/;
int devfs_lookup(struct vop_lookup_args *ap) /*proto*/;
int devfs_create(struct vop_mknod_args  *ap) /*proto*/;
int devfs_mknod( struct vop_mknod_args *ap) /*proto*/;
int devfs_open(struct vop_open_args *ap) /*proto*/;
int devfs_close( struct vop_close_args *ap) /*proto*/;
int devfs_access(struct vop_access_args *ap) /*proto*/;
int devfs_getattr(struct vop_getattr_args *ap) /*proto*/;
int devfs_setattr(struct vop_setattr_args *ap) /*proto*/;
int devfs_read(struct vop_read_args *ap) /*proto*/;
int devfs_write(struct vop_write_args *ap) /*proto*/;
int devfs_ioctl(struct vop_ioctl_args *ap) /*proto*/;
int devfs_select(struct vop_select_args *ap) /*proto*/;
int devfs_mmap(struct vop_mmap_args *ap) /*proto*/;
int devfs_fsync(struct vop_fsync_args *ap) /*proto*/;
int devfs_seek(struct vop_seek_args *ap) /*proto*/;
int devfs_remove(struct vop_remove_args *ap) /*proto*/;
int devfs_link(struct vop_link_args *ap) /*proto*/;
int devfs_rename(struct vop_rename_args *ap) /*proto*/;
int devfs_mkdir(struct vop_mkdir_args *ap) /*proto*/;
int devfs_rmdir(struct vop_rmdir_args *ap) /*proto*/;
int devfs_symlink(struct vop_symlink_args *ap) /*proto*/;
int devfs_readdir(struct vop_readdir_args *ap) /*proto*/;
int devfs_readlink(struct vop_readlink_args *ap) /*proto*/;
int devfs_abortop(struct vop_abortop_args *ap) /*proto*/;
int devfs_inactive(struct vop_inactive_args *ap) /*proto*/;
int devfs_lock(struct vop_lock_args *ap) /*proto*/;
int devfs_unlock( struct vop_unlock_args *ap) /*proto*/;
int devfs_islocked(struct vop_islocked_args *ap) /*proto*/;
int devfs_bmap(struct vop_bmap_args *ap) /*proto*/;
int devfs_strategy(struct vop_strategy_args *ap) /*proto*/;
int devfs_advlock(struct vop_advlock_args *ap) /*proto*/;
int	devfs_reclaim(struct vop_reclaim_args *ap) /*proto*/;
int devfs_pathconf(struct vop_pathconf_args *ap) /*proto*/;
int devfs_print(struct vop_print_args *ap) /*proto*/;
int devfs_vfree(struct vop_vfree_args *ap) /*proto*/;
int devfs_enotsupp(void *junk) /*proto*/;
int devfs_badop(void *junk) /*proto*/;
int devfs_nullop(void *junk) /*proto*/;
void	devfs_dropvnode(dn_p dnp) /*proto*/;
/* THIS FILE PRODUCED AUTOMATICALLY */
/* DO NOT EDIT (see reproto.sh) */
