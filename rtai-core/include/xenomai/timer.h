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

#ifndef _xenomai_timer_h
#define _xenomai_timer_h

#include "xenomai/queue.h"

#define XNPOD_APERIODIC_TICK       0

/* Number of outstanding timers (hint only) -- must be ^2 */
#define XNTIMER_WHEELSIZE 128
#define XNTIMER_WHEELMASK (XNTIMER_WHEELSIZE - 1)

#define XNTIMER_ENABLED   0x00000001
#define XNTIMER_DEQUEUED  0x00000002
#define XNTIMER_KILLED    0x00000004

/* These flags are available to the real-time interfaces */
#define XNTIMER_SPARE0  0x01000000
#define XNTIMER_SPARE1  0x02000000
#define XNTIMER_SPARE2  0x04000000
#define XNTIMER_SPARE3  0x08000000
#define XNTIMER_SPARE4  0x10000000
#define XNTIMER_SPARE5  0x20000000
#define XNTIMER_SPARE6  0x40000000
#define XNTIMER_SPARE7  0x80000000

typedef struct xntimer {

    xnholder_t link;

#define link2timer(laddr) \
((xntimer_t *)(((char *)laddr) - (int)(&((xntimer_t *)0)->link)))

    xnflags_t status;

    xnticks_t date;		/* Timeout date (in ticks) */

    xnticks_t interval;		/* Periodic interval (in ticks, 0 == one shot) */

    void (*handler)(void *cookie); /* Timeout handler */

    void *cookie;	/* Cookie to pass to the timeout handler */

    XNARCH_DECL_DISPLAY_CONTEXT();

} xntimer_t;

#define xntimer_date(t)           ((t)->date)
#define xntimer_interval(t)       ((t)->interval)
#define xntimer_set_cookie(t,c)   ((t)->cookie = (c))

static inline int xntimer_active_p (xntimer_t *timer) {
    return !testbits(timer->status,XNTIMER_DEQUEUED);
}

#ifdef __cplusplus
extern "C" {
#endif

void xntimer_init(xntimer_t *timer,
		  void (*handler)(void *cookie),
		  void *cookie);

void xntimer_destroy(xntimer_t *timer);

void xntimer_do_timers(int incr);

int xntimer_start(xntimer_t *timer,
		  xnticks_t value,
		  xnticks_t interval);

void xntimer_stop(xntimer_t *timer);

xnticks_t xntimer_get_date(xntimer_t *timer);

xnticks_t xntimer_get_timeout(xntimer_t *timer);

void xntimer_freeze(void);

#ifdef __cplusplus
}
#endif

#endif /* !_xenomai_timer_h */
