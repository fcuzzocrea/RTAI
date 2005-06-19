/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * RTAI/fusion is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RTAI/fusion; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "rtai_config.h"
#include "vxworks/defs.h"

MODULE_DESCRIPTION("VxWorks(R) virtual machine");
MODULE_AUTHOR("gilles.chanteperdrix@laposte.net");
MODULE_LICENSE("GPL");

/* Default tick period */
static u_long tick_hz_arg = 1000000000 / XNPOD_DEFAULT_TICK;
module_param_named(tick_hz,tick_hz_arg,ulong,0444);
MODULE_PARM_DESC(tick_hz,"Clock tick frequency (Hz)");

static xnpod_t pod;



static void wind_shutdown (int xtype)
{
    xnpod_lock_sched();

    wind_sysclk_cleanup();
    wind_msgq_cleanup();
    wind_sem_cleanup();
    wind_wd_cleanup();
    wind_task_hooks_cleanup();
    wind_task_cleanup();

    xnpod_shutdown(xtype);
}




int __fusion_skin_init (void)
{
    int err;

    err = xnpod_init(&pod,255,0,0);

    if (err != 0)
        return err;

    err = wind_sysclk_init(module_param_value(tick_hz_arg));

    if (err != 0)
        {
        xnpod_shutdown(err);    
        return err;
        }

    wind_wd_init();
    wind_task_hooks_init();
    wind_sem_init();
    wind_msgq_init();
    wind_task_init();
    
    xnprintf("starting VxWorks services.\n");

    return 0;
}

void __fusion_skin_exit (void)
{
    xnprintf("stopping VxWorks services.\n");
    wind_shutdown(XNPOD_NORMAL_EXIT);
}

module_init(__fusion_skin_init);
module_exit(__fusion_skin_exit);

/* exported API : */

EXPORT_SYMBOL(wind_current_context_errno);
EXPORT_SYMBOL(printErrno);
EXPORT_SYMBOL(errnoSet);
EXPORT_SYMBOL(errnoGet);
EXPORT_SYMBOL(errnoOfTaskGet);
EXPORT_SYMBOL(errnoOfTaskSet);
EXPORT_SYMBOL(taskSpawn);
EXPORT_SYMBOL(taskInit);
EXPORT_SYMBOL(taskActivate);
EXPORT_SYMBOL(taskExit);
EXPORT_SYMBOL(taskDelete);
EXPORT_SYMBOL(taskDeleteForce);
EXPORT_SYMBOL(taskSuspend);
EXPORT_SYMBOL(taskResume);
EXPORT_SYMBOL(taskRestart);
EXPORT_SYMBOL(taskPrioritySet);
EXPORT_SYMBOL(taskPriorityGet);
EXPORT_SYMBOL(taskLock);
EXPORT_SYMBOL(taskUnlock);
EXPORT_SYMBOL(taskIdSelf);
EXPORT_SYMBOL(taskSafe);
EXPORT_SYMBOL(taskUnsafe);
EXPORT_SYMBOL(taskDelay);
EXPORT_SYMBOL(taskIdVerify);
EXPORT_SYMBOL(taskTcb);
EXPORT_SYMBOL(taskCreateHookAdd);
EXPORT_SYMBOL(taskCreateHookDelete);
EXPORT_SYMBOL(taskSwitchHookAdd);
EXPORT_SYMBOL(taskSwitchHookDelete);
EXPORT_SYMBOL(taskDeleteHookAdd);
EXPORT_SYMBOL(taskDeleteHookDelete);
EXPORT_SYMBOL(taskName);
EXPORT_SYMBOL(taskNameToId);
EXPORT_SYMBOL(taskIdDefault);
EXPORT_SYMBOL(taskIsReady);
EXPORT_SYMBOL(taskIsSuspended);
EXPORT_SYMBOL(semGive);
EXPORT_SYMBOL(semTake);
EXPORT_SYMBOL(semFlush);
EXPORT_SYMBOL(semDelete);
EXPORT_SYMBOL(semBCreate);
EXPORT_SYMBOL(semMCreate);
EXPORT_SYMBOL(semCCreate);
EXPORT_SYMBOL(wdCreate);
EXPORT_SYMBOL(wdDelete);
EXPORT_SYMBOL(wdStart);
EXPORT_SYMBOL(wdCancel);
EXPORT_SYMBOL(msgQCreate);
EXPORT_SYMBOL(msgQDelete);
EXPORT_SYMBOL(msgQNumMsgs);
EXPORT_SYMBOL(msgQReceive);
EXPORT_SYMBOL(msgQSend);
EXPORT_SYMBOL(intContext);
EXPORT_SYMBOL(intCount);
EXPORT_SYMBOL(intLevelSet);
EXPORT_SYMBOL(intLock);
EXPORT_SYMBOL(intUnlock);
EXPORT_SYMBOL(sysClkConnect);
EXPORT_SYMBOL(sysClkDisable);
EXPORT_SYMBOL(sysClkEnable);
EXPORT_SYMBOL(sysClkRateGet);
EXPORT_SYMBOL(sysClkRateSet);
EXPORT_SYMBOL(tickAnnounce);
EXPORT_SYMBOL(tickGet);
EXPORT_SYMBOL(tickSet);
EXPORT_SYMBOL(kernelTimeSlice);
EXPORT_SYMBOL(kernelVersion);
