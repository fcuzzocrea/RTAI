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


#include "pse51/thread.h"
#include "pse51/cancel.h"
#include "pse51/signal.h"
#include "pse51/tsd.h"

xnticks_t pse51_time_slice;
xnqueue_t pse51_threadsq;
static pthread_attr_t default_attr;

static void thread_trampoline (void *cookie);
static void thread_delete_hook (xnthread_t *xnthread);
static void thread_destroy(pthread_t thread);



int pthread_create(pthread_t *tid,const pthread_attr_t *attr,
                   void *(*start) (void *),void *arg)
{
    xnflags_t flags = 0;
    pthread_t thread, cur;
    int prio;
    size_t stacksize;
    const char *name;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if(!tid || !start)
        return EINVAL;

    xnmutex_lock(&__imutex);
    if(attr && attr->magic != PSE51_THREAD_ATTR_MAGIC ) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }
    xnmutex_unlock(&__imutex);

    if(!(thread = (pthread_t) xnmalloc(sizeof(*thread))))
	return EAGAIN;

    xnmutex_lock(&__imutex);
    thread->attr = attr ? *attr : default_attr;
    xnmutex_unlock(&__imutex);

    cur=pse51_current_thread();
    if(thread->attr.inheritsched == PTHREAD_INHERIT_SCHED) {
        /* cur may be NULL if pthread_create is not called by a pse51 thread, in
           which case trying to inherit scheduling parameters is treated as an
           error. */
        if(!cur) {
            xnfree(thread);
            return EINVAL;
        }

        thread->attr.policy = cur->attr.policy;
        thread->attr.schedparam = cur->attr.schedparam;
    }

    prio = thread->attr.schedparam.sched_priority;
    stacksize = thread->attr.stacksize;
    name = thread->attr.name;
    
    if(thread->attr.fp)
        flags |= XNFPU;
    
    if (xnpod_init_thread(&thread->threadbase, name, prio, flags,
                          stacksize, NULL, PSE51_SKIN_MAGIC) != XN_OK) {
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
    
    if(thread->attr.policy == SCHED_RR) {        
        xnthread_time_slice(&thread->threadbase) = pse51_time_slice;
        flags = XNRRB;
    } else
        flags = 0;

    xnmutex_lock(&__imutex);
    appendq(&pse51_threadsq,&thread->link);
    xnmutex_unlock(&__imutex);

    *tid = thread;

    xnpod_start_thread(&thread->threadbase,flags,0,thread_trampoline,thread);

    return 0;
}



int pthread_detach(pthread_t thread)
{
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(thread, PSE51_THREAD_MAGIC, struct pse51_thread)) {
        xnmutex_unlock(&__imutex);
        return ESRCH;
    }

    if(thread_getdetachstate(thread) != PTHREAD_CREATE_JOINABLE) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }
    thread_setdetachstate(thread, PTHREAD_CREATE_DETACHED);
    xnsynch_flush(&thread->join_synch, XNBREAK);
    xnmutex_unlock(&__imutex);

    return 0;
}



int pthread_equal(pthread_t t1, pthread_t t2)
{
    return t1 == t2;
}



void pthread_exit(void *value_ptr)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);
    pse51_thread_abort(pse51_current_thread(), value_ptr, &__imutex);
}



int pthread_join(pthread_t thread, void **value_ptr)
{
    pthread_t cur;
    int not_last;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    cur = pse51_current_thread();

    xnmutex_lock(&__imutex);
    if(thread == cur) {
        xnmutex_unlock(&__imutex);
        return EDEADLK;
    }

    if(!pse51_obj_active(thread, PSE51_THREAD_MAGIC, struct pse51_thread)
       && !pse51_obj_deleted(thread, PSE51_THREAD_MAGIC, struct pse51_thread)) {
        xnmutex_unlock(&__imutex);
        return ESRCH;
    }

    if(thread_getdetachstate(thread) != PTHREAD_CREATE_JOINABLE) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    while(pse51_obj_active(thread, PSE51_THREAD_MAGIC, struct pse51_thread)) {
        xnthread_clear_flags(&cur->threadbase, XNBREAK);
        xnsynch_sleep_on(&thread->join_synch, XN_INFINITE, &__imutex);
        
        if(cur)
            thread_cancellation_point(cur, &__imutex);
        
        /* In case another thread called pthread_detach. */
        if(xnthread_test_flags(&cur->threadbase, XNBREAK) &&
           thread_getdetachstate(thread) != PTHREAD_CREATE_JOINABLE) {
            xnmutex_unlock(&__imutex);
            return EINVAL;
        }
    }
    if(value_ptr)
        *value_ptr = thread_exit_status(thread);
    not_last=(xnsynch_wakeup_one_sleeper(&thread->join_synch)!=NULL);
    xnmutex_unlock(&__imutex);

    if(not_last)
        xnpod_schedule(NULL);
    else
        thread_destroy(thread);

    return 0;
}



pthread_t pthread_self(void)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    return pse51_current_thread();
}



int sched_yield(void)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnpod_yield();

    return 0;
}
    


void pse51_thread_abort(pthread_t thread, void *status, xnmutex_t *mutex)
{
    thread_exit_status(thread) = status;
    thread_setcancelstate(thread, PTHREAD_CANCEL_DISABLE);
    thread_setcanceltype(thread, PTHREAD_CANCEL_DEFERRED);

    xnpod_delete_thread(&thread->threadbase, mutex);
}



void pse51_thread_init(u_long rrperiod)
{
    initq(&pse51_threadsq);
    pthread_attr_init(&default_attr);
    pse51_time_slice = rrperiod;

    xnpod_add_hook(XNHOOK_THREAD_DELETE,thread_delete_hook);
}



void pse51_thread_cleanup(void)
{
    xnholder_t *holder;

    while ((holder = getheadq(&pse51_threadsq)) != NULL) {
        pthread_t thread = link2pthread(holder);
        if(pse51_obj_active(thread, PSE51_THREAD_MAGIC, struct pse51_thread)) {
            /* Remaining running thread. */
            thread_setdetachstate(thread, PTHREAD_CREATE_DETACHED);
            pse51_thread_abort(thread, NULL, NULL);
        } else
            /* Remaining TCB (joinable thread, which was never joined). */
            thread_destroy(thread);
    }

    xnpod_remove_hook(XNHOOK_THREAD_DELETE,thread_delete_hook);
}



static void thread_trampoline (void *cookie)
{
    pthread_t thread=(pthread_t ) cookie;
    pthread_exit(thread->entry(thread->arg));
}



static void thread_delete_hook (xnthread_t *xnthread)
{
    pthread_t thread;

    if(!(thread=thread2pthread(xnthread)))
        return;

    xnmutex_lock(&__imutex);
    pse51_cancel_cleanup_thread(thread);
    pse51_tsd_cleanup_thread(thread);
    pse51_mark_deleted(thread);

    switch(thread_getdetachstate(thread)) {
    case PTHREAD_CREATE_DETACHED:
        thread_destroy(thread);
        break;

    case PTHREAD_CREATE_JOINABLE:
        if(xnsynch_wakeup_one_sleeper(&thread->join_synch)) {
            thread_setdetachstate(thread, PTHREAD_CREATE_DETACHED);
            xnpod_schedule(&__imutex);
        }
        /* The TCB will be freed by the last joiner. */
        break;
    default:
	break;
    }
    xnmutex_unlock(&__imutex);
}



static void thread_destroy(pthread_t thread)
{
    removeq(&pse51_threadsq, &thread->link);
    xnsynch_destroy(&thread->join_synch);
    xnfree(thread);
}
