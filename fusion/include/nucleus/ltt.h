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

#define rtai_ev_ienter  0
#define rtai_ev_iexit   1

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
