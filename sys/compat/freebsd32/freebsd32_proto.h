/*
 * System call prototypes.
 *
 * DO NOT EDIT-- this file is automatically generated.
 * $FreeBSD$
 * created from FreeBSD: src/sys/compat/freebsd32/syscalls.master,v 1.44 2005/01/04 18:53:32 jhb Exp 
 */

#ifndef _FREEBSD32_SYSPROTO_H_
#define	_FREEBSD32_SYSPROTO_H_

#include <sys/signal.h>
#include <sys/acl.h>
#include <sys/thr.h>
#include <sys/umtx.h>
#include <posix4/_semaphore.h>

#include <sys/ucontext.h>

struct proc;

struct thread;

#define	PAD_(t)	(sizeof(register_t) <= sizeof(t) ? \
		0 : sizeof(register_t) - sizeof(t))

#if BYTE_ORDER == LITTLE_ENDIAN
#define	PADL_(t)	0
#define	PADR_(t)	PAD_(t)
#else
#define	PADL_(t)	PAD_(t)
#define	PADR_(t)	0
#endif

struct freebsd32_wait4_args {
	char pid_l_[PADL_(int)]; int pid; char pid_r_[PADR_(int)];
	char status_l_[PADL_(int *)]; int * status; char status_r_[PADR_(int *)];
	char options_l_[PADL_(int)]; int options; char options_r_[PADR_(int)];
	char rusage_l_[PADL_(struct rusage32 *)]; struct rusage32 * rusage; char rusage_r_[PADR_(struct rusage32 *)];
};
struct freebsd32_sigaltstack_args {
	char ss_l_[PADL_(struct sigaltstack32 *)]; struct sigaltstack32 * ss; char ss_r_[PADR_(struct sigaltstack32 *)];
	char oss_l_[PADL_(struct sigaltstack32 *)]; struct sigaltstack32 * oss; char oss_r_[PADR_(struct sigaltstack32 *)];
};
struct freebsd32_execve_args {
	char fname_l_[PADL_(char *)]; char * fname; char fname_r_[PADR_(char *)];
	char argv_l_[PADL_(u_int32_t *)]; u_int32_t * argv; char argv_r_[PADR_(u_int32_t *)];
	char envv_l_[PADL_(u_int32_t *)]; u_int32_t * envv; char envv_r_[PADR_(u_int32_t *)];
};
struct freebsd32_setitimer_args {
	char which_l_[PADL_(u_int)]; u_int which; char which_r_[PADR_(u_int)];
	char itv_l_[PADL_(struct itimerval32 *)]; struct itimerval32 * itv; char itv_r_[PADR_(struct itimerval32 *)];
	char oitv_l_[PADL_(struct itimerval32 *)]; struct itimerval32 * oitv; char oitv_r_[PADR_(struct itimerval32 *)];
};
struct freebsd32_getitimer_args {
	char which_l_[PADL_(u_int)]; u_int which; char which_r_[PADR_(u_int)];
	char itv_l_[PADL_(struct itimerval32 *)]; struct itimerval32 * itv; char itv_r_[PADR_(struct itimerval32 *)];
};
struct freebsd32_select_args {
	char nd_l_[PADL_(int)]; int nd; char nd_r_[PADR_(int)];
	char in_l_[PADL_(fd_set *)]; fd_set * in; char in_r_[PADR_(fd_set *)];
	char ou_l_[PADL_(fd_set *)]; fd_set * ou; char ou_r_[PADR_(fd_set *)];
	char ex_l_[PADL_(fd_set *)]; fd_set * ex; char ex_r_[PADR_(fd_set *)];
	char tv_l_[PADL_(struct timeval32 *)]; struct timeval32 * tv; char tv_r_[PADR_(struct timeval32 *)];
};
struct freebsd32_gettimeofday_args {
	char tp_l_[PADL_(struct timeval32 *)]; struct timeval32 * tp; char tp_r_[PADR_(struct timeval32 *)];
	char tzp_l_[PADL_(struct timezone *)]; struct timezone * tzp; char tzp_r_[PADR_(struct timezone *)];
};
struct freebsd32_getrusage_args {
	char who_l_[PADL_(int)]; int who; char who_r_[PADR_(int)];
	char rusage_l_[PADL_(struct rusage32 *)]; struct rusage32 * rusage; char rusage_r_[PADR_(struct rusage32 *)];
};
struct freebsd32_readv_args {
	char fd_l_[PADL_(int)]; int fd; char fd_r_[PADR_(int)];
	char iovp_l_[PADL_(struct iovec32 *)]; struct iovec32 * iovp; char iovp_r_[PADR_(struct iovec32 *)];
	char iovcnt_l_[PADL_(u_int)]; u_int iovcnt; char iovcnt_r_[PADR_(u_int)];
};
struct freebsd32_writev_args {
	char fd_l_[PADL_(int)]; int fd; char fd_r_[PADR_(int)];
	char iovp_l_[PADL_(struct iovec32 *)]; struct iovec32 * iovp; char iovp_r_[PADR_(struct iovec32 *)];
	char iovcnt_l_[PADL_(u_int)]; u_int iovcnt; char iovcnt_r_[PADR_(u_int)];
};
struct freebsd32_settimeofday_args {
	char tv_l_[PADL_(struct timeval32 *)]; struct timeval32 * tv; char tv_r_[PADR_(struct timeval32 *)];
	char tzp_l_[PADL_(struct timezone *)]; struct timezone * tzp; char tzp_r_[PADR_(struct timezone *)];
};
struct freebsd32_utimes_args {
	char path_l_[PADL_(char *)]; char * path; char path_r_[PADR_(char *)];
	char tptr_l_[PADL_(struct timeval32 *)]; struct timeval32 * tptr; char tptr_r_[PADR_(struct timeval32 *)];
};
struct freebsd32_adjtime_args {
	char delta_l_[PADL_(struct timeval32 *)]; struct timeval32 * delta; char delta_r_[PADR_(struct timeval32 *)];
	char olddelta_l_[PADL_(struct timeval32 *)]; struct timeval32 * olddelta; char olddelta_r_[PADR_(struct timeval32 *)];
};
struct freebsd32_semsys_args {
	char which_l_[PADL_(int)]; int which; char which_r_[PADR_(int)];
	char a2_l_[PADL_(int)]; int a2; char a2_r_[PADR_(int)];
	char a3_l_[PADL_(int)]; int a3; char a3_r_[PADR_(int)];
	char a4_l_[PADL_(int)]; int a4; char a4_r_[PADR_(int)];
	char a5_l_[PADL_(int)]; int a5; char a5_r_[PADR_(int)];
};
struct freebsd32_msgsys_args {
	char which_l_[PADL_(int)]; int which; char which_r_[PADR_(int)];
	char a2_l_[PADL_(int)]; int a2; char a2_r_[PADR_(int)];
	char a3_l_[PADL_(int)]; int a3; char a3_r_[PADR_(int)];
	char a4_l_[PADL_(int)]; int a4; char a4_r_[PADR_(int)];
	char a5_l_[PADL_(int)]; int a5; char a5_r_[PADR_(int)];
	char a6_l_[PADL_(int)]; int a6; char a6_r_[PADR_(int)];
};
struct freebsd32_shmsys_args {
	char which_l_[PADL_(int)]; int which; char which_r_[PADR_(int)];
	char a2_l_[PADL_(int)]; int a2; char a2_r_[PADR_(int)];
	char a3_l_[PADL_(int)]; int a3; char a3_r_[PADR_(int)];
	char a4_l_[PADL_(int)]; int a4; char a4_r_[PADR_(int)];
};
struct freebsd32_pread_args {
	char fd_l_[PADL_(int)]; int fd; char fd_r_[PADR_(int)];
	char buf_l_[PADL_(void *)]; void * buf; char buf_r_[PADR_(void *)];
	char nbyte_l_[PADL_(size_t)]; size_t nbyte; char nbyte_r_[PADR_(size_t)];
	char pad_l_[PADL_(int)]; int pad; char pad_r_[PADR_(int)];
	char offsetlo_l_[PADL_(u_int32_t)]; u_int32_t offsetlo; char offsetlo_r_[PADR_(u_int32_t)];
	char offsethi_l_[PADL_(u_int32_t)]; u_int32_t offsethi; char offsethi_r_[PADR_(u_int32_t)];
};
struct freebsd32_pwrite_args {
	char fd_l_[PADL_(int)]; int fd; char fd_r_[PADR_(int)];
	char buf_l_[PADL_(const void *)]; const void * buf; char buf_r_[PADR_(const void *)];
	char nbyte_l_[PADL_(size_t)]; size_t nbyte; char nbyte_r_[PADR_(size_t)];
	char pad_l_[PADL_(int)]; int pad; char pad_r_[PADR_(int)];
	char offsetlo_l_[PADL_(u_int32_t)]; u_int32_t offsetlo; char offsetlo_r_[PADR_(u_int32_t)];
	char offsethi_l_[PADL_(u_int32_t)]; u_int32_t offsethi; char offsethi_r_[PADR_(u_int32_t)];
};
struct freebsd32_stat_args {
	char path_l_[PADL_(char *)]; char * path; char path_r_[PADR_(char *)];
	char ub_l_[PADL_(struct stat32 *)]; struct stat32 * ub; char ub_r_[PADR_(struct stat32 *)];
};
struct freebsd32_fstat_args {
	char fd_l_[PADL_(int)]; int fd; char fd_r_[PADR_(int)];
	char ub_l_[PADL_(struct stat32 *)]; struct stat32 * ub; char ub_r_[PADR_(struct stat32 *)];
};
struct freebsd32_lstat_args {
	char path_l_[PADL_(char *)]; char * path; char path_r_[PADR_(char *)];
	char ub_l_[PADL_(struct stat32 *)]; struct stat32 * ub; char ub_r_[PADR_(struct stat32 *)];
};
struct freebsd32_mmap_args {
	char addr_l_[PADL_(caddr_t)]; caddr_t addr; char addr_r_[PADR_(caddr_t)];
	char len_l_[PADL_(size_t)]; size_t len; char len_r_[PADR_(size_t)];
	char prot_l_[PADL_(int)]; int prot; char prot_r_[PADR_(int)];
	char flags_l_[PADL_(int)]; int flags; char flags_r_[PADR_(int)];
	char fd_l_[PADL_(int)]; int fd; char fd_r_[PADR_(int)];
	char pad_l_[PADL_(int)]; int pad; char pad_r_[PADR_(int)];
	char poslo_l_[PADL_(u_int32_t)]; u_int32_t poslo; char poslo_r_[PADR_(u_int32_t)];
	char poshi_l_[PADL_(u_int32_t)]; u_int32_t poshi; char poshi_r_[PADR_(u_int32_t)];
};
struct freebsd32_lseek_args {
	char fd_l_[PADL_(int)]; int fd; char fd_r_[PADR_(int)];
	char pad_l_[PADL_(int)]; int pad; char pad_r_[PADR_(int)];
	char offsetlo_l_[PADL_(u_int32_t)]; u_int32_t offsetlo; char offsetlo_r_[PADR_(u_int32_t)];
	char offsethi_l_[PADL_(u_int32_t)]; u_int32_t offsethi; char offsethi_r_[PADR_(u_int32_t)];
	char whence_l_[PADL_(int)]; int whence; char whence_r_[PADR_(int)];
};
struct freebsd32_truncate_args {
	char path_l_[PADL_(char *)]; char * path; char path_r_[PADR_(char *)];
	char pad_l_[PADL_(int)]; int pad; char pad_r_[PADR_(int)];
	char lengthlo_l_[PADL_(u_int32_t)]; u_int32_t lengthlo; char lengthlo_r_[PADR_(u_int32_t)];
	char lengthhi_l_[PADL_(u_int32_t)]; u_int32_t lengthhi; char lengthhi_r_[PADR_(u_int32_t)];
};
struct freebsd32_ftruncate_args {
	char fd_l_[PADL_(int)]; int fd; char fd_r_[PADR_(int)];
	char pad_l_[PADL_(int)]; int pad; char pad_r_[PADR_(int)];
	char lengthlo_l_[PADL_(u_int32_t)]; u_int32_t lengthlo; char lengthlo_r_[PADR_(u_int32_t)];
	char lengthhi_l_[PADL_(u_int32_t)]; u_int32_t lengthhi; char lengthhi_r_[PADR_(u_int32_t)];
};
struct freebsd32_sysctl_args {
	char name_l_[PADL_(int *)]; int * name; char name_r_[PADR_(int *)];
	char namelen_l_[PADL_(u_int)]; u_int namelen; char namelen_r_[PADR_(u_int)];
	char old_l_[PADL_(void *)]; void * old; char old_r_[PADR_(void *)];
	char oldlenp_l_[PADL_(u_int32_t *)]; u_int32_t * oldlenp; char oldlenp_r_[PADR_(u_int32_t *)];
	char new_l_[PADL_(void *)]; void * new; char new_r_[PADR_(void *)];
	char newlen_l_[PADL_(u_int32_t)]; u_int32_t newlen; char newlen_r_[PADR_(u_int32_t)];
};
struct freebsd32_nanosleep_args {
	char rqtp_l_[PADL_(const struct timespec *)]; const struct timespec * rqtp; char rqtp_r_[PADR_(const struct timespec *)];
	char rmtp_l_[PADL_(struct timespec *)]; struct timespec * rmtp; char rmtp_r_[PADR_(struct timespec *)];
};
struct freebsd32_kevent_args {
	char fd_l_[PADL_(int)]; int fd; char fd_r_[PADR_(int)];
	char changelist_l_[PADL_(const struct kevent *)]; const struct kevent * changelist; char changelist_r_[PADR_(const struct kevent *)];
	char nchanges_l_[PADL_(int)]; int nchanges; char nchanges_r_[PADR_(int)];
	char eventlist_l_[PADL_(struct kevent *)]; struct kevent * eventlist; char eventlist_r_[PADR_(struct kevent *)];
	char nevents_l_[PADL_(int)]; int nevents; char nevents_r_[PADR_(int)];
	char timeout_l_[PADL_(const struct timespec *)]; const struct timespec * timeout; char timeout_r_[PADR_(const struct timespec *)];
};
struct freebsd32_sendfile_args {
	char fd_l_[PADL_(int)]; int fd; char fd_r_[PADR_(int)];
	char s_l_[PADL_(int)]; int s; char s_r_[PADR_(int)];
	char offsetlo_l_[PADL_(u_int32_t)]; u_int32_t offsetlo; char offsetlo_r_[PADR_(u_int32_t)];
	char offsethi_l_[PADL_(u_int32_t)]; u_int32_t offsethi; char offsethi_r_[PADR_(u_int32_t)];
	char nbytes_l_[PADL_(size_t)]; size_t nbytes; char nbytes_r_[PADR_(size_t)];
	char hdtr_l_[PADL_(struct sf_hdtr *)]; struct sf_hdtr * hdtr; char hdtr_r_[PADR_(struct sf_hdtr *)];
	char sbytes_l_[PADL_(off_t *)]; off_t * sbytes; char sbytes_r_[PADR_(off_t *)];
	char flags_l_[PADL_(int)]; int flags; char flags_r_[PADR_(int)];
};
struct freebsd32_sigaction_args {
	char sig_l_[PADL_(int)]; int sig; char sig_r_[PADR_(int)];
	char act_l_[PADL_(struct sigaction32 *)]; struct sigaction32 * act; char act_r_[PADR_(struct sigaction32 *)];
	char oact_l_[PADL_(struct sigaction32 *)]; struct sigaction32 * oact; char oact_r_[PADR_(struct sigaction32 *)];
};
struct freebsd32_sigreturn_args {
	char sigcntxp_l_[PADL_(const struct freebsd32_ucontext *)]; const struct freebsd32_ucontext * sigcntxp; char sigcntxp_r_[PADR_(const struct freebsd32_ucontext *)];
};
int	freebsd32_wait4(struct thread *, struct freebsd32_wait4_args *);
int	freebsd32_sigaltstack(struct thread *, struct freebsd32_sigaltstack_args *);
int	freebsd32_execve(struct thread *, struct freebsd32_execve_args *);
int	freebsd32_setitimer(struct thread *, struct freebsd32_setitimer_args *);
int	freebsd32_getitimer(struct thread *, struct freebsd32_getitimer_args *);
int	freebsd32_select(struct thread *, struct freebsd32_select_args *);
int	freebsd32_gettimeofday(struct thread *, struct freebsd32_gettimeofday_args *);
int	freebsd32_getrusage(struct thread *, struct freebsd32_getrusage_args *);
int	freebsd32_readv(struct thread *, struct freebsd32_readv_args *);
int	freebsd32_writev(struct thread *, struct freebsd32_writev_args *);
int	freebsd32_settimeofday(struct thread *, struct freebsd32_settimeofday_args *);
int	freebsd32_utimes(struct thread *, struct freebsd32_utimes_args *);
int	freebsd32_adjtime(struct thread *, struct freebsd32_adjtime_args *);
int	freebsd32_semsys(struct thread *, struct freebsd32_semsys_args *);
int	freebsd32_msgsys(struct thread *, struct freebsd32_msgsys_args *);
int	freebsd32_shmsys(struct thread *, struct freebsd32_shmsys_args *);
int	freebsd32_pread(struct thread *, struct freebsd32_pread_args *);
int	freebsd32_pwrite(struct thread *, struct freebsd32_pwrite_args *);
int	freebsd32_stat(struct thread *, struct freebsd32_stat_args *);
int	freebsd32_fstat(struct thread *, struct freebsd32_fstat_args *);
int	freebsd32_lstat(struct thread *, struct freebsd32_lstat_args *);
int	freebsd32_mmap(struct thread *, struct freebsd32_mmap_args *);
int	freebsd32_lseek(struct thread *, struct freebsd32_lseek_args *);
int	freebsd32_truncate(struct thread *, struct freebsd32_truncate_args *);
int	freebsd32_ftruncate(struct thread *, struct freebsd32_ftruncate_args *);
int	freebsd32_sysctl(struct thread *, struct freebsd32_sysctl_args *);
int	freebsd32_nanosleep(struct thread *, struct freebsd32_nanosleep_args *);
int	freebsd32_kevent(struct thread *, struct freebsd32_kevent_args *);
int	freebsd32_sendfile(struct thread *, struct freebsd32_sendfile_args *);
int	freebsd32_sigaction(struct thread *, struct freebsd32_sigaction_args *);
int	freebsd32_sigreturn(struct thread *, struct freebsd32_sigreturn_args *);

