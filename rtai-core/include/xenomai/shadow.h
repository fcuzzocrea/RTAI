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

#ifndef _xenomai_shadow_h
#define _xenomai_shadow_h

#include <asm/rtai_xncall.h>

#define XNSHADOW_MAXRQ 16

#ifdef __cplusplus
extern "C" {
#endif

struct xnthread;
struct xnmutex;
struct pt_regs;
struct timespec;
struct timeval;

int xnshadow_init(void);

void xnshadow_cleanup(void);

void xnshadow_map(struct xnthread *thread,
		  const char *name,
		  int prio,
		  pid_t syncpid,
		  int *u_syncp,
		  unsigned magic,
		  struct xnmutex *imutex);

void xnshadow_unmap(struct xnthread *thread);

void xnshadow_relax(void);

void xnshadow_harden(struct xnmutex *imutex);

void xnshadow_renice(struct xnthread *thread);

void xnshadow_start(struct xnthread *thread,
		    u_long mode,
		    void (*uentry)(void *cookie),
		    void *ucookie,
		    int resched);

int xnshadow_register_skin(unsigned magic,
			   int nrcalls,
			   xnsysent_t *systab);

int xnshadow_unregister_skin(int muxid);

void xnshadow_exit(void);

static inline void xnshadow_schedule (void) {
    XENOMAI_SYSCALL0(__xn_sys_sched);
}

unsigned long long xnshadow_ts2ticks(const struct timespec *v);

void xnshadow_ticks2ts(unsigned long long ticks,
		       struct timespec *v);

unsigned long long xnshadow_tv2ticks(const struct timeval *v);

void xnshadow_ticks2tv(unsigned long long ticks,
		       struct timeval *v);

#ifdef __cplusplus
}
#endif

#endif /* !_xenomai_shadow_h */
