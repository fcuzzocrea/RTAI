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

#ifndef _RTAI_NUCLEUS_SHADOW_H
#define _RTAI_NUCLEUS_SHADOW_H

#include <nucleus/asm/syscall.h>
#include <nucleus/asm/atomic.h>

#define XENOMAI_MUX_NR 16

#define XNSHADOW_MAXRQ 16

/* Events sent to the interface callback */
#define XNSHADOW_CLIENT_ATTACH  0
#define XNSHADOW_CLIENT_DETACH  1

#ifdef __cplusplus
extern "C" {
#endif

struct xnthread;
struct xnmutex;
struct pt_regs;
struct timespec;
struct timeval;

struct xnskentry {

    const char *name;
    unsigned magic;
    int nrcalls;
    atomic_counter_t refcnt;
    int (*eventcb)(int);
    xnsysent_t *systab;
};

int xnshadow_mount(void);

void xnshadow_cleanup(void);

void xnshadow_grab_events(void);

void xnshadow_release_events(void);

void xnshadow_map(struct xnthread *thread,
		  pid_t syncpid,
		  int __user *u_syncp);

void xnshadow_unmap(struct xnthread *thread);

void xnshadow_relax(void);

void xnshadow_harden(void);

void xnshadow_renice(struct xnthread *thread);

int xnshadow_wait_barrier(struct pt_regs *regs);

void xnshadow_start(struct xnthread *thread,
		    void (*uentry)(void *cookie),
		    void *ucookie);

void xnshadow_sync_post(pid_t syncpid,
			int *u_syncp,
			int err);

void xnshadow_exit(void);

int xnshadow_register_skin(const char *name,
			   unsigned magic,
			   int nrcalls,
			   xnsysent_t *systab,
			   int (*eventcb)(int event));

int xnshadow_unregister_skin(int muxid);

unsigned long long xnshadow_ts2ticks(const struct timespec *v);

void xnshadow_ticks2ts(unsigned long long,
		       struct timespec *v);

unsigned long long xnshadow_tv2ticks(const struct timeval *v);

void xnshadow_ticks2tv(unsigned long long ticks,
		       struct timeval *v);

extern struct xnskentry muxtable[];

#ifdef __cplusplus
}
#endif

#endif /* !_RTAI_NUCLEUS_SHADOW_H */