#ifdef COMPAT_43


#endif /* COMPAT_43 */


#ifdef COMPAT_FREEBSD4

struct freebsd4_freebsd32_getfsstat_args {
	char buf_l_[PADL_(struct statfs32 *)]; struct statfs32 * buf; char buf_r_[PADR_(struct statfs32 *)];
	char bufsize_l_[PADL_(long)]; long bufsize; char bufsize_r_[PADR_(long)];
	char flags_l_[PADL_(int)]; int flags; char flags_r_[PADR_(int)];
};
struct freebsd4_freebsd32_statfs_args {
	char path_l_[PADL_(char *)]; char * path; char path_r_[PADR_(char *)];
	char buf_l_[PADL_(struct statfs32 *)]; struct statfs32 * buf; char buf_r_[PADR_(struct statfs32 *)];
};
struct freebsd4_freebsd32_fstatfs_args {
	char fd_l_[PADL_(int)]; int fd; char fd_r_[PADR_(int)];
	char buf_l_[PADL_(struct statfs32 *)]; struct statfs32 * buf; char buf_r_[PADR_(struct statfs32 *)];
};
struct freebsd4_freebsd32_fhstatfs_args {
	char u_fhp_l_[PADL_(const struct fhandle *)]; const struct fhandle * u_fhp; char u_fhp_r_[PADR_(const struct fhandle *)];
	char buf_l_[PADL_(struct statfs32 *)]; struct statfs32 * buf; char buf_r_[PADR_(struct statfs32 *)];
};
struct freebsd4_freebsd32_sendfile_args {
	char fd_l_[PADL_(int)]; int fd; char fd_r_[PADR_(int)];
	char s_l_[PADL_(int)]; int s; char s_r_[PADR_(int)];
	char offsetlo_l_[PADL_(u_int32_t)]; u_int32_t offsetlo; char offsetlo_r_[PADR_(u_int32_t)];
	char offsethi_l_[PADL_(u_int32_t)]; u_int32_t offsethi; char offsethi_r_[PADR_(u_int32_t)];
	char nbytes_l_[PADL_(size_t)]; size_t nbytes; char nbytes_r_[PADR_(size_t)];
	char hdtr_l_[PADL_(struct sf_hdtr *)]; struct sf_hdtr * hdtr; char hdtr_r_[PADR_(struct sf_hdtr *)];
	char sbytes_l_[PADL_(off_t *)]; off_t * sbytes; char sbytes_r_[PADR_(off_t *)];
	char flags_l_[PADL_(int)]; int flags; char flags_r_[PADR_(int)];
};
struct freebsd4_freebsd32_sigaction_args {
	char sig_l_[PADL_(int)]; int sig; char sig_r_[PADR_(int)];
	char act_l_[PADL_(struct sigaction32 *)]; struct sigaction32 * act; char act_r_[PADR_(struct sigaction32 *)];
	char oact_l_[PADL_(struct sigaction32 *)]; struct sigaction32 * oact; char oact_r_[PADR_(struct sigaction32 *)];
};
struct freebsd4_freebsd32_sigreturn_args {
	char sigcntxp_l_[PADL_(const struct freebsd4_freebsd32_ucontext *)]; const struct freebsd4_freebsd32_ucontext * sigcntxp; char sigcntxp_r_[PADR_(const struct freebsd4_freebsd32_ucontext *)];
};
int	freebsd4_freebsd32_getfsstat(struct thread *, struct freebsd4_freebsd32_getfsstat_args *);
int	freebsd4_freebsd32_statfs(struct thread *, struct freebsd4_freebsd32_statfs_args *);
int	freebsd4_freebsd32_fstatfs(struct thread *, struct freebsd4_freebsd32_fstatfs_args *);
int	freebsd4_freebsd32_fhstatfs(struct thread *, struct freebsd4_freebsd32_fhstatfs_args *);
int	freebsd4_freebsd32_sendfile(struct thread *, struct freebsd4_freebsd32_sendfile_args *);
int	freebsd4_freebsd32_sigaction(struct thread *, struct freebsd4_freebsd32_sigaction_args *);
int	freebsd4_freebsd32_sigreturn(struct thread *, struct freebsd4_freebsd32_sigreturn_args *);

#endif /* COMPAT_FREEBSD4 */

#undef PAD_
#undef PADL_
#undef PADR_

#endif /* !_FREEBSD32_SYSPROTO_H_ */
