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

#define WIND_MAX_PRIORITIES 255

/* WIND_DEFAULT_NAME_LEN = strlen("t")+log10(ULONG_MAX) */
#define WIND_DEFAULT_NAME_LEN 11

static xnqueue_t wind_tasks_q;
static unsigned long int tasks_count;
static xnticks_t rrperiod;

static void testSafe (wind_task_t *task);
static void wind_task_delete_hook (xnthread_t *xnthread);
static void wind_task_trampoline (void *cookie);




void wind_task_init (void)
{
    tasks_count = 0UL;
    rrperiod = 0;
    initq(&wind_tasks_q);
    
    xnpod_add_hook(XNHOOK_THREAD_DELETE,wind_task_delete_hook);
}


void wind_task_cleanup (void)
{
    xnholder_t *holder;

    while ((holder = getheadq(&wind_tasks_q)) != NULL)
        taskDeleteForce((int) link2wind_task(holder));
    
    xnpod_remove_hook(XNHOOK_THREAD_DELETE,wind_task_delete_hook);
}


void wind_set_rrperiod( xnticks_t ticks )
{
    rrperiod = ticks;
}




STATUS taskInit(WIND_TCB * handle,
                char * name,
                int prio,
                int flags,
                char * stack __attribute__ ((unused)),
                int stacksize,
                FUNCPTR entry,
                int arg0, int arg1, int arg2, int arg3, int arg4,
                int arg5, int arg6, int arg7, int arg8, int arg9 )
{
    char aname[WIND_DEFAULT_NAME_LEN+1]="t";
    xnflags_t bflags = 0;

    check_NOT_ISR_CALLABLE(return ERROR);

    if (prio < 0 || prio > WIND_MAX_PRIORITIES)
    {
        wind_errnoset(S_taskLib_ILLEGAL_PRIORITY);
        return ERROR;
    }
    
    if (stacksize < 1024)
        return ERROR;

    /* We forbid to use twice the same tcb */
    if(!handle || handle->magic == WIND_TASK_MAGIC)
    {
        wind_errnoset(S_objLib_OBJ_ID_ERROR);
        return ERROR;
    }

    /* TODO: check what happens here in the real OS
    if(flags & ~WIND_TASK_OPTIONS_MASK)
    {
        wind_task_errnoset();
        return ERROR;
    }
    */

    if (flags & VX_FP_TASK)
        bflags |= XNFPU;

    /*  not implemented: VX_PRIVATE_ENV, VX_NO_STACK_FILL, VX_UNBREAKABLE */
    
    handle->flow_id = tasks_count++;
    if (!name) {
        sprintf(aname+1, "%lu", handle->flow_id);
        name = aname;
    }
        
    if ( xnpod_init_thread(&handle->threadbase,name,prio,bflags,
                           (unsigned) stacksize,NULL,VXWORKS_SKIN_MAGIC) != XN_OK )
        return ERROR;

    
    /* finally set the Tcb after error conditions checking */
    handle->magic = WIND_TASK_MAGIC;
    handle->name = handle->threadbase.name;
    handle->flags = flags;
    handle->prio = prio;
    handle->entry = entry;
    handle->errorStatus = 0;

    xnthread_set_flags(&handle->threadbase, IS_WIND_TASK);
    xnthread_time_slice(&handle->threadbase) = rrperiod;
    
    handle->safecnt=0;
    xnsynch_init(&handle->safesync, 0);

    /* TODO: fill in attributes of wind_task_t:
       handle->status
     */

    handle->auto_delete = 0;
    inith(&handle->link);
    
    handle->arg0 = arg0;
    handle->arg1 = arg1;
    handle->arg2 = arg2;
    handle->arg3 = arg3;
    handle->arg4 = arg4;
    handle->arg5 = arg5;
    handle->arg6 = arg6;
    handle->arg7 = arg7;
    handle->arg8 = arg8;
    handle->arg9 = arg9;

    xnmutex_lock(&__imutex);
    appendq(&wind_tasks_q,&handle->link);
    xnmutex_unlock(&__imutex);

    return OK;
}


STATUS taskActivate(int task_id)
{
    wind_task_t * task;

    if(task_id == 0)
        return ERROR;

    xnmutex_lock(&__imutex);

    check_OBJ_ID_ERROR(task_id,wind_task_t,task,WIND_TASK_MAGIC,goto error );
    
    if( !xnthread_test_flags(&(task->threadbase), XNDORMANT) )
        goto error;
    
    xnpod_start_thread( &task->threadbase, XNRRB, 0, wind_task_trampoline,
                        task );

    xnmutex_unlock(&__imutex);
    
    return OK;
    
 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
    
}


