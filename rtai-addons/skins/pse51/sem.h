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

#ifndef PSE51_SEM_H
#define PSE51_SEM_H

#include "pse51/thread.h"       /* For pse51_current_thread and
                                   pse51_thread_t definition. */

#define link2sem(laddr) \
((sem_t *)(((char *)laddr) - (int)(&((sem_t *)0)->link)))

#define synch2sem(saddr) \
((sem_t *)(((char *)saddr) - (int)(&((sem_t *)0)->synchbase)))


void pse51_sem_obj_init(void);

void pse51_sem_obj_cleanup(void);

#endif /*PSE51_SEM_H*/
