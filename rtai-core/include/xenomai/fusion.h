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

#ifndef _xenomai_fusion_h
#define _xenomai_fusion_h

#include <asm/rtai_xncall.h>

#define FUSION_SKIN_MAGIC      0x504f5358

#define __xn_fusion_init     0
#define __xn_fusion_create   1
#define __xn_fusion_start    2
#define __xn_fusion_migrate  3
#define __xn_fusion_time     4
#define __xn_fusion_inquire  5
#define __xn_fusion_idle     6
#define __xn_fusion_cancel   7
#define __xn_fusion_activate 8
#define __xn_fusion_hold     9
#define __xn_fusion_release  10

#define FUSION_RTAI_DOMAIN   0
#define FUSION_LINUX_DOMAIN  1

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef __KERNEL__

#include <sys/types.h>
#include <pthread.h>

    /* Public RTAI/fusion interface. */

int pthread_info_rt(xnsysinfo_t *infop);

int pthread_init_rt(const char *name,
		    void *uhandle,
		    void **khandlep);

int pthread_create_rt(const char *name,
		      void *uhandle,
		      pid_t syncpid,
		      int *syncp,
		      void **khandlep);

int pthread_start_rt(void *khandle);

int pthread_sync_rt(int *syncp);

int pthread_migrate_rt(int domain);

int pthread_time_rt(unsigned long long *tp);

int pthread_inquire_rt(xninquiry_t *infop);

    /* The following routines are private to the RTAI/vm control layer
       based on the RTAI/fusion interface. Do not use them in
       applications. */

int __pthread_idle_vm(int *lockp);

int __pthread_cancel_vm(void *deadhandle,
			void *nexthandle);

int __pthread_activate_vm(void *nexthandle,
			  void *prevhandle);

int __pthread_hold_vm(int *pendp);

int __pthread_release_vm(int *lockp);

#endif /* !__KERNEL__ */

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* !_xenomai_fusion_h */
