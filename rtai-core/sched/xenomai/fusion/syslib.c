/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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

#include <sys/types.h>
#include <sys/mman.h>
#include <stdio.h>
#include <memory.h>
#include <errno.h>
#include <unistd.h>
#include <xenomai/fusion.h>

static int __fusion_muxid;

static xnsysinfo_t __fusion_info;

static inline int __init_skin (void)

{
    __fusion_muxid = XENOMAI_SYSCALL2(__xn_sys_attach,FUSION_SKIN_MAGIC,&__fusion_info);
    return __fusion_muxid;
}

/* Public RTAI/fusion interface. */

int pthread_info_rt (xnsysinfo_t *infop)

{
    if (__fusion_muxid == 0 && __init_skin() < 0)
	return -1;

    memcpy(infop,&__fusion_info,sizeof(*infop));

    return 0;
}

int pthread_init_rt (const char *name, void *uhandle, void **khandlep)

{
    char stack[32768];

    if (__fusion_muxid == 0 && __init_skin() < 0)
	return -1;

    XENOMAI_SYSCALL1(__xn_sys_migrate,0);

    mlockall(MCL_CURRENT|MCL_FUTURE);
    memset(stack,0,sizeof(stack));

    /* Move the current thread into the RTAI realm. */
    return XENOMAI_SKINCALL3(__fusion_muxid,__xn_fusion_init,name,khandlep,uhandle);
}

int pthread_create_rt (const char *name,
		       void *uhandle,
		       pid_t syncpid,
		       int *syncp,
		       void **khandlep)
{
    char stack[32768];

    if (__fusion_muxid == 0 && __init_skin() < 0)
	return -1;

    XENOMAI_SYSCALL1(__xn_sys_migrate,0);

    mlockall(MCL_CURRENT|MCL_FUTURE);
    memset(stack,0,sizeof(stack));

    /* Move the current thread into the RTAI realm. */
    return XENOMAI_SKINCALL5(__fusion_muxid,__xn_fusion_create,name,khandlep,uhandle,syncpid,syncp);
}

int pthread_start_rt (void *khandle) {

    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_start,khandle);
}

int pthread_sync_rt (int *syncp)

{
    XENOMAI_SYSCALL1(__xn_sys_migrate,0);
    return XENOMAI_SYSCALL1(__xn_sys_sync,syncp);
}

int pthread_time_rt (unsigned long long *tp) {

    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_time,tp);
}

int pthread_inquire_rt (xninquiry_t *buf) {

    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_inquire,buf);
}

int pthread_migrate_rt (int domain) {

    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_migrate,domain);
}

/* Private RTAI/vm interface. */

int __pthread_idle_vm (int *lockp) {

    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_idle,lockp);
}

int __pthread_cancel_vm (void *deadhandle, void *nexthandle) {

    return XENOMAI_SKINCALL2(__fusion_muxid,__xn_fusion_cancel,deadhandle,nexthandle);
}

int __pthread_activate_vm (void *nexthandle, void *prevhandle) {

    return XENOMAI_SKINCALL2(__fusion_muxid,__xn_fusion_activate,nexthandle,prevhandle);
}

int __pthread_hold_vm (int *pendp) {

    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_hold,pendp);
}

int __pthread_release_vm (int *lockp) {

    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_release,lockp);
}
