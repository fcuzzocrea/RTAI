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

#ifndef pse51_internal_h
#define pse51_internal_h

#include "xenomai/xenomai.h"
#include "rtai_config.h"
#include "pse51/rtai_pse51.h"

extern xnmutex_t __imutex;

#define PSE51_MAGIC(n) (0x8686##n##n)
#define PSE51_ANY_MAGIC         PSE51_MAGIC(00)
#define PSE51_THREAD_MAGIC      PSE51_MAGIC(01)
#define PSE51_THREAD_ATTR_MAGIC PSE51_MAGIC(02)
#define PSE51_MUTEX_MAGIC       PSE51_MAGIC(03)
#define PSE51_MUTEX_ATTR_MAGIC  PSE51_MAGIC(04)
#define PSE51_COND_MAGIC        PSE51_MAGIC(05)
#define PSE51_COND_ATTR_MAGIC   PSE51_MAGIC(05)
#define PSE51_SEM_MAGIC         PSE51_MAGIC(06)
#define PSE51_KEY_MAGIC         PSE51_MAGIC(07)
#define PSE51_ONCE_MAGIC        PSE51_MAGIC(08)

#define PSE51_MIN_PRIORITY      0
#define PSE51_MAX_PRIORITY      255

#define pse51_obj_active(h,m,t) \
((h) && ((t *)(h))->magic == (m))

#define pse51_obj_deleted(h,m,t) \
((h) && ((t *)(h))->magic == ~(m))

#define pse51_obj_busy(h) ((h) && \
(((*((unsigned *)(h)) & 0xffff0000) == PSE51_ANY_MAGIC)))

#define pse51_mark_deleted(t) ((t)->magic = ~(t)->magic)

#define ticks2timespec(timespec, ticks)                         \
do {                                                            \
    (timespec)->tv_nsec = xnpod_ticks2time(ticks);              \
    (timespec)->tv_sec = (timespec)->tv_nsec / 1000000000;      \
    (timespec)->tv_nsec %= 1000000000;                          \
} while(0)

#define timespec2ticks(timespec) \
(xnpod_time2ticks((timespec)->tv_sec*1000000000+(timespec)->tv_nsec))

#endif /* !pse51_internal_h */
