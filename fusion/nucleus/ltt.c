/*
 * Copyright (C) 2004 Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA
 * 02139, USA; either version 2 of the License, or (at your option)
 * any later version.
 *
 * RTAI/fusion is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <stdarg.h>
#include <nucleus/ltt.h>

int __init xnltt_mount (void)

{
    int ev, evid;

    /* Create all custom LTT events we need. */

    for (ev = 0; xnltt_evtable[ev].ltt_label != NULL; ev++)
        {
	evid = ltt_create_event(xnltt_evtable[ev].ltt_label,
				xnltt_evtable[ev].ltt_format,
				LTT_CUSTOM_EV_FORMAT_TYPE_STR,
				NULL);
	if (evid < 0)
	    {
	    while (--ev >= 0)
	        {
		xnltt_evtable[ev].ltt_evid = -1;
		ltt_destroy_event(xnltt_evtable[ev].ltt_evid);
		}

	    return evid;
	    }

	xnltt_evtable[ev].ltt_evid = evid;
	}

    return 0;
}

void __exit xnltt_umount (void)

{
    int ev;

    for (ev = 0; xnltt_evtable[ev].ltt_evid != -1; ev++)
	ltt_destroy_event(xnltt_evtable[ev].ltt_evid);
}

struct xnltt_evmap xnltt_evtable[] = {

    [rtai_ev_ienter] = { "RTAI i-enter", "IRQ=%d", -1 },
    [rtai_ev_iexit] = { "RTAI i-exit", "IRQ=%d", -1 },
};

EXPORT_SYMBOL(xnltt_evtable);
