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

#ifndef _psos_task_h
#define _psos_task_h

#include "psos+/event.h"

#define PSOSTASK_NOTEPAD_REGS 16

#define PSOS_TASK_MAGIC 0x81810101

typedef struct psostask {

    unsigned magic;   /* Magic code - must be first */

    xnholder_t link;	/* Link in psostaskq */

#define link2psostask(laddr) \
((psostask_t *)(((char *)laddr) - (int)(&((psostask_t *)0)->link)))

    xnthread_t threadbase;

#define thread2psostask(taddr) \
((taddr) ? ((psostask_t *)(((char *)(taddr)) - (int)(&((psostask_t *)0)->threadbase))) : NULL)

    void (*entry)(u_long,u_long,u_long,u_long);

    u_long args[4];

    u_long notepad[PSOSTASK_NOTEPAD_REGS];

    psosevent_t evgroup; /* Event flags group */

    xngqueue_t alarmq;	/* List of outstanding alarms */

    union { /* Saved args for current synch. wait operation */

	struct {
	    u_long flags;
	    u_long events;
	} evgroup;

	struct psosmbuf *qmsg;

	struct {
	    u_long size;
	    void *chunk;
	} region;

    } waitargs;

} psostask_t;

#define psos_current_task() thread2psostask(xnpod_current_thread())

#ifdef __cplusplus
extern "C" {
#endif

static inline xnflags_t psos_mode_to_xeno (u_long mode) {

    xnflags_t xnmode = 0;

    if (mode & T_NOPREEMPT)
	xnmode |= XNLOCK;

    if (mode & T_TSLICE)
	xnmode |= XNRRB;

    if (mode & T_NOASR)
	xnmode |= XNASDI;

    return xnmode;
}

static inline u_long xeno_mode_to_psos (xnflags_t xnmode) {

    u_long mode = 0;

    if (xnmode & XNLOCK)
	mode |= T_NOPREEMPT;

    if (xnmode & XNRRB)
	mode |= T_TSLICE;

    if (xnmode & XNASDI)
	mode |= T_NOASR;

    return mode;
}

void psostask_init(u_long rrperiod);

void psostask_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* !_psos_task_h */
