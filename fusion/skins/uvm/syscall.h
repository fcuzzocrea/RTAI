/*
 * Copyright (C) 2001-2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * RTAI/fusion is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RTAI/fusion; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _UVM_SYSCALL_H
#define _UVM_SYSCALL_H

#include <nucleus/asm/syscall.h>

#define UVM_SKIN_MAGIC    0x53554d53

#define __uvm_thread_shadow       0
#define __uvm_thread_create       1
#define __uvm_thread_start        2
#define __uvm_thread_set_periodic 3
#define __uvm_thread_wait_period  4
#define __uvm_thread_idle         5
#define __uvm_thread_cancel       6
#define __uvm_thread_activate     7
#define __uvm_thread_hold         8
#define __uvm_thread_release      9
#define __uvm_timer_read          10
#define __uvm_timer_tsc           11
#define __uvm_timer_start         12
#define __uvm_timer_stop          13

#ifdef __KERNEL__

int __uvm_syscall_init(void);

void __uvm_syscall_cleanup(void);

#endif /* __KERNEL__ */

#endif /* !_UVM_SYSCALL_H */
