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


#include "posix/thread.h"
#include "posix/cancel.h"
#include "posix/signal.h"
#include "posix/tsd.h"

xnticks_t pse51_time_slice;

xnqueue_t pse51_threadq;

static pthread_attr_t default_attr;

static void thread_destroy (pthread_t thread)

{
    removeq(&pse51_threadq, &thread->link);
    xnsynch_destroy(&thread->join_synch);
    xnfree(thread);
}

static void thread_trampoline (void *cookie)
{
    pthread_t thread=(pthread_t ) cookie;
    pthread_exit(thread->entry(thread->arg));
}

static void thread_delete_hook (xnthread_t *xnthread)

{
    pthread_t thread = thread2pthread(xnthread);
    spl_t s;

    if (!thread)
        return;

    xnlock_get_irqsave(&nklock, s);

    pse51_cancel_cleanup_thread(thread);
    pse51_tsd_cleanup_thread(thread);
    pse51_mark_deleted(thread);

    switch (thread_getdetachstate(thread))
	{
	case PTHREAD_CREATE_DETACHED:

	    thread_destroy(thread);
	    break;

	case PTHREAD_CREATE_JOINABLE:

	    if (xnsynch_wakeup_one_sleeper(&thread->join_synch))
		{
		thread_setdetachstate(thread, PTHREAD_CREATE_DETACHED);
                xnpod_schedule();
		}

	    /* The TCB will be freed by the last joiner. */
	    break;

	default:

	    break;
	}

    xnlock_put_irqrestore(&nklock, s);
}

int pthread_create (pthread_t *tid,
		    const pthread_attr_t *attr,
		    void *(*start) (void *),
		    void *arg)
{
    pthread_t thread, cur;
    xnflags_t flags = 0;
    size_t stacksize;
    const char *name;
    int prio;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (!tid || !start)
        return EINVAL;

    if (attr && attr->magic != PSE51_THREAD_ATTR_MAGIC)
        return EINVAL;

    thread = (pthread_t)xnmalloc(sizeof(*thread));

    if (!thread)
	return EAGAIN;

    thread->attr = attr ? *attr : default_attr;

    cur = pse51_current_thread();

    if (thread->attr.inheritsched == PTHREAD_INHERIT_SCHED)
	{
        /* cur may be NULL if pthread_create is not called by a pse51
           thread, in which case trying to inherit scheduling
           parameters is treated as an error. */

        if (!cur)
	    {
            xnfree(thread);
            return EINVAL;
	    }

        thread->attr.policy = cur->attr.policy;
        thread->attr.schedparam = cur->attr.schedparam;
	}

    prio = thread->attr.schedparam.sched_priority;
    stacksize = thread->attr.stacksize;
    name = thread->attr.name;
    
    if (thread->attr.fp)
        flags |= XNFPU;
    
    if (xnpod_init_thread(&thread->threadbase,
			  name,
			  prio,
			  flags,
                          stacksize,
			  PSE51_SKIN_MAGIC) != 0)
	{
	xnfree(thread);
	return EAGAIN;
	}
    
    thread->attr.name = xnthread_name(&thread->threadbase);
    
    inith(&thread->link);
    
    thread->magic = PSE51_THREAD_MAGIC;
    thread->entry = start;
    thread->arg = arg;
    xnsynch_init(&thread->join_synch, XNSYNCH_PRIO);

    pse51_cancel_init_thread(thread);
    pse51_signal_init_thread(thread, cur);
    pse51_tsd_init_thread(thread);
    
    if (thread->attr.policy == SCHED_RR)
	{
	xnthread_time_slice(&thread->threadbase) = pse51_time_slice;
        flags = XNRRB;
	}
    else
        flags = 0;

    xnlock_get_irqsave(&nklock, s);
    appendq(&pse51_threadq,&thread->link);
    xnlock_put_irqrestore(&nklock, s);

    *tid = thread; /* Must be done before the thread is started. */

    xnpod_start_thread(&thread->threadbase,
                       flags,
                       0,
                       thread->attr.affinity,
                       thread_trampoline,
                       thread);

    return 0;
}

