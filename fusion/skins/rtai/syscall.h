/*
 * Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * As a special exception, the RTAI project gives permission
 * for additional uses of the text contained in its release of
 * Xenomai.
 *
 * The exception is that, if you link the Xenomai libraries with other
 * files to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the Xenomai libraries code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public
 * License.
 *
 * This exception applies only to the code released by the
 * RTAI project under the name Xenomai.  If you copy code from other
 * RTAI project releases into a copy of Xenomai, as the General Public
 * License permits, the exception does not apply to the code that you
 * add in this way.  To avoid misleading anyone as to the status of
 * such modified files, you must delete this exception notice from
 * them.
 *
 * If you write modifications of your own for Xenomai, it is your
 * choice whether to permit this exception to apply to your
 * modifications. If you do not wish that, delete this exception
 * notice.
 */

#ifndef _RTAI_SYSCALL_H
#define _RTAI_SYSCALL_H

#ifndef __RTAI_SIM__
#include <nucleus/asm/syscall.h>
#endif /* __RTAI_SIM__ */

#define __rtai_task_create        0
#define __rtai_task_bind          1
#define __rtai_task_start         2
#define __rtai_task_set_periodic  3
#define __rtai_task_wait_period   4
#define __rtai_task_sleep         5
#define __rtai_timer_start        6
#define __rtai_timer_stop         7
#define __rtai_timer_read         8
#define __rtai_timer_tsc          9
#define __rtai_timer_ns2ticks     10
#define __rtai_timer_ticks2ns     11
#define __rtai_sem_create         12
#define __rtai_sem_bind           13
#define __rtai_sem_delete         14
#define __rtai_sem_p              15
#define __rtai_sem_v              16
#define __rtai_sem_inquire        17

struct rt_arg_bulk {

    u_long a1;
    u_long a2;
    u_long a3;
    u_long a4;
    u_long a5;
};

#ifdef __KERNEL__

#ifdef __cplusplus
extern "C" {
#endif

int __syscall_pkg_init(void);

void __syscall_pkg_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ */

#endif /* _RTAI_SYSCALL_H */
