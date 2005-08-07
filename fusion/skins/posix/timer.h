/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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

#ifndef _POSIX_TIMER_H
#define _POSIX_TIMER_H

#include <nucleus/timer.h>
#include <posix/posix.h>        /* For struct itimerspec. */

#define PSE51_TIMER_MAX  128

struct pse51_timer {

    xntimer_t timerbase;

    struct itimerspec value;

    int overruns;

    xnholder_t link;

#define link2tm(laddr) \
((struct pse51_timer *)(((char *)laddr) - (int)(&((struct pse51_timer *)0)->link)))
};

int pse51_timer_pkg_init(void);

void pse51_timer_pkg_cleanup(void);

#endif /* !_POSIX_TIMER_H */
