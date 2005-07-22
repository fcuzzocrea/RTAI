/**
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org> 
 * @note Copyright (C) 2005 Nextream France S.A.
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

#ifndef _COMPAT_SEM_H
#define _COMPAT_SEM_H

#include <compat/types.h>

typedef struct rt_sem_placeholder {
    rt_handle_t opaque;
} RT_SEM_PLACEHOLDER;

#if defined(__KERNEL__) || defined(__RTAI_SIM__)

#define COMPAT_SEM_MAGIC 0x17170202

#define PRIO_Q   0x0
#define RES_Q    0x3
#define FIFO_Q   0x4

#define BIN_SEM  0x1
#define CNT_SEM  0x2
#define RES_SEM  0x3

#define SEM_TIMOUT  0xfffe
#define SEM_ERR     0xffff

struct rt_task_struct;

typedef struct rt_semaphore {

    unsigned magic;		/* !< Magic code - must be first */

    xnsynch_t synch_base;	/* !< Base synchronization object. */

    int count;			/* !< Current semaphore value. */

    int type;			/* !< Semaphore type. */

    struct rt_task_struct *owner; /* !< Current owner of a resource sem. */
} SEM;

#ifdef __cplusplus
extern "C" {
#endif

int __sem_pkg_init(void);

void __sem_pkg_cleanup(void);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __RTAI_SIM__) */

typedef RT_SEM_PLACEHOLDER SEM;

#endif /* __KERNEL__ || __RTAI_SIM__ */

#ifdef __cplusplus
extern "C" {
#endif

void rt_typed_sem_init(SEM *sem,
		       int value,
		       int type);

int rt_sem_delete(SEM *sem);

int rt_sem_signal(SEM *sem);

int rt_sem_wait(SEM *sem);

int rt_sem_wait_if(SEM *sem);

#ifdef __cplusplus
}
#endif

#endif /* !_COMPAT_SEM_H */
