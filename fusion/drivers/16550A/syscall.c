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

#include <nucleus/heap.h>
#include <rtai/registry.h>
#include <16550A/16550A.h>
#include <16550A/syscall.h>

static int __muxid;

/*
 * int __rt_uart_open(RT_UART_PLACEHOLDER *ph,
 *                    RT_UART_CONFIG *config)
 */

int __rt_uart_open (struct task_struct *curr, struct pt_regs *regs)

{
    RT_UART_PLACEHOLDER ph;
    RT_UART_CONFIG config;
    RT_UART *uart;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(config)))
	return -EFAULT;

    __xn_copy_from_user(curr,&config,(void __user *)__xn_reg_arg2(regs),sizeof(config));

    uart = (RT_UART *)xnmalloc(sizeof(*uart));

    if (!uart)
	return -ENOMEM;

    err = rt_uart_open(uart,&config);

    if (err == 0)
	{
	uart->source = RT_UAPI_SOURCE;
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = uart->handle;
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));
	}
    else
	xnfree(uart);

    return err;
}

/*
 * int __rt_uart_close(RT_UART_PLACEHOLDER *ph)
 */

int __rt_uart_close (struct task_struct *curr, struct pt_regs *regs)

{
    RT_UART_PLACEHOLDER ph;
    RT_UART *uart;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    uart = (RT_UART *)rt_registry_fetch(ph.opaque);

    if (!uart)
	return -ESRCH;

    err = rt_uart_close(uart);

    if (!err && uart->source == RT_UAPI_SOURCE)
	xnfree(uart);

    return err;
}

/*
 * int __rt_uart_read(RT_UART_PLACEHOLDER *ph,
 *                    void *buf,
 *                    size_t nbytes,
 *                    RTIME timeout)
 */

int __rt_uart_read (struct task_struct *curr, struct pt_regs *regs)

{
    RT_UART_PLACEHOLDER ph;
    void __user *buf;
    RT_UART *uart;
    size_t nbytes;
    RTIME timeout;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    /* (mlocked) Buffer to write. */
    buf = (void __user *)__xn_reg_arg2(regs);

    /* Number of bytes in buffer. */
    nbytes = (size_t)__xn_reg_arg3(regs);

    if (nbytes > 0 && !__xn_access_ok(curr,VERIFY_WRITE,buf,nbytes))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg4(regs),sizeof(timeout)))
	return -EFAULT;

    __xn_copy_from_user(curr,&timeout,(void __user *)__xn_reg_arg4(regs),sizeof(timeout));

    uart = (RT_UART *)rt_registry_fetch(ph.opaque);

    if (!uart)
	return -ESRCH;

    return rt_uart_read(uart,buf,nbytes,timeout);
}

/*
 * int __rt_uart_write(RT_UART_PLACEHOLDER *ph,
 *                     const void *buf,
 *                     size_t nbytes)
 */

int __rt_uart_write (struct task_struct *curr, struct pt_regs *regs)

{
    RT_UART_PLACEHOLDER ph;
    const void __user *buf;
    RT_UART *uart;
    size_t nbytes;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    /* (mlocked) Buffer to write. */
    buf = (const void __user *)__xn_reg_arg2(regs);

    /* Number of bytes in buffer. */
    nbytes = (size_t)__xn_reg_arg3(regs);

    if (nbytes > 0 && !__xn_access_ok(curr,VERIFY_READ,buf,nbytes))
	return -EFAULT;

    uart = (RT_UART *)rt_registry_fetch(ph.opaque);

    if (!uart)
	return -ESRCH;

    return rt_uart_write(uart,buf,nbytes);
}

/*
 * int __rt_uart_control(RT_UART_PLACEHOLDER *ph,
 *                       int cmd,
 *                       void *arg)
 */

int __rt_uart_control (struct task_struct *curr, struct pt_regs *regs)

{
    RT_UART_PLACEHOLDER ph;
    void __user *arg;
    RT_UART *uart;
    int cmd;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    uart = (RT_UART *)rt_registry_fetch(ph.opaque);

    if (!uart)
	return -ESRCH;

    /* Command word. */
    cmd = (int)__xn_reg_arg2(regs);

    /* (mlocked) Arg buffer. */
    arg = (void __user *)__xn_reg_arg3(regs);

    return rt_uart_control(uart,cmd,arg);
}

static xnsysent_t __systab[] = {
    [__rtai_uart_open ] = { &__rt_uart_open, __xn_flag_lostage },
    [__rtai_uart_close ] = { &__rt_uart_close, __xn_flag_lostage },
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