int taskSpawn ( char * name,
                int prio,
                int flags,
                int stacksize,
                FUNCPTR entry,
                int arg0, int arg1, int arg2, int arg3, int arg4,
                int arg5, int arg6, int arg7, int arg8, int arg9 )
{
    wind_task_t * task;
    int task_id;
    STATUS status;
    
    check_NOT_ISR_CALLABLE(return ERROR);

    check_alloc(wind_task_t, task, return ERROR);
    task_id = (int) task;
    
    status = taskInit( task, name, prio, flags, NULL, stacksize, entry, arg0,
                       arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9
                       );
    
    if(status == ERROR)
        goto error;

    task->auto_delete = 1;
    status = taskActivate(task_id);
    
    if(status == ERROR)
        goto error;
    
    return task_id;
    
 error:
    taskDeleteForce(task_id);
    return ERROR;
}


STATUS taskDeleteForce (int task_id)
{
    wind_task_t *task;

    check_NOT_ISR_CALLABLE(return ERROR);

    if (task_id == 0)
        xnpod_delete_self(NULL); /* Never returns */
    
    xnmutex_lock(&__imutex);
    check_OBJ_ID_ERROR(task_id,wind_task_t,task,WIND_TASK_MAGIC,goto error );
    xnpod_delete_thread(&task->threadbase,&__imutex);
    xnmutex_unlock(&__imutex);

    return OK;

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}


STATUS taskDelete(int task_id)
{
    wind_task_t *task;
    unsigned long int flow_id;
    
    check_NOT_ISR_CALLABLE(return ERROR);

    if (task_id == 0)
        xnpod_delete_self(NULL);
    
    xnmutex_lock(&__imutex);

    check_OBJ_ID_ERROR(task_id,wind_task_t,task,WIND_TASK_MAGIC,goto error );
    flow_id = task->flow_id;
    testSafe(task);

    /* we use flow_id here just in case task was destroyed and the block
       reused for another task by the allocator */
    if(!wind_h2obj_active(task, WIND_TASK_MAGIC, wind_task_t)
       || task->flow_id != flow_id)
    {
	wind_errnoset(S_objLib_OBJ_DELETED);
	goto error;
    }

    xnpod_delete_thread(&task->threadbase,&__imutex);
    xnmutex_unlock(&__imutex);

    return OK;

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}


void taskExit(int code)
{
    wind_task_t * task;
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    task = wind_current_task();
    task->errorStatus = code;
    xnpod_delete_self(NULL);
}


STATUS taskSuspend (int task_id)
{
    wind_task_t *task;
    
    if (task_id == 0)
    {
        xnpod_suspend_self(NULL);
        return OK;
    }

    xnmutex_lock(&__imutex);

    check_OBJ_ID_ERROR(task_id,wind_task_t,task,WIND_TASK_MAGIC,goto error );
        
/*     if (testbits(xnthread_status_flags(&task->threadbase),XNSUSP)) */
/*         goto error; */
        
    if (!xnpod_suspend_thread(&task->threadbase, XNSUSP, XN_INFINITE, NULL,
                              &__imutex)
        )
        xnpod_schedule(&__imutex);

    xnmutex_unlock(&__imutex);

    return OK;

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}


STATUS taskResume (int task_id)
{
    wind_task_t *task;

    xnmutex_lock(&__imutex);

    check_OBJ_ID_ERROR(task_id,wind_task_t,task,WIND_TASK_MAGIC,goto error );
        
/*     if (!testbits(xnthread_status_flags(&task->threadbase),XNSUSP)) */
/*         goto error; */
        
    xnpod_resume_thread(&task->threadbase,XNSUSP);

    xnmutex_unlock(&__imutex);
    xnpod_schedule(NULL);
    return OK;
 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}


STATUS taskRestart ( int task_id )
{
    wind_task_t * task;

    check_NOT_ISR_CALLABLE(return ERROR);

    xnmutex_lock(&__imutex);

    if(task_id == 0)
        task = wind_current_task();
    else
        check_OBJ_ID_ERROR(task_id,wind_task_t,task,WIND_TASK_MAGIC,goto error);

    xnpod_restart_thread(&task->threadbase,&__imutex);

    xnmutex_unlock(&__imutex);
    return OK;

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}


STATUS taskPrioritySet (int task_id, int prio)
{
    wind_task_t *task;

    if ( prio<0 || prio>WIND_MAX_PRIORITIES )
        wind_errnoset(S_taskLib_ILLEGAL_PRIORITY);
        
    xnmutex_lock(&__imutex);

    if (task_id == 0)
        task = wind_current_task();
    else
        check_OBJ_ID_ERROR(task_id,wind_task_t,task, WIND_TASK_MAGIC,goto error);
    
    xnpod_renice_thread(&task->threadbase,prio);
    task->prio=prio;

    xnmutex_unlock(&__imutex);
    xnpod_schedule(NULL);
    return OK;

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}


