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

MODULE_DESCRIPTION("XENOMAI-based VxWorks(R) API emulator");
MODULE_AUTHOR("gilles.chanteperdrix@laposte.net");
MODULE_LICENSE("GPL");

static u_long tick_hz_arg = 100; /* Default tick period */
MODULE_PARM(tick_hz_arg,"i");
MODULE_PARM_DESC(tick_hz_arg,"Clock tick frequency (Hz)");

int INIT_MODULE (void);
void CLEANUP_MODULE (void);

static xnpod_t pod;

xnmutex_t __imutex;




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




int INIT_MODULE (void)
{
    u_long nstick = XNPOD_DEFAULT_TICK;
    int err;

    err = xnpod_init(&pod,255,0,0);

    if (err != XN_OK)
        return err;

    xnmutex_init(&__imutex);

    if (MODULE_PARM_VALUE(tick_hz_arg) > 0)
        nstick = 1000000000 / MODULE_PARM_VALUE(tick_hz_arg);

    err = wind_sysclk_init(nstick);

    if (err != XN_OK)
        return err;
    
    wind_wd_init();
    wind_task_hooks_init();
    wind_sem_init();
    wind_msgq_init();
    wind_task_init();
    
    pod.svctable.shutdown = &wind_shutdown;

    xnprintf("VxWorks/VM: starting services.\n");

    return 0;
}




void CLEANUP_MODULE (void)
{
    xnprintf("VxWorks/VM: stopping services.\n");
    wind_shutdown(XNPOD_NORMAL_EXIT);
}

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
