/*
 * Copyright (c) 1995 John Birrell <jb@cimlogic.com.au>.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by John Birrell.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
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
 * $Id$
 *
 * POSIX stdio FILE locking functions. These assume that the locking
 * is only required at FILE structure level, not at file descriptor
 * level too.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#ifdef _THREAD_SAFE
#include <pthread.h>
#include "pthread_private.h"

/*
 * Weak symbols for externally visible functions in this file:
 */
#pragma	weak	flockfile=_flockfile
#pragma	weak	ftrylockfile=_ftrylockfile
#pragma	weak	funlockfile=_funlockfile

/*
 * The FILE lock structure. The FILE *fp is locked if the owner is
 * not NULL. If not locked, the file lock structure can be
 * reassigned to a different file by setting fp.
 */
struct	file_lock {
	LIST_ENTRY(file_lock)	entry;	/* Entry if file list.       */
	TAILQ_HEAD(lock_head, pthread)
				l_head;	/* Head of queue for threads */
					/* waiting on this lock.     */
	FILE		*fp;		/* The target file.          */
	pthread_t	owner;		/* Thread that owns lock.    */
	int		count;		/* Lock count for owner.     */
};

/*
 * The number of file lock lists into which the file pointer is
 * hashed. Ideally, the FILE structure size would have been increased,
 * but this causes incompatibility, so separate data structures are
 * required.
 */
#define NUM_HEADS	128

/*
 * This macro casts a file pointer to a long integer and right
 * shifts this by the number of bytes in a pointer. The shifted
 * value is then remaindered using the maximum number of hash
 * entries to produce and index into the array of static lock
 * structures. If there is a collision, a linear search of the
 * dynamic list of locks linked to each static lock is perfomed.
 */
#define file_idx(_p)	((((long) _p) >> sizeof(void *)) % NUM_HEADS)

/*
 * Global array of file locks. The first lock for each hash bucket is
 * allocated statically in the hope that there won't be too many
 * collisions that require a malloc and an element added to the list.
 */
struct static_file_lock {
	LIST_HEAD(file_list_head, file_lock) head;
	struct	file_lock	fl;
} flh[NUM_HEADS];

/* Set to non-zero when initialisation is complete: */
static	int	init_done	= 0;

/*
 * Find a lock structure for a FILE, return NULL if the file is
 * not locked:
 */
static
struct file_lock *
find_lock(int idx, FILE *fp)
{
	struct file_lock *p;

	/* Check if the file is locked using the static structure: */
	if (flh[idx].fl.fp == fp && flh[idx].fl.owner != NULL)
		/* Return a pointer to the static lock: */
		p = &flh[idx].fl;
	else {
		/* Point to the first dynamic lock: */
		p = flh[idx].head.lh_first;

		/*
		 * Loop through the dynamic locks looking for the
		 * target file:
		 */
		while (p != NULL && p->fp != fp && p->owner != NULL)
			/* Not this file, try the next: */
			p = p->entry.le_next;
	}
	return(p);
}

/*
 * Lock a file, assuming that there is no lock structure currently1
 * assigned to it.
 */
static
struct file_lock *
do_lock(int idx, FILE *fp)
{
	struct file_lock *p;

	/* Check if the static structure is not being used: */
	if (flh[idx].fl.owner == NULL) {
		/* Return a pointer to the static lock: */
		p = &flh[idx].fl;
	}
	else {
		/* Point to the first dynamic lock: */
		p = flh[idx].head.lh_first;

		/*
		 * Loop through the dynamic locks looking for a
		 * lock structure that is not being used:
		 */
		while (p != NULL && p->owner != NULL)
			/* This one is used, try the next: */
			p = p->entry.le_next;
	}

	/*
	 * If an existing lock structure has not been found,
	 * allocate memory for a new one:
	 */
	if (p == NULL && (p = (struct file_lock *)
	    malloc(sizeof(struct file_lock))) != NULL) {
		/* Add the new element to the list: */
		LIST_INSERT_HEAD(&flh[idx].head, p, entry);
	}

	/* Check if there is a lock structure to acquire: */
	if (p != NULL) {
		/* Acquire the lock for the running thread: */
		p->fp		= fp;
		p->owner	= _thread_run;
		p->count	= 1;
		TAILQ_INIT(&p->l_head);
	}
	return(p);
}

