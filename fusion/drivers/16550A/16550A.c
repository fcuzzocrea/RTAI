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

#include <linux/ioport.h>
#include <nucleus/pod.h>
#include <16550A/16550A.h>
#include <16550A/syscall.h>

static DECLARE_XNQUEUE(__rtai_uart_q);

static inline int __do_stat (RT_UART *uart)

{
    int lsr = inb(LSR(uart)); /* Read line status. */

    lsr &= (LSR_DATA|LSR_OE|LSR_PE|LSR_BI|LSR_FE);

    if (lsr & ~LSR_DATA) /* Save any pending error status. */
	{
	uart->status &= ~(LSR_OE|LSR_PE|LSR_BI|LSR_FE);
	uart->status |= (lsr & ~LSR_DATA);
	}

    return lsr;
}

static inline void __do_modem (RT_UART *uart)

{
    inb(MSR(uart)); /* Reset bits. */
}

static inline void __do_rx (RT_UART *uart)

{
    int c, rbytes = 0;

    do
	{
	c = inb(RHR(uart)); /* Read input character. */

	uart->i_buf[uart->i_tail++] = c;
	uart->i_tail &= (sizeof(uart->i_buf) - 1);

	if (++uart->i_count > sizeof(uart->i_buf))
	    {
	    uart->status |= LSR_OE; /* Buffer overflow: notify overrun. */
	    --uart->i_count;
	    }

	++rbytes;
	}
    while (__do_stat(uart) & LSR_DATA);	/* Collect error conditions. */

    if (uart->i_nwait <= rbytes)
	{
	uart->i_nwait = 0;
	rt_sem_v(&uart->i_pulse);
	}
    else if (uart->i_nwait > 0)
	uart->i_nwait -= rbytes;
}

static inline void __do_tx (RT_UART *uart)

{
    int c, count;

    /* Output until the FIFO is full or all buffered bytes have been
       written out. */

    for (count = uart->config.fifo_depth;
	 count > 0 && uart->o_count > 0;
	 count--, uart->o_count--)
	{
	c = uart->o_buf[uart->o_head++];
	outb(c,THR(uart));
	uart->o_head &= (sizeof(uart->o_buf) - 1);
	}

    if (uart->o_count == 0)
	{
	/* Mask transmitter empty interrupt. */
	outb(IER_RX|IER_STAT|IER_MODEM,IER(uart));
	/* Wake up the sending task. */
	rt_sem_v(&uart->o_pulse);
	}
}

static int __uart_isr (RT_INTR *intr)

{
    RT_UART *uart = (RT_UART *)intr->private_data;
    int iir, req = 0;

    while (((iir = inb(IIR(uart))) & IIR_PIRQ) == 0)
	{
	switch (iir & IIR_MASK)
	    {
	    case IIR_RX:
	    case IIR_TMO:

		__do_rx(uart);
		break;

	    case IIR_TX:

		__do_tx(uart);
		break;

	    case IIR_STAT:

		__do_stat(uart);
		break;

	    case IIR_MODEM:

		__do_modem(uart);
		break;
	    }

	++req;
	}

    /* Allow sharing the interrupt line with some other device down in
       the pipeline. */
    return req > 0 ? RT_INTR_HANDLED|RT_INTR_ENABLE : RT_INTR_CHAINED;
}