int pthread_detach (pthread_t thread)

{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(thread, PSE51_THREAD_MAGIC, struct pse51_thread))
	{
        xnlock_put_irqrestore(&nklock, s);
        return ESRCH;
	}

    if (thread_getdetachstate(thread) != PTHREAD_CREATE_JOINABLE)
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    thread_setdetachstate(thread, PTHREAD_CREATE_DETACHED);
    xnsynch_flush(&thread->join_synch, XNBREAK);
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_equal (pthread_t t1, pthread_t t2) {

    return t1 == t2;
}

void pthread_exit (void *value_ptr)

{
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);
    xnlock_get_irqsave(&nklock, s);
    pse51_thread_abort(pse51_current_thread(), value_ptr);
}

int pthread_join (pthread_t thread, void **value_ptr)

{
    pthread_t cur;
    spl_t s;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    cur = pse51_current_thread();

    xnlock_get_irqsave(&nklock, s);

    if (thread == cur)
	{
        xnlock_put_irqrestore(&nklock, s);
        return EDEADLK;
	}

    if (!pse51_obj_active(thread, PSE51_THREAD_MAGIC, struct pse51_thread)
	&& !pse51_obj_deleted(thread, PSE51_THREAD_MAGIC, struct pse51_thread))
	{
        xnlock_put_irqrestore(&nklock, s);
        return ESRCH;
	}

    if (thread_getdetachstate(thread) != PTHREAD_CREATE_JOINABLE)
	{
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
	}

    while (pse51_obj_active(thread, PSE51_THREAD_MAGIC, struct pse51_thread))
	{
        /* if cur is NULL (joining from the root thread), xnsynch_sleep_on
           will cause a fatal error. */
        xnsynch_sleep_on(&thread->join_synch, XN_INFINITE);
        
        thread_cancellation_point(cur);
        
        /* In case another thread called pthread_detach. */
        if (xnthread_test_flags(&cur->threadbase, XNBREAK) &&
	    thread_getdetachstate(thread) != PTHREAD_CREATE_JOINABLE)
	    {
            xnlock_put_irqrestore(&nklock, s);
            return EINVAL;
	    }
	}

    if (value_ptr)
        *value_ptr = thread_exit_status(thread);

    if(xnsynch_wakeup_one_sleeper(&thread->join_synch) != NULL)
        xnpod_schedule();
    else
        thread_destroy(thread);

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

pthread_t pthread_self (void)

{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);
    return pse51_current_thread();
}

int sched_yield (void)

{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);
    xnpod_yield();
    return 0;
}

void pse51_thread_abort (pthread_t thread, void *status)

{
    thread_exit_status(thread) = status;
    thread_setcancelstate(thread, PTHREAD_CANCEL_DISABLE);
    thread_setcanceltype(thread, PTHREAD_CANCEL_DEFERRED);
    xnpod_delete_thread(&thread->threadbase);
}

void pse51_thread_init (u_long rrperiod)

{
    initq(&pse51_threadq);
    pthread_attr_init(&default_attr);
    pse51_time_slice = rrperiod;
    xnpod_add_hook(XNHOOK_THREAD_DELETE,thread_delete_hook);
}

void pse51_thread_cleanup (void)

{
    xnholder_t *holder;

    while ((holder = getheadq(&pse51_threadq)) != NULL)
	{
        pthread_t thread = link2pthread(holder);

        if (pse51_obj_active(thread, PSE51_THREAD_MAGIC, struct pse51_thread))
	    {
            /* Remaining running thread. */
            thread_setdetachstate(thread, PTHREAD_CREATE_DETACHED);
            pse51_thread_abort(thread, NULL);
	    }
	else
            /* Remaining TCB (joinable thread, which was never joined). */
            thread_destroy(thread);
	}

    xnpod_remove_hook(XNHOOK_THREAD_DELETE,thread_delete_hook);
}

int *pse51_errno_location (void)

{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);
    return &thread_errno();
}
