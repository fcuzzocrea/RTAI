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

#ifndef _RTAI_16550A_H
#define _RTAI_16550A_H

#include <nucleus/asm/uart.h>
#include <rtai/types.h>

#define RTAI_UART_MAGIC 0x55558000

typedef struct rt_uart_placeholder {
    rt_handle_t opaque;
} RT_UART_PLACEHOLDER;

typedef struct rt_uart_config {

    struct __uart_port { /* TTYS0 - TTYSx */
	int base;
	int irq;
    } port;

    int fifo_depth;
#define RT_UART_DEPTH1  0x0
#define RT_UART_DEPTH4  0x40
#define RT_UART_DEPTH8  0x80
#define RT_UART_DEPTH14 0xc0

    int speed;
#define RT_UART_50     2304
#define RT_UART_75     1536
#define RT_UART_110    1047
#define RT_UART_134_5  857
#define RT_UART_150    768
#define RT_UART_300    384
#define RT_UART_600    192
#define RT_UART_1200   96
#define RT_UART_2400   48
#define RT_UART_3600   32
#define RT_UART_4800   24
#define RT_UART_7200   16
#define RT_UART_9600   12
#define RT_UART_19200  6
#define RT_UART_38400  3
#define RT_UART_56K    2
#define RT_UART_115K   1

    int parity;
#define RT_UART_NOPARITY 0x0
#define RT_UART_ODDP     0x1
#define RT_UART_EVENP    0x3

    int data_bits;
#define RT_UART_CHR5  0x0
#define RT_UART_CHR6  0x1
#define RT_UART_CHR7  0x2
#define RT_UART_CHR8  0x3

    int stop_bits;
#define RT_UART_STOPB1 0x0
#define RT_UART_STOPB2 0x1

    int handshake;
#define RT_UART_NOHAND  0x0
#define RT_UART_RTSCTS  0x1

    int rts_hiwm; /* High watermark on RX for clearing RTS */
    int rts_lowm; /* Low watermark on RX for raising RTS */
#define RT_UART_RTSWM_DEFAULT { 0, 0 }

} RT_UART_CONFIG;

#ifdef __KERNEL__

#include <rtai/sem.h>
#include <rtai/intr.h>
#include <rtai/registry.h>

#define uart_base(uart) ((uart)->config.port.base)
#define RHR(uart) (uart_base(uart) + 0)	/* Receive Holding Buffer */
#define THR(uart) (uart_base(uart) + 0)	/* Transmit Holding Buffer */
#define DLL(uart) (uart_base(uart) + 0)	/* Divisor Latch LSB */
#define IER(uart) (uart_base(uart) + 1)	/* Interrupt Enable Register */
#define DLM(uart) (uart_base(uart) + 1)	/* Divisor Latch MSB */
#define IIR(uart) (uart_base(uart) + 2)	/* Interrupt Id Register */
#define FCR(uart) (uart_base(uart) + 2)	/* Fifo Control Register */
#define LCR(uart) (uart_base(uart) + 3)	/* Line Control Register */
#define MCR(uart) (uart_base(uart) + 4)	/* Modem Control Register */
#define LSR(uart) (uart_base(uart) + 5)	/* Line Status Register */
#define MSR(uart) (uart_base(uart) + 6)	/* Modem Status Register */

#define IER_RX    0x1
#define IER_TX    0x2
#define IER_STAT  0x4
#define IER_MODEM 0x8

#define IIR_MODEM 0x0
#define IIR_PIRQ  0x1
#define IIR_TX    0x2
#define IIR_RX    0x4
#define IIR_STAT  0x6
#define IIR_TMO   0xc
#define IIR_MASK  0xf

#define LCR_DLAB  0x80

#define LSR_DATA  0x1
#define LSR_OE    0x2
#define LSR_PE    0x4
#define LSR_FE    0x8
#define LSR_BI    0x10
#define LSR_THRE  0x20
#define LSR_TSRE  0x40

#define MCR_DTR   0x1
#define MCR_RTS   0x2
#define MCR_OP1   0x4
#define MCR_OP2   0x8

#define MSR_DCTS  0x01
#define MSR_DDSR  0x02
#define MSR_TERI  0x04
#define MSR_DDCD  0x08
#define MSR_CTS   0x10
#define MSR_DSR   0x20
#define MSR_RI    0x40
#define MSR_DCD   0x80

#define FCR_FIFO  0x1
#define FCR_RESET 0x6

#define RT_UART_BUFSZ 4096	/* Must be ^2 */

typedef struct rt_uart {

    unsigned magic;   /* !< Magic code - must be first */

    xnholder_t link;	/* !< Link in global UART queue. */

#define link2uart(laddr) \
((RT_UART *)(((char *)laddr) - (int)(&((RT_UART *)0)->link)))

    RT_INTR intr_desc;		/* !< Interrupt descriptor. */

    RT_UART_CONFIG config;	/* !< Current UART configuration. */

    int i_head;			/* !< Input store cursor. */
    int i_tail;			/* !< Input load cursor. */
    int i_nwait;		/* !< Size of current read request. */
    int i_count;		/* !< Number of pending input bytes. */
    RT_SEM i_pulse;		/* !< Input ready pulse. */
    RT_SEM i_lock;		/* !< Inter-task input lock. */
    unsigned char i_buf[RT_UART_BUFSZ];  /* !< Input store buffer. */

    int o_head;			/* !< Output store cursor. */
    int o_tail;			/* !< Output load cursor. */
    int o_count;		/* !< Number of pending output bytes. */
    RT_SEM o_pulse;		/* !< Output ready pulse. */
    unsigned char o_buf[RT_UART_BUFSZ]; /* !< Output store buffer. */

    int status;			/* !< UART status information. */
    int modem;			/* !< UART modem flags. */

    rt_handle_t handle;		/* !< Handle in registry -- zero if unregistered. */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    int cpid;			/* !< Opener's pid. */
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

} RT_UART;

#else /* !__KERNEL__ */

typedef RT_UART_PLACEHOLDER RT_UART;

#endif /* __KERNEL__ */

#ifdef __cplusplus
extern "C" {
#endif

/* Public interface. */

int rt_uart_open(RT_UART *uart,
		 RT_UART_CONFIG *config);

int rt_uart_close(RT_UART *uart);

ssize_t rt_uart_read(RT_UART *uart,
		     void *buf,
		     size_t nbytes,
		     RTIME timeout);

ssize_t rt_uart_write(RT_UART *uart,
		      const void *buf,
		      size_t nbytes);

int rt_uart_control(RT_UART *uart,
		    int cmd,
		    void *arg);

#ifdef __cplusplus
}
#endif

#endif /* !_RTAI_16550A_H */
