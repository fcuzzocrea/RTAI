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

#ifndef _16550A_SYSCALL_H
#define _16550A_SYSCALL_H

#ifndef __RTAI_SIM__
#include <nucleus/syscall.h>
#endif /* __RTAI_SIM__ */

#define __rtai_uart_open     0
#define __rtai_uart_close    1
#define __rtai_uart_read     2
#define __rtai_uart_write    3
#define __rtai_uart_control  4

#ifdef __KERNEL__

#ifdef __cplusplus
extern "C" {
#endif

int __uart_syscall_init(void);

void __uart_syscall_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ */

#endif /* _16550A_SYSCALL_H */
