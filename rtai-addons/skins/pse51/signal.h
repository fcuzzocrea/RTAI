/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
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


#ifndef PSE51_SIGNAL_H
#define PSE51_SIGNAL_H

#include "pse51/thread.h"

#define SIGACTION_FLAGS (SA_ONESHOT|SA_NOMASK)

#define PSE51_SIGMIN  1       /* 0 has a special meaning for pthread_kill. */
#define PSE51_SIGMAX  ((int) (sizeof(pse51_sigset_t)*8))

void pse51_signal_init_thread(pthread_t new, const pthread_t parent);

void pse51_signal_init(void);

#endif /*PSE51_SIGNAL_H*/
