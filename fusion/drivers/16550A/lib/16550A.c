/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <unistd.h>
#include <16550A/16550A.h>
#include <16550A/syscall.h>

extern int __16550A_muxid;

int __init_skin(void);

/* Public 16550A interface. */

int rt_uart_open (RT_UART *uart,
		  RT_UART_CONFIG *config)
{
    if (__16550A_muxid < 0 && __init_skin() < 0)
	return -ENOSYS;

    return XENOMAI_SKINCALL2(__16550A_muxid,
			     __rtai_uart_open,
			     uart,
			     config);
}

int rt_uart_close (RT_UART *uart)

{
    return XENOMAI_SKINCALL1(__16550A_muxid,
			     __rtai_uart_close,
			     uart);
}

ssize_t rt_uart_read (RT_UART *uart,
		      void *buf,
		      size_t nbytes,
		      RTIME timeout)
{
    return (ssize_t)XENOMAI_SKINCALL4(__16550A_muxid,
				      __rtai_uart_read,
				      uart,
				      buf,
				      nbytes,
				      &timeout);
}

ssize_t rt_uart_write (RT_UART *uart,
		       const void *buf,
		       size_t nbytes)
{
    return (ssize_t)XENOMAI_SKINCALL3(__16550A_muxid,
				      __rtai_uart_write,
				      uart,
				      buf,
				      nbytes);
}

int rt_uart_control (RT_UART *uart,
		     int cmd,
		     void *arg)
{
    return XENOMAI_SKINCALL3(__16550A_muxid,
			     __rtai_uart_control,
			     uart,
			     cmd,
			     arg);
}
