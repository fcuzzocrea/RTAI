/**
 * @file
 * This file is part of the RTAI project.
 *
 * @note Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org> 
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

#include <16550A/16550A.h>
#include <16550A/syscall.h>

static int __muxid;

/*
 * int __rt_uart_open(RT_UART_PLACEHOLDER *ph,
 *                    unsigned irq,
 *                    int mode)
 */

int __rt_uart_open (struct task_struct *curr, struct pt_regs *regs)

{
    return 0;
}

/*
 * int __rt_uart_close(RT_UART_PLACEHOLDER *ph)
 */

int __rt_uart_close (struct task_struct *curr, struct pt_regs *regs)

{
    return 0;
}

/*
 * int __rt_uart_read(RT_UART_PLACEHOLDER *ph,
 *                    void *buf,
 *                    size_t nbytes,
 *                    RTIME timeout)
 */

int __rt_uart_read (struct task_struct *curr, struct pt_regs *regs)

{
    return 0;
}

/*
 * int __rt_uart_write(RT_UART_PLACEHOLDER *ph,
 *                     const void *buf,
 *                     size_t nbytes)
 */

int __rt_uart_write (struct task_struct *curr, struct pt_regs *regs)

{
    return 0;
}

/*
 * int __rt_uart_control(RT_UART_PLACEHOLDER *ph,
 *                       int cmd,
 *                       void *arg)
 */

int __rt_uart_control (struct task_struct *curr, struct pt_regs *regs)

{
    return 0;
}

static xnsysent_t __systab[] = {
    [__rtai_uart_open ] = { &__rt_uart_open, __xn_flag_anycall },
    [__rtai_uart_close ] = { &__rt_uart_close, __xn_flag_anycall },
    [__rtai_uart_read ] = { &__rt_uart_read, __xn_flag_regular },
    [__rtai_uart_write ] = { &__rt_uart_write, __xn_flag_regular },
    [__rtai_uart_control ] = { &__rt_uart_control, __xn_flag_regular },
};

int __uart_syscall_init (void)

{
    __muxid = xnshadow_register_interface("16550A",
					  RTAI_UART_MAGIC,
					  sizeof(__systab) / sizeof(__systab[0]),
					  __systab,
					  NULL);
    return __muxid < 0 ? -ENOSYS : 0;
}

void __uart_syscall_cleanup (void)

{
    xnshadow_unregister_interface(__muxid);
}
