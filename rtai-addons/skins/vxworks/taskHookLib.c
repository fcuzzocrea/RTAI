/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * As a special exception, the RTAI project gives permission
 * for additional uses of the text contained in its release of
 * Xenomai.
 *
 * The exception is that, if you link the Xenomai libraries with other
 * files to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the Xenomai libraries code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public
 * License.
 *
 * This exception applies only to the code released by the
 * RTAI project under the name Xenomai.  If you copy code from other
 * RTAI project releases into a copy of Xenomai, as the General Public
 * License permits, the exception does not apply to the code that you
 * add in this way.  To avoid misleading anyone as to the status of
 * such modified files, you must delete this exception notice from
 * them.
 *
 * If you write modifications of your own for Xenomai, it is your
 * choice whether to permit this exception to apply to your
 * modifications. If you do not wish that, delete this exception
 * notice.
 */

#include "rtai_config.h"
#include "vxworks/defs.h"

typedef struct wind_hook 
{
    FUNCPTR function;
    xnholder_t link;

#define link2wind_hook(laddr)                                           \
((wind_hook_t *)(((char *)laddr) - (int)(&((wind_hook_t *)0)->link)))

} wind_hook_t;

static xnqueue_t wind_create_hooks_q;
static xnqueue_t wind_switch_hooks_q;
static xnqueue_t wind_delete_hooks_q;
static wind_task_t * previous_task;

static void create_hook (xnthread_t * xnthread);
static void switch_hook (xnthread_t * xnthread);
static void delete_hook (xnthread_t * xnthread);


void wind_task_hooks_init(void)
{
    initq(&wind_create_hooks_q);
    initq(&wind_switch_hooks_q);
    initq(&wind_delete_hooks_q);

    previous_task = NULL;
    
    xnpod_add_hook(XNHOOK_THREAD_START,create_hook);
    xnpod_add_hook(XNHOOK_THREAD_SWITCH,switch_hook);
    xnpod_add_hook(XNHOOK_THREAD_DELETE,delete_hook);
}


static inline void free_hooks_queue (xnqueue_t * queue)
{
    xnholder_t * holder;
    xnholder_t * next_holder;
    
    for(holder = getheadq(queue); holder ; holder=next_holder)
    {
        next_holder = nextq(queue, holder);
        removeq(queue, holder);
        xnfree(link2wind_hook(holder));
    }
}


void wind_task_hooks_cleanup(void)
{
    xnmutex_lock(&__imutex);

    free_hooks_queue(&wind_create_hooks_q);
    free_hooks_queue(&wind_switch_hooks_q);
    free_hooks_queue(&wind_delete_hooks_q);

    xnpod_remove_hook(XNHOOK_THREAD_START,create_hook);
    xnpod_remove_hook(XNHOOK_THREAD_SWITCH,switch_hook);
    xnpod_remove_hook(XNHOOK_THREAD_DELETE,delete_hook);

    xnmutex_unlock(&__imutex);
}




static inline STATUS hook_add( xnqueue_t * queue,
                               int (*adder)( xnqueue_t *, xnholder_t * ),
                               FUNCPTR wind_hook )
{
    wind_hook_t * hook = (wind_hook_t *) xnmalloc(sizeof(wind_hook_t));

    if(!hook)
    {
        wind_errnoset(S_taskLib_TASK_HOOK_TABLE_FULL);
        return ERROR;
    }

    hook->function = wind_hook;
    inith(&hook->link);
    
    xnmutex_lock(&__imutex);
    adder(queue, &hook->link);
    xnmutex_unlock(&__imutex);

    return OK;
}


static inline STATUS hook_del(xnqueue_t * queue, FUNCPTR wind_hook)
{
    xnholder_t * holder;

    for( holder = getheadq(queue); holder ; holder = nextq(queue, holder))
        if(link2wind_hook(holder)->function == wind_hook)
            break;

    if(!holder) {
        wind_errnoset(S_taskLib_TASK_HOOK_NOT_FOUND);
        return ERROR;
    }
    
    xnmutex_lock(&__imutex);
    removeq(queue, holder);
    xnmutex_unlock(&__imutex);

    return OK;
}




STATUS taskCreateHookAdd (wind_create_hook hook)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    return hook_add(&wind_create_hooks_q, appendq, (FUNCPTR) hook);
}


STATUS taskCreateHookDelete (wind_create_hook hook)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    return hook_del(&wind_create_hooks_q, (FUNCPTR) hook);
}


STATUS taskSwitchHookAdd (wind_switch_hook hook)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    return hook_add(&wind_switch_hooks_q, appendq, (FUNCPTR) hook);
}


STATUS taskSwitchHookDelete (wind_switch_hook hook)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    return hook_del(&wind_switch_hooks_q, (FUNCPTR) hook);
}


STATUS taskDeleteHookAdd (wind_delete_hook hook)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    return hook_add(&wind_delete_hooks_q, prependq, (FUNCPTR) hook);
}


STATUS taskDeleteHookDelete (wind_delete_hook hook)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    return hook_del(&wind_delete_hooks_q, (FUNCPTR) hook);
}




static void create_hook (xnthread_t * xnthread)
{
    xnholder_t * holder;
    wind_task_t * task = thread2wind_task(xnthread);
    wind_create_hook hook;
    
    for( holder = getheadq(&wind_create_hooks_q) ; holder != NULL ;
         holder = nextq(&wind_create_hooks_q, holder) )
    {
        hook = (wind_create_hook) (link2wind_hook(holder)->function);
        hook(task);
    }
}


static void switch_hook (xnthread_t * xnthread)
{
    xnholder_t * holder;
    wind_task_t * task;
    wind_switch_hook hook;

    task = thread2wind_task(xnthread);
    
    for( holder = getheadq(&wind_switch_hooks_q) ; holder != NULL ;
         holder = nextq(&wind_switch_hooks_q, holder) )
    {
        hook = (wind_switch_hook) (link2wind_hook(holder)->function);
        hook(previous_task, task);
    }
    previous_task = task;
}


static void delete_hook (xnthread_t * xnthread)
{
    xnholder_t * holder;
    wind_task_t * task = thread2wind_task(xnthread);
    wind_delete_hook hook;
    
    for( holder = getheadq(&wind_delete_hooks_q) ; holder != NULL ;
         holder = nextq(&wind_delete_hooks_q, holder) )
    {
        hook = (wind_delete_hook) (link2wind_hook(holder)->function);
        hook(task);
    }
}
