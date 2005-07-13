/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * RTAI/fusion is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RTAI/fusion; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _POSIX_SYSCALL_H
#define _POSIX_SYSCALL_H

#ifndef __RTAI_SIM__
#include <nucleus/asm/syscall.h>
#endif /* __RTAI_SIM__ */

#define __pse51_thread_create         0
#define __pse51_thread_detach         1
#define __pse51_thread_setschedparam  2
#define __pse51_sched_yield           3
#define __pse51_thread_make_periodic  4
#define __pse51_thread_wait           5
#define __pse51_thread_set_mode       6
#define __pse51_sem_init              7
#define __pse51_sem_destroy           8
#define __pse51_sem_post              9
#define __pse51_sem_wait              10
#define __pse51_sem_trywait           11
#define __pse51_sem_getvalue          12
#define __pse51_clock_getres          13
#define __pse51_clock_gettime         14
#define __pse51_clock_settime         15
#define __pse51_clock_nanosleep       16
#define __pse51_mutex_init            17
#define __pse51_mutex_destroy         18
#define __pse51_mutex_lock            19
#define __pse51_mutex_timedlock       20
#define __pse51_mutex_trylock         21
#define __pse51_mutex_unlock          22
#define __pse51_cond_init             23
#define __pse51_cond_destroy          24
#define __pse51_cond_wait             25
#define __pse51_cond_timedwait        26
#define __pse51_cond_signal           27
#define __pse51_cond_broadcast        28
#define __pse51_mq_open               29
#define __pse51_mq_close              30
#define __pse51_mq_unlink             31
#define __pse51_mq_getattr            32
#define __pse51_mq_setattr            33
#define __pse51_mq_send               34
#define __pse51_mq_timedsend          35
#define __pse51_mq_receive            36
#define __pse51_mq_timedreceive       37

#ifdef __KERNEL__

#ifdef __cplusplus
extern "C" {
#endif

int pse51_syscall_init(void);

void pse51_syscall_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ */

#endif /* _POSIX_SYSCALL_H */
