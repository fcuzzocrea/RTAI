/*
 * Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _RTAI_SYSCALL_H
#define _RTAI_SYSCALL_H

#if !__RTAI_SIM__
#include <nucleus/syscall.h>
#endif /* __RTAI_SIM__ */

#define __rtai_task_create        0
#define __rtai_task_bind          1
#define __rtai_task_start         2
#define __rtai_task_suspend       3
#define __rtai_task_resume        4
#define __rtai_task_delete        5
#define __rtai_task_yield         6
#define __rtai_task_set_periodic  7
#define __rtai_task_wait_period   8
#define __rtai_task_set_priority  9
#define __rtai_task_sleep         10
#define __rtai_task_sleep_until   11
#define __rtai_task_unblock       12
#define __rtai_task_inquire       13
#define __rtai_task_notify        14
#define __rtai_task_set_mode      15
#define __rtai_task_self          16
#define __rtai_task_slice         17
#define __rtai_timer_start        18
#define __rtai_timer_stop         19
#define __rtai_timer_read         20
#define __rtai_timer_tsc          21
#define __rtai_timer_ns2ticks     22
#define __rtai_timer_ticks2ns     23
#define __rtai_timer_inquire      24
#define __rtai_sem_create         25
#define __rtai_sem_bind           26
#define __rtai_sem_delete         27
#define __rtai_sem_p              28
#define __rtai_sem_v              29
#define __rtai_sem_broadcast      30
#define __rtai_sem_inquire        31
#define __rtai_event_create       32
#define __rtai_event_bind         33
#define __rtai_event_delete       34
#define __rtai_event_wait         35
#define __rtai_event_signal       36
#define __rtai_event_clear        37
#define __rtai_event_inquire      38
#define __rtai_mutex_create       39
#define __rtai_mutex_bind         40
#define __rtai_mutex_delete       41
#define __rtai_mutex_lock         42
#define __rtai_mutex_unlock       43
#define __rtai_mutex_inquire      44
#define __rtai_cond_create        45
#define __rtai_cond_bind          46
#define __rtai_cond_delete        47
#define __rtai_cond_wait          48
#define __rtai_cond_signal        49
#define __rtai_cond_broadcast     50
#define __rtai_cond_inquire       51
#define __rtai_queue_create       52
#define __rtai_queue_bind         53
#define __rtai_queue_delete       54
#define __rtai_queue_alloc        55
#define __rtai_queue_free         56
#define __rtai_queue_send         57
#define __rtai_queue_recv         58
#define __rtai_queue_inquire      59
#define __rtai_heap_create        60
#define __rtai_heap_bind          61
#define __rtai_heap_delete        62
#define __rtai_heap_alloc         63
#define __rtai_heap_free          64
#define __rtai_heap_inquire       65
#define __rtai_alarm_create       66
#define __rtai_alarm_delete       67
#define __rtai_alarm_start        68
#define __rtai_alarm_stop         69
#define __rtai_alarm_wait         70
#define __rtai_alarm_inquire      71
#define __rtai_intr_create        72
#define __rtai_intr_bind          73
#define __rtai_intr_delete        74
#define __rtai_intr_wait          75
#define __rtai_intr_enable        76
#define __rtai_intr_disable       77
#define __rtai_intr_inquire       78
#define __rtai_misc_get_io_region 79
#define __rtai_misc_put_io_region 80

struct rt_arg_bulk {

    u_long a1;
    u_long a2;
    u_long a3;
    u_long a4;
    u_long a5;
};

#if __KERNEL__

#ifdef __cplusplus
extern "C" {
#endif

int __rtai_syscall_init(void);

void __rtai_syscall_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ */

#endif /* _RTAI_SYSCALL_H */