STATUS taskPriorityGet ( int task_id, int * pprio )
{
    wind_task_t *task;
    
    xnmutex_lock(&__imutex);

    if (task_id == 0)
        task = wind_current_task();
    else
        check_OBJ_ID_ERROR(task_id,wind_task_t,task,WIND_TASK_MAGIC,goto error);

    *pprio = xnthread_current_priority(&task->threadbase);

    xnmutex_unlock(&__imutex);
    return OK;

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}


STATUS taskLock(void)
{
    check_NOT_ISR_CALLABLE(return ERROR);

    xnpod_lock_sched();

    return OK;
}


STATUS taskUnlock(void)
{
    check_NOT_ISR_CALLABLE(return ERROR);

    xnpod_unlock_sched();

    return OK;
}


int taskIdSelf (void)
{
    check_NOT_ISR_CALLABLE(return ERROR);

    return (int) wind_current_task();
}


STATUS taskSafe (void)
{
    xnmutex_lock(&__imutex);
    wind_current_task()->safecnt++;
    xnmutex_unlock(&__imutex);

    return OK;
}


STATUS taskUnsafe (void)
{
    xnmutex_lock(&__imutex);

    if( wind_current_task()->safecnt == 0 )
        goto error;
    
    if( --wind_current_task()->safecnt == 0 &&
        xnsynch_flush(&wind_current_task()->safesync,0) == XNSYNCH_RESCHED)
        xnpod_schedule(&__imutex);

    xnmutex_unlock(&__imutex);
    return OK;
    
 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}


STATUS taskDelay ( int ticks )
{
    check_NOT_ISR_CALLABLE(return ERROR);

    /* TODO: manage to detect that an interrupt occured and return EINTR
       Postponed till implementation of signals */

    if (ticks > 0)
        xnpod_delay(ticks);
    else
        xnpod_yield();

    return OK;
}


/* TODO: check if TaskIdVerify or TaskTcb accept 0 as argument */
STATUS taskIdVerify( int task_id )
{
    wind_task_t * task;

    check_OBJ_ID_ERROR(task_id,wind_task_t,task,WIND_TASK_MAGIC,return ERROR );

    return OK;
}
    

wind_task_t * taskTcb( int task_id )
{
    wind_task_t * task;
    
    check_OBJ_ID_ERROR(task_id,wind_task_t,task,WIND_TASK_MAGIC,return NULL );

    return task;
}


/* We put this function here and not in taskInfo.c because it needs access to
   wind_tasks_q */
int taskNameToId ( char * name )
{
    xnholder_t * holder;
    wind_task_t * task;
    int result = ERROR;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if(!name)
        return ERROR;

    xnmutex_lock(&__imutex);

    for( holder = getheadq(&wind_tasks_q); holder;
         holder = nextq(&wind_tasks_q, holder) )
    {
        task = link2wind_task(holder);
        if(!strcmp(name, task->name))
        {
            result = (int) task;
            break;
        }
    }
    
    if(result == ERROR)
        wind_errnoset(S_taskLib_NAME_NOT_FOUND);
    
    xnmutex_unlock(&__imutex);

    return result;
}





/* Scheduler must be locked on entry */
static void testSafe (wind_task_t *task)
{
    xnmutex_lock(&__imutex);

    xnpod_unlock_sched();
    
    while (task->safecnt > 0)
    {
        xnsynch_sleep_on(&task->safesync,XN_INFINITE,&__imutex);
    }

    xnpod_lock_sched();
    
    xnmutex_unlock(&__imutex);
}


static void wind_task_delete_hook (xnthread_t *xnthread)
{
    wind_task_t *task;

    if (xnthread_magic(xnthread) != VXWORKS_SKIN_MAGIC)
	return;

    task = thread2wind_task(xnthread);

    xnsynch_destroy(&task->safesync);
   
    removeq(&wind_tasks_q,&task->link);

    xnthread_clear_flags(xnthread, IS_WIND_TASK);
    wind_mark_deleted(task);

    if(task->auto_delete)
        xnfree(task);
}


static void wind_task_trampoline (void *cookie)
{
    wind_task_t *task = (wind_task_t *)cookie;

    task->entry( task->arg0, task->arg1, task->arg2, task->arg3, task->arg4, 
                 task->arg5, task->arg6, task->arg7, task->arg8, task->arg9 );

    taskDeleteForce((int) task);
}
