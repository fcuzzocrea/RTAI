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

typedef void (*cleanup_routine_t) (void *);

typedef struct {
    cleanup_routine_t routine;
    void *arg;
    xnholder_t link;

#define link2cleanup_handler(laddr) \
((cleanup_handler_t *)(((char *)laddr)-(int)(&((cleanup_handler_t *)0)->link)))
} cleanup_handler_t;


int pthread_cancel(pthread_t thread)
{
    int cancel_enabled;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);
    if (!pse51_obj_active(thread,PSE51_THREAD_MAGIC,struct pse51_thread)) {
	xnmutex_unlock(&__imutex);
	return ESRCH;
    }

    if( (cancel_enabled = thread_getcancelstate(thread)==PTHREAD_CANCEL_ENABLE)
       && thread_getcanceltype(thread)==PTHREAD_CANCEL_ASYNCHRONOUS)
        pse51_thread_abort(thread, PTHREAD_CANCELED, &__imutex);
    else {
        /* pthread_cancel is not a cancellation point, so
           thread == pthread_self() is not a special case. */
        thread_setcancel(thread);
        if(cancel_enabled) {
            /* Unblock thread, so that it can honor the cancellation request. */
            xnpod_unblock_thread(&thread->threadbase);
            xnpod_schedule(&__imutex);
        }
    }
    xnmutex_unlock(&__imutex);

    return 0;
}


void pthread_cleanup_push(cleanup_routine_t routine, void *arg)
{
    cleanup_handler_t *handler;

    if(!routine)
        return;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    /* The allocation is inside the critical section in order to make the
       function async-signal safe, that is in order to avoid leaks if an
       asynchronous cancellation request could occur between the call to
       xnmalloc and xnmutex_lock. */
    xnmutex_lock(&__imutex);
    if(!(handler = xnmalloc(sizeof(*handler)))) {
        xnmutex_unlock(&__imutex);
        return ;
    }
    handler->routine=routine;
    handler->arg=arg;
    inith(&handler->link);

    prependq(thread_cleanups(pse51_current_thread()), &handler->link);
    xnmutex_unlock(&__imutex);
}


void pthread_cleanup_pop(int execute)
{
    xnholder_t *holder;
    cleanup_handler_t *handler;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);
    if(!(holder=getq(thread_cleanups(pse51_current_thread())))) {
        xnmutex_unlock(&__imutex);
        return;
    }

    handler=link2cleanup_handler(holder);
    if(execute)
        handler->routine(handler->arg);
    xnmutex_unlock(&__imutex);

    xnfree(handler);
}


int pthread_setcanceltype(int type, int *oldtype_ptr)
{
    int oldtype;
    pthread_t cur;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);
    
    switch(type) {
    default:
        return EINVAL;
    case PTHREAD_CANCEL_DEFERRED:
    case PTHREAD_CANCEL_ASYNCHRONOUS:
        break;
    }

    cur = pse51_current_thread();
    
    xnmutex_lock(&__imutex);
    oldtype=thread_getcanceltype(cur);
    thread_setcanceltype(cur, type);

    if(type == PTHREAD_CANCEL_ASYNCHRONOUS
       && thread_getcancelstate(cur) == PTHREAD_CANCEL_ENABLE)
        thread_cancellation_point(cur, &__imutex);

    if(oldtype_ptr)
        *oldtype_ptr=oldtype;
    xnmutex_unlock(&__imutex);

    return 0;
}


int pthread_setcancelstate(int state, int *oldstate_ptr)
{
    int oldstate;
    pthread_t cur;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);
    
    switch(state) {
    default:
        return EINVAL;
    case PTHREAD_CANCEL_ENABLE:
    case PTHREAD_CANCEL_DISABLE:
        break;
    }

    cur = pse51_current_thread();

    xnmutex_lock(&__imutex);
    oldstate = thread_getcancelstate(cur);
    thread_setcancelstate(cur, state);

    if(state == PTHREAD_CANCEL_ENABLE
       && thread_getcanceltype(cur) == PTHREAD_CANCEL_ASYNCHRONOUS)
        thread_cancellation_point(cur, &__imutex);
    
    if(oldstate_ptr)
        *oldstate_ptr=oldstate;
    xnmutex_unlock(&__imutex);

    return 0;
}


void pthread_testcancel(void)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);
    thread_cancellation_point(pse51_current_thread(), &__imutex);
    xnmutex_unlock(&__imutex);
}


void pse51_cancel_init_thread(pthread_t thread)
{
    thread_setcancelstate(thread, PTHREAD_CANCEL_ENABLE);
    thread_setcanceltype(thread, PTHREAD_CANCEL_DEFERRED);
    thread_clrcancel(thread);
    initq(thread_cleanups(thread));
}

void pse51_cancel_cleanup_thread(pthread_t thread)
{
    xnholder_t *holder;

    while((holder = getq(thread_cleanups(thread)))) {
        cleanup_handler_t *handler = link2cleanup_handler(holder);
        handler->routine(handler->arg);
        xnfree(handler);
    }
}
