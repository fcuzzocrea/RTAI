/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 * USA; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <xenomai/pod.h>
#include <xenomai/fusion.h>
#include <xenomai/dbridge.h>

/* Not finished yet. Unused. */

extern xnqueue_t dbridge_sleepq;

extern xnqueue_t dbridge_asyncq;

extern spinlock_t dbridge_sqlock;

extern spinlock_t dbridge_aqlock;

dbridge_ops_t dbridge_ev_ops = {
    NULL,NULL,NULL,NULL,NULL,NULL,NULL
};

int linux_ev_init (void)

{
    dbridge_state_t *state;

    for (state = &dbridge_states[DBRIDGE_MQ_NDEVS];
	 state < &dbridge_states[DBRIDGE_MQ_NDEVS + DBRIDGE_EV_NDEVS]; state++)
	{
	state->u.ev.events = 0;
	state->ops = &dbridge_ev_ops;
	}

    return 0;
}

void linux_ev_exit (void) {
}
