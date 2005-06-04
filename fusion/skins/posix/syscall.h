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
#define __pse51_sem_init              6
#define __pse51_sem_destroy           7
#define __pse51_sem_post              8
#define __pse51_sem_wait              9
#define __pse51_clock_gettime         10
#define __pse51_clock_settime         11
#define __pse51_clock_nanosleep       12

#ifdef __KERNEL__

#ifdef __cplusplus
extern "C" {
#endif

int __pse51_syscall_init(void);

void __pse51_syscall_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ */

#endif /* _POSIX_SYSCALL_H */
