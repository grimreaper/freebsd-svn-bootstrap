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
#include <errno.h>
#include <pthread.h>
#include "pthread_private.h"

#pragma weak	pthread_resume_np=_pthread_resume_np

/* Resume a thread: */
int
_pthread_resume_np(pthread_t thread)
{
	int	ret;
	enum	pthread_susp old_suspended;

	/* Find the thread in the list of active threads: */
	if ((ret = _find_thread(thread)) == 0) {
		/* Cancel any pending suspensions: */
		old_suspended = thread->suspended;
		thread->suspended = SUSP_NO;

		/* Is it currently suspended? */
		if (thread->state == PS_SUSPENDED) {
			/*
			 * Defer signals to protect the scheduling queues
			 * from access by the signal handler:
			 */
			_thread_kern_sig_defer();

			switch (old_suspended) {
			case SUSP_MUTEX_WAIT:
				/* Set the thread's state back. */
				PTHREAD_SET_STATE(thread,PS_MUTEX_WAIT);
				break;
			case SUSP_COND_WAIT:
				/* Set the thread's state back. */
				PTHREAD_SET_STATE(thread,PS_COND_WAIT);
				break;
			case SUSP_NOWAIT:
				/* Allow the thread to run. */
				PTHREAD_SET_STATE(thread,PS_RUNNING);
				PTHREAD_WAITQ_REMOVE(thread);
				PTHREAD_PRIOQ_INSERT_TAIL(thread);
				break;
			case SUSP_NO:
			case SUSP_YES:
				/* Allow the thread to run. */
				PTHREAD_SET_STATE(thread,PS_RUNNING);
				PTHREAD_PRIOQ_INSERT_TAIL(thread);
				break;
			}

			/*
			 * Undefer and handle pending signals, yielding if
			 * necessary:
			 */
			_thread_kern_sig_undefer();
		}
	}
	return(ret);
}
