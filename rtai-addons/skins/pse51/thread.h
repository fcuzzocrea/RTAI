/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */


#ifndef PSE51_THREAD_H
#define PSE51_THREAD_H

#include "pse51/internal.h"

typedef xnsigmask_t pse51_sigset_t;

struct pse51_thread {
    unsigned magic;
    xnthread_t threadbase;

#define thread2pthread(taddr)                                                   \
((taddr)                                                                        \
 ? ((xnthread_magic(taddr) == PSE51_SKIN_MAGIC)                                 \
    ? ((pthread_t)(((char *)taddr)-(int)(&((pthread_t) 0)->threadbase)))        \
    : NULL)                                                                     \
 : NULL)


    xnholder_t link;	/* Link in pse51_threadq */
    
#define link2pthread(laddr) \
((pthread_t)(((char *)laddr) - (int)(&((pthread_t)0)->link)))
    

    pthread_attr_t attr;        /* creation attributes */

    void *(*entry)(void *arg);  /* start routine */
    void *arg;                  /* start routine argument */

    /* For pthread_join */
    unsigned exiting : 1;
    void *exit_status;
    xnsynch_t join_synch;       /* synchronization object, used by other threads
                                   waiting for this one to finish. */

    /* For pthread_cancel */
    unsigned cancelstate : 2;
    unsigned canceltype : 2;
    unsigned cancel_request:1;
    xnqueue_t cleanup_handlers_q;

    /* errno value for this thread. */
    int err;

    /* For signals handling. */
    pse51_sigset_t sigmask;          /* signals mask. */
    pse51_sigset_t blocked_received; /* blocked signals received. */

    /* For thread specific data. */
    const void *tsd [PTHREAD_KEYS_MAX];
};

#define pse51_current_thread() thread2pthread(xnpod_current_thread())

#define thread_errno() (pse51_current_thread()->err)

#define thread_name(thread) ((thread)->attr.name)

#define thread_exit_status(thread) ((thread)->exit_status)

#define thread_getdetachstate(thread) ((thread)->attr.detachstate)

#define thread_setdetachstate(thread, state) ((thread)->attr.detachstate=state)

#define thread_getcancelstate(thread) ((thread)->cancelstate)

#define thread_setcancelstate(thread, state) ((thread)->cancelstate=state)

#define thread_setcanceltype(thread, type) ((thread)->canceltype=type)

#define thread_getcanceltype(thread) ((thread)->canceltype)

#define thread_clrcancel(thread) ((thread)->cancel_request = 0)

#define thread_setcancel(thread) ((thread)->cancel_request = 1)

#define thread_cleanups(thread) (&(thread)->cleanup_handlers_q)

#define thread_gettsd(thread, key) ((thread)->tsd[key])

#define thread_settsd(thread, key, value) ((thread)->tsd[key]=(value))

void pse51_thread_abort(pthread_t thread, void *status, xnmutex_t *mutex);


static inline int thread_testcancel(pthread_t thread)
{
    return (thread)->cancel_request
        && thread_getcancelstate(thread) == PTHREAD_CANCEL_ENABLE;
}

static inline void
thread_cancellation_point(pthread_t thread, xnmutex_t *mutex)
{
    if( thread_testcancel(thread) )
        pse51_thread_abort(thread, PTHREAD_CANCELED, mutex);
}

void pse51_thread_init(u_long rrperiod);

void pse51_thread_cleanup(void);

/* round-robin period. */
extern xnticks_t pse51_time_slice;

/* threads list */
extern xnqueue_t pse51_threadsq;

#endif /*PSE51_THREAD_H*/
