/*
 * Copyright (C) 2004 Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * RTAI/fusion is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RTAI/fusion; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _RTAI_NUCLEUS_LTT_H
#define _RTAI_NUCLEUS_LTT_H

#include <linux/config.h>

#if __KERNEL__ && CONFIG_LTT

#include <linux/ltt-core.h>

struct xnltt_evmap {

    char *ltt_label;	/* !< Event label (creation time). */
    char *ltt_format;	/* !< Event format (creation time). */
    int ltt_evid;	/* !< LTT custom event id. */
    int ltt_filter;	/* !< Event filter. */
};

#define rtai_ev_ienter       0
#define rtai_ev_iexit        1
#define rtai_ev_resched      2
#define rtai_ev_smpsched     3
#define rtai_ev_fastsched    4
#define rtai_ev_switch       5
#define rtai_ev_fault        6
#define rtai_ev_callout      7
#define rtai_ev_finalize     8
#define rtai_ev_thrinit      9
#define rtai_ev_thrstart     10
#define rtai_ev_threstart    11
#define rtai_ev_thrdelete    12
#define rtai_ev_thrsuspend   13
#define rtai_ev_thresume     14
#define rtai_ev_thrunblock   15
#define rtai_ev_threnice     16
#define rtai_ev_cpumigrate   17
#define rtai_ev_sigdispatch  18
#define rtai_ev_thrboot      19
#define rtai_ev_tmtick       20
#define rtai_ev_sleepon      21
#define rtai_ev_wakeup1      22
#define rtai_ev_wakeupx      23
#define rtai_ev_syncflush    24
#define rtai_ev_syncforget   25
#define rtai_ev_lohandler    26
#define rtai_ev_primarysw    27
#define rtai_ev_primary      28
#define rtai_ev_secondarysw  29
#define rtai_ev_secondary    30
#define rtai_ev_shadowmap    31
#define rtai_ev_shadowunmap  32
#define rtai_ev_shadowstart  33
#define rtai_ev_syscall      34
#define rtai_ev_shadowexit   35
#define rtai_ev_thrsetmode   36
#define rtai_ev_rdrotate     37
#define rtai_ev_rractivate   38
#define rtai_ev_rrdeactivate 39
#define rtai_ev_timeset      40
#define rtai_ev_addhook      41
#define rtai_ev_remhook      42
#define rtai_ev_thrperiodic  43
#define rtai_ev_thrwait      44
#define rtai_ev_tmstart      45
#define rtai_ev_tmstop       46
#define rtai_ev_mark         47
#define rtai_ev_watchdog     48

#define rtai_evthr  0x1
#define rtai_evirq  0x2
#define rtai_evsys  0x4
#define rtai_evall  0x7

#define XNLTT_MAX_EVENTS 64

extern struct xnltt_evmap xnltt_evtable[];

extern int xnltt_filter;

#define xnltt_log_event(ev, args...) \
do { \
  if (xnltt_evtable[ev].ltt_filter & xnltt_filter) \
    ltt_log_std_formatted_event(xnltt_evtable[ev].ltt_evid, ##args); \
} while(0)

static inline void xnltt_set_filter (int mask)
{
    xnltt_filter = mask;
}

static inline void xnltt_stop_tracing (void)
{
    xnltt_set_filter(0);
}

void xnltt_log_mark(const char *fmt,
		    ...);

int xnltt_mount(void);

void xnltt_umount(void);

#else /* !(__KERNEL__ && CONFIG_LTT) */

#define xnltt_log_event(ev, args...); /* Eat the semi-colon. */

static inline void xnltt_log_mark (const char *fmt, ...)
{
}

static inline void xnltt_set_filter (int mask)
{
}

static inline void xnltt_stop_tracing (void)
{
}

#endif /* __KERNEL__ && CONFIG_LTT */

#endif /* !_RTAI_NUCLEUS_LTT_H_ */