int rt_uart_open (RT_UART *uart,
		  RT_UART_CONFIG *config)
{
    int err;
    spl_t s;

    if (!xnpod_root_p())
	return -EACCES;

    if (!request_region(config->port.base,8,"RTAI-based 16550A driver"))
	return -EBUSY;

    xnlock_get_irqsave(&nklock,s);

    err = rt_intr_create(&uart->intr_desc,
			 config->port.irq,
			 &__uart_isr,
			 0);
    if (err)
	goto unlock_and_exit;

    uart->intr_desc.private_data = uart;
    uart->magic = RTAI_UART_MAGIC;
    uart->handle = 0;
    snprintf(uart->name,sizeof(uart->name),"uart/%x",config->port.base);

    /* Mask all UART interrupts and clear pending ones. */
    outb(0,IER(uart));
    inb(IIR(uart));
    inb(LSR(uart));
    inb(RHR(uart));
    inb(MSR(uart));

    uart->config = *config;

    uart->i_head = 0;
    uart->i_tail = 0;
    uart->i_nwait = 0;
    uart->i_count = 0;
    rt_sem_create(&uart->i_pulse,NULL,0,S_FIFO|S_PULSE);
    rt_sem_create(&uart->i_lock,NULL,1,S_PRIO);

    uart->o_head = 0;
    uart->o_tail = 0;
    uart->o_count = 0;
    rt_sem_create(&uart->o_pulse,NULL,0,S_FIFO|S_PULSE);

    uart->status = 0;

    /* Set Baud rate. */
    outb(LCR_DLAB,LCR(uart));
    outb(config->speed & 0xff,DLL(uart));
    outb(config->speed >> 8,DLM(uart));

    /* Set parity, stop bits and character size. */
    outb((config->parity << 3)|(config->stop_bits << 2)|config->data_bits,LCR(uart));

    /* Reset FIFO and set triggers. */
    outb(FCR_FIFO|FCR_RESET,FCR(uart));
    outb(FCR_FIFO|config->fifo_depth,FCR(uart));

    /* Set modem control information. */
    outb(MCR_DTR|MCR_RTS|MCR_OP1|MCR_OP2,MCR(uart));

    /* Enable UART interrupts (except TX). */
    outb(IER_RX|IER_STAT|IER_MODEM,IER(uart));

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    uart->source = RT_KAPI_SOURCE;
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    /* <!> Since rt_register_enter() may reschedule, only register
       complete objects, so that the registry cannot return handles to
       half-baked objects... */

    err = rt_registry_enter(uart->name,uart,&uart->handle);

    if (err)
	rt_uart_close(uart);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

int rt_uart_close (RT_UART *uart)

{
    int err = 0;
    spl_t s;

    if (!xnpod_root_p())
	return -EACCES;

    xnlock_get_irqsave(&nklock,s);

    uart = rtai_h2obj_validate(uart,RTAI_UART_MAGIC,RT_UART);

    if (!uart)
        {
        err = rtai_handle_error(uart,RTAI_UART_MAGIC,RT_UART);
        goto unlock_and_exit;
        }
    
    rt_intr_delete(&uart->intr_desc);

    /* Mask all UART interrupts and clear pending ones. */
    outb(0,IER(uart));
    inb(IIR(uart));
    inb(LSR(uart));
    inb(RHR(uart));
    inb(MSR(uart));

    /* Clear DTS/RTS. */
    outb(MCR_OP1|MCR_OP2,MCR(uart));

    release_region(config->port.base,8);

    rt_sem_delete(&uart->o_pulse);
    rt_sem_delete(&uart->i_pulse);
    rt_sem_delete(&uart->i_lock);

    rtai_mark_deleted(uart);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

ssize_t rt_uart_read (RT_UART *uart,
		      void *buf,
		      size_t nbytes,
		      RTIME timeout)
{
    unsigned char *cp = (unsigned char *)buf;
    int c, rbytes = 0;
    ssize_t ret;
    spl_t s;

    if (nbytes == 0)
	return 0;

    xnlock_get_irqsave(&nklock,s);

    uart = rtai_h2obj_validate(uart,RTAI_UART_MAGIC,RT_UART);

    if (!uart)
        {
        ret = rtai_handle_error(uart,RTAI_UART_MAGIC,RT_UART);
        goto unlock_and_exit;
        }

    while (rbytes < nbytes)
	{
	if (uart->i_count > 0)
	    {
	    c = uart->i_buf[uart->i_head++];
	    uart->i_head &= (sizeof(uart->i_buf) - 1);
	    cp[rbytes++] = c;
	    uart->i_count--;
            continue;
	    }

	if (timeout == TM_NONBLOCK)
	    {
	    ret = -EWOULDBLOCK;
	    goto unlock_and_exit;
	    }

	rt_sem_p(&uart->i_lock,TM_INFINITE);
	uart->i_nwait = nbytes - rbytes;
        ret = rt_sem_p(&uart->i_pulse,timeout);
	rt_sem_v(&uart->i_lock);

        if (ret)
	    goto unlock_and_exit;
	}

    if (uart->status == 0)
	ret = rbytes;
    else
	{
	if (uart->status & LSR_BI) /* Break. */
	    ret = -EPIPE;
	else if (uart->status & LSR_OE)	/* Overrun error. */
	    ret = -ENOSPC;
	else
	    ret = -EIO;	/*  Framing and Parity errors. */

	uart->status = 0;
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return ret;
}

ssize_t rt_uart_write (RT_UART *uart,
		       const void *buf,
		       size_t nbytes)
{
    const unsigned char *cp = (const unsigned char *)buf;
    int wbytes = 0;
    ssize_t ret;
    spl_t s;

    if (nbytes == 0)
	return 0;

    xnlock_get_irqsave(&nklock,s);

    uart = rtai_h2obj_validate(uart,RTAI_UART_MAGIC,RT_UART);

    if (!uart)
        {
        ret = rtai_handle_error(uart,RTAI_UART_MAGIC,RT_UART);
        goto unlock_and_exit;
        }

    /* Enable transmitter empty interrupt. */
    outb(IER_RX|IER_TX|IER_STAT|IER_MODEM,IER(uart));

    while (wbytes < nbytes)
	{
	if (uart->o_count < sizeof(uart->o_buf))
	    {
            uart->o_buf[uart->o_tail++] = cp[wbytes++];
            uart->o_tail &= (sizeof(uart->o_buf) - 1);
	    uart->o_count++;
	    continue;
	    }

        ret = rt_sem_p(&uart->o_pulse,TM_INFINITE);

        if (ret)
	    goto unlock_and_exit;
	}

    ret = wbytes;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return ret;
}

int rt_uart_control (RT_UART *uart,
		     int cmd,
		     void *arg)
{
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    uart = rtai_h2obj_validate(uart,RTAI_UART_MAGIC,RT_UART);

    if (!uart)
        {
        err = rtai_handle_error(uart,RTAI_UART_MAGIC,RT_UART);
        goto unlock_and_exit;
        }
    
    switch (cmd)
	{
	default:

	    err = -EINVAL;
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

int __uart_pkg_init (void)

{
    int err = 0;

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    err = __uart_syscall_init();
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    return err;
}

void __uart_pkg_exit (void)

{
    xnholder_t *holder;

#if defined (__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    __uart_syscall_cleanup();
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    while ((holder = getheadq(&__rtai_uart_q)) != NULL)
	rt_uart_close(link2uart(holder));
}

module_init(__uart_pkg_init);
module_exit(__uart_pkg_exit);
