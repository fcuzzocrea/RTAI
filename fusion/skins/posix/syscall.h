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
#define __pse51_thread_set_name       7
#define __pse51_sem_init              8
#define __pse51_sem_destroy           9
#define __pse51_sem_post              10
#define __pse51_sem_wait              11
#define __pse51_sem_trywait           12
#define __pse51_sem_getvalue          13
#define __pse51_clock_getres          14
#define __pse51_clock_gettime         15
#define __pse51_clock_settime         16
#define __pse51_clock_nanosleep       17
#define __pse51_mutex_init            18
#define __pse51_mutex_destroy         19
#define __pse51_mutex_lock            20
#define __pse51_mutex_timedlock       21
#define __pse51_mutex_trylock         22
#define __pse51_mutex_unlock          23
#define __pse51_cond_init             24
#define __pse51_cond_destroy          25
#define __pse51_cond_wait             26
#define __pse51_cond_timedwait        27
#define __pse51_cond_signal           28
#define __pse51_cond_broadcast        29
#define __pse51_mq_open               30
#define __pse51_mq_close              31
#define __pse51_mq_unlink             32
#define __pse51_mq_getattr            33
#define __pse51_mq_setattr            34
#define __pse51_mq_send               35
#define __pse51_mq_timedsend          36
#define __pse51_mq_receive            37
#define __pse51_mq_timedreceive       38
#define __pse51_intr_attach           39
#define __pse51_intr_detach           40
#define __pse51_intr_wait             41
#define __pse51_intr_control          42
#define __pse51_timer_create          43
#define __pse51_timer_delete          44
#define __pse51_timer_settime         45
#define __pse51_timer_gettime         46
#define __pse51_timer_getoverrun      47

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