void
_flockfile_debug(FILE * fp, char *fname, int lineno)
{
	int	fd, flags;
	int	status;
	int	idx = file_idx(fp);
	struct	file_lock	*p;

	/* Check if this is a real file: */
	if (fp->_file >= 0) {
		/* Block signals: */
		_thread_kern_sig_block(&status);

		/* Check if the static array has not been initialised: */
		if (!init_done) {
			/* Initialise the global array: */
			memset(flh,0,sizeof(flh));

			/* Flag the initialisation as complete: */
			init_done = 1;
		}

		/* Get a pointer to any existing lock for the file: */
		if ((p = find_lock(idx, fp)) == NULL) {
			/*
			 * The file is not locked, so this thread can
			 * grab the lock:
			 */
			p = do_lock(idx, fp);

		/*
		 * The file is already locked, so check if the
		 * running thread is the owner:
		 */
		} else if (p->owner == _thread_run) {
			/*
			 * The running thread is already the
			 * owner, so increment the count of
			 * the number of times it has locked
			 * the file:
			 */
			p->count++;
		} else {
			/*
			 * The file is locked for another thread.
			 * Append this thread to the queue of
			 * threads waiting on the lock.
			 */
			TAILQ_INSERT_TAIL(&p->l_head,_thread_run,qe);

			/* Wait on the FILE lock: */
			_thread_kern_sched_state(PS_FILE_WAIT, fname, lineno);

			/* Block signals again: */
			_thread_kern_sig_block(NULL);
		}

		/* Unblock signals: */
		_thread_kern_sig_unblock(status);
	}
	return;
}

void
_flockfile(FILE * fp)
{
	_flockfile_debug(fp, __FILE__, __LINE__);
	return;
}

int
_ftrylockfile(FILE * fp)
{
	int	ret = -1;
	int	status;
	int	idx = file_idx(fp);
	struct	file_lock	*p;

	/* Check if this is a real file: */
	if (fp->_file >= 0) {
		/* Block signals: */
		_thread_kern_sig_block(&status);

		/* Get a pointer to any existing lock for the file: */
		if ((p = find_lock(idx, fp)) == NULL) {
			/*
			 * The file is not locked, so this thread can
			 * grab the lock:
			 */
			p = do_lock(idx, fp);

		/*
		 * The file is already locked, so check if the
		 * running thread is the owner:
		 */
		} else if (p->owner == _thread_run) {
			/*
			 * The running thread is already the
			 * owner, so increment the count of
			 * the number of times it has locked
			 * the file:
			 */
			p->count++;
		} else {
			/*
			 * The file is locked for another thread,
			 * so this try fails.
			 */
			p = NULL;
		}

		/* Check if the lock was obtained: */
		if (p != NULL)
			/* Return success: */
			ret = 0;

		/* Unblock signals: */
		_thread_kern_sig_unblock(status);
	}
	return (ret);
}

void 
_funlockfile(FILE * fp)
{
	int	status;
	int	idx = file_idx(fp);
	struct	file_lock	*p;

	/* Check if this is a real file: */
	if (fp->_file >= 0) {
		/* Block signals: */
		_thread_kern_sig_block(&status);

		/*
		 * Get a pointer to the lock for the file and check that
		 * the running thread is the one with the lock:
		 */
		if ((p = find_lock(idx, fp)) != NULL &&
		    p->owner == _thread_run) {
			/*
			 * Check if this thread has locked the FILE
			 * more than once:
			 */
			if (p->count > 1)
				/*
				 * Decrement the count of the number of
				 * times the running thread has locked this
				 * file:
				 */
				p->count--;
			else {
				/*
				 * The running thread will release the
				 * lock now:
				 */
				p->count = 0;

				/* Get the new owner of the lock: */
				if ((p->owner = TAILQ_FIRST(&p->l_head)) != NULL) {
					/* Pop the thread off the queue: */
					TAILQ_REMOVE(&p->l_head,p->owner,qe);

					/*
					 * This is the first lock for the new
					 * owner:
					 */
					p->count = 1;

					/* Allow the new owner to run: */
					PTHREAD_NEW_STATE(p->owner,PS_RUNNING);
				}
			}
		}

		/* Unblock signals: */
		_thread_kern_sig_unblock(status);
	}
	return;
}

#endif
