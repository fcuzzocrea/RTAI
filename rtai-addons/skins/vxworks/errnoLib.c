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

/* FIXME: handle all cases here */
int * wind_current_context_errno(void)
{
    if(!xnpod_asynch_p())
        if(xnthread_test_flags(xnpod_current_thread(), IS_WIND_TASK))
            return &wind_current_task()->errorStatus;

    return NULL;
}


void printErrno(int status)
{
    const char * msg;
    
    switch(status)
    {
    case S_objLib_OBJ_ID_ERROR:
        msg="S_objLib_OBJ_ID_ERROR";
        break;
    case S_objLib_OBJ_UNAVAILABLE:
        msg="S_objLib_OBJ_UNAVAILABLE";
        break;
    case S_objLib_OBJ_DELETED:
        msg="S_objLib_OBJ_DELETED";
        break;
    case S_objLib_OBJ_TIMEOUT:
        msg="S_objLib_OBJ_TIMEOUT";
        break;
    case S_taskLib_NAME_NOT_FOUND:
        msg="S_taskLib_NAME_NOT_FOUND";
        break;
    case S_taskLib_TASK_HOOK_NOT_FOUND:
        msg="S_taskLib_TASK_HOOK_NOT_FOUND";
        break;
    case S_taskLib_ILLEGAL_PRIORITY:
        msg="S_taskLib_ILLEGAL_PRIORITY";
        break;
    case S_taskLib_TASK_HOOK_TABLE_FULL:
        msg="S_taskLib_TASK_HOOK_TABLE_FULL";
        break;
    case S_semLib_INVALID_STATE:
        msg="S_semLib_INVALID_STATE";
        break;
    case S_semLib_INVALID_OPTION:
        msg="S_semLib_INVALID_OPTION";
        break;
    case S_semLib_INVALID_QUEUE_TYPE:
        msg="S_semLib_INVALID_QUEUE_TYPE";
        break;
    case S_semLib_INVALID_OPERATION:
        msg="S_semLib_INVALID_OPERATION";
        break;
    case S_msgQLib_INVALID_MSG_LENGTH:
        msg="S_msgQLib_INVALID_MSG_LENGTH";
        break;
    case S_msgQLib_NON_ZERO_TIMEOUT_AT_INT_LEVEL:
        msg="S_msgQLib_NON_ZERO_TIMEOUT_AT_INT_LEVEL";
        break;
    case S_msgQLib_INVALID_QUEUE_TYPE:
        msg="S_msgQLib_INVALID_QUEUE_TYPE";
        break;
    case S_intLib_NOT_ISR_CALLABLE:
        msg="S_intLib_NOT_ISR_CALLABLE";
        break;
    case S_memLib_NOT_ENOUGH_MEMORY:
        msg="S_memLib_NOT_ENOUGH_MEMORY";
        break;
    default:
        msg="Unknown error";
    }

    xnarch_printf("Status: %s\n", msg);
}


STATUS errnoSet(int status)
{
    int * errno_ptr = wind_current_context_errno();

    if(!errno_ptr)
        return ERROR;

    *errno_ptr=status;
    return OK;
}


int errnoGet(void)
{
    int * errno_ptr = wind_current_context_errno();

    if(!errno_ptr)
        return 0;
    
    return (*errno_ptr);
}


int errnoOfTaskGet(int task_id)
{
    wind_task_t * task;
    int result;

    xnmutex_lock(&__imutex);

    check_OBJ_ID_ERROR(task_id, wind_task_t, task, WIND_TASK_MAGIC, goto error);

    result = task->errorStatus;

    xnmutex_unlock(&__imutex);
    return result;

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}


STATUS errnoOfTaskSet(int task_id, int status )
{
    wind_task_t * task;

    xnmutex_lock(&__imutex);

    check_OBJ_ID_ERROR(task_id, wind_task_t, task, WIND_TASK_MAGIC, goto error);

    task->errorStatus = status;

    xnmutex_unlock(&__imutex);
    return OK;

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}
