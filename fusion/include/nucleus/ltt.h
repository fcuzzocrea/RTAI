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

#if defined(__KERNEL__) && defined(CONFIG_LTT)

#include <linux/ltt-core.h>

struct xnltt_evmap {

    char *ltt_label;	/* !< Event label (creation time). */
    char *ltt_format;	/* !< Event format (creation time). */
    int ltt_evid;	/* !< LTT custom event id. */
};

#define rtai_ev_ienter      0
#define rtai_ev_iexit       1
#define rtai_ev_resched     2
#define rtai_ev_smpsched    3
#define rtai_ev_fastsched   4
#define rtai_ev_switch      5
#define rtai_ev_fault       6
#define rtai_ev_callout     7
#define rtai_ev_finalize    8
#define rtai_ev_thrinit     9
#define rtai_ev_thrstart    10
#define rtai_ev_threstart   11
#define rtai_ev_thrdelete   12
#define rtai_ev_thrsuspend  13
#define rtai_ev_thresume    14
#define rtai_ev_thrunblock  15
#define rtai_ev_threnice    16
#define rtai_ev_cpumigrate  17
#define rtai_ev_sigdispatch 18
#define rtai_ev_thrboot     19
#define rtai_ev_tmtick      20
#define rtai_ev_sleepon     21
#define rtai_ev_wakeup1     22
#define rtai_ev_wakeupx     23
#define rtai_ev_syncflush   24
#define rtai_ev_syncforget  25
#define rtai_ev_lohandler   26
#define rtai_ev_primarysw   27
#define rtai_ev_primary     28
#define rtai_ev_secondarysw 29
#define rtai_ev_secondary   30
#define rtai_ev_shadowmap   31
#define rtai_ev_shadowunmap 32
#define rtai_ev_shadowstart 33
#define rtai_ev_syscall     34
#define rtai_ev_shadowexit  35

#define XNLTT_MAX_EVENTS 64

#define xnltt_log_event(ev, args...) \
ltt_log_std_formatted_event(xnltt_evtable[ev].ltt_evid, ##args)

int xnltt_mount(void);

void xnltt_umount(void);

extern struct xnltt_evmap xnltt_evtable[];

#else /* !(__KERNEL__ && CONFIG_LTT) */

#define xnltt_log_event(ev, args...); /* Eat the semi-colon. */

#endif /* __KERNEL__ && CONFIG_LTT */

#endif /* !_RTAI_NUCLEUS_LTT_H_ */
