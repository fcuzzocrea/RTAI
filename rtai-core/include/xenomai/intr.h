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

#ifndef _xenomai_intr_h
#define _xenomai_intr_h

#include "xenomai/thread.h"

#define XN_ISR_HANDLED   0x0
#define XN_ISR_CHAINED   0x1
#define XN_ISR_SCHED_T   0x2
#define XN_ISR_ENABLE    0x4

#define XNINTR_ATTACHED  0x2

#define XNINTR_MAX_PRIORITY 15

struct xnintr;

typedef int (*xnisr_t)(struct xnintr *intr);

typedef void (*xnist_t)(struct xnintr *intr,
			int hits);

typedef struct xnintr {

    xnflags_t status;	/*!< Status bitmask. */

    unsigned irq;	/* IRQ number */

    int priority;	/* Service thread priority */

    xnisr_t isr;	/* Interrupt service routine */

    xnist_t ist;	/* Interrupt service task */

    int pending;	/* Pending IRQ hits */

    void *cookie;	/* User-defined cookie value */

    xnthread_t svcthread; /* Service thread */

} xnintr_t;

extern xnintr_t nkclock;

#ifdef __cplusplus
extern "C" {
#endif

int xnintr_init(xnintr_t *intr,
		unsigned irq,
		int priority,
		xnisr_t isr,
		xnist_t ist,
		xnflags_t flags);

int xnintr_destroy(xnintr_t *intr);

int xnintr_attach(xnintr_t *intr,
		  void *cookie);

int xnintr_detach(xnintr_t *intr);

void xnintr_enable(xnintr_t *intr);

void xnintr_disable(xnintr_t *intr);
    
#ifdef __cplusplus
}
#endif

#endif /* !_xenomai_intr_h */
