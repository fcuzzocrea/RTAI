/*
 * Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>.
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

/* ************************************************************************
 *  NOTE: This is a PRELIMINARY version of the common interface every
 *        RTDM-compliant serial device has to provide. This revision may
 *        still change until the final version. E.g., all definitions need
 *        to be reviewed if they do not contain too much 16550A-specifics
 *        or if significant features are missing.
 * ************************************************************************/

#ifndef _RTSERIAL_H
#define _RTSERIAL_H

#include <rtdm/rtdm.h>


/* Baudrate */
#define RTSER_50_BAUD               2304
#define RTSER_75_BAUD               1536
#define RTSER_110_BAUD              1047
#define RTSER_134_5_BAUD            857
#define RTSER_150_BAUD              768
#define RTSER_300_BAUD              384
#define RTSER_600_BAUD              192
#define RTSER_1200_BAUD             96
#define RTSER_2400_BAUD             48
#define RTSER_3600_BAUD             32
#define RTSER_4800_BAUD             24
#define RTSER_7200_BAUD             16
#define RTSER_9600_BAUD             12
#define RTSER_19200_BAUD            6
#define RTSER_38400_BAUD            3
#define RTSER_57600_BAUD            2
#define RTSER_115200_BAUD           1
#define RTSER_DEF_BAUD              RTSER_9600_BAUD

#define RTSER_CUSTOM_BAUD(base, rate) \
    ((base + (rate >> 1)) / rate)

/* Parity */
#define RTSER_NO_PARITY             0x00
#define RTSER_ODD_PARITY            0x01
#define RTSER_EVEN_PARITY           0x03
#define RTSER_DEF_PARITY            RTSER_NO_PARITY

/* Data bits */
#define RTSER_5_BITS                0x00
#define RTSER_6_BITS                0x01
#define RTSER_7_BITS                0x02
#define RTSER_8_BITS                0x03
#define RTSER_DEF_BITS              RTSER_8_BITS

/* Stop bits */
#define RTSER_1_STOPB               0x00
#define RTSER_1_5_STOPB             0x01 /* when using 5 data bits */
#define RTSER_2_STOPB               0x01 /* when using >5 data bits */
#define RTSER_DEF_STOPB             RTSER_1_STOPB

/* Handshake */
#define RTSER_NO_HAND               0x00
#define RTSER_RTSCTS_HAND           0x01
#define RTSER_DEF_HAND              RTSER_NO_HAND

/* FIFO depth */
#define RTSER_FIFO_DEPTH_1          0x00
#define RTSER_FIFO_DEPTH_4          0x40
#define RTSER_FIFO_DEPTH_8          0x80
#define RTSER_FIFO_DEPTH_14         0xC0
#define RTSER_DEF_FIFO_DEPTH        RTSER_FIFO_DEPTH_1

/* Special timeouts */
#define RTSER_TIMEOUT_INFINITE      0
#define RTSER_TIMEOUT_NONE          ((__u64)-1)
#define RTSER_DEF_TIMEOUT           RTSER_TIMEOUT_INFINITE

/* Timestamp history */
#define RTSER_RX_TIMESTAMP_HISTORY  0x01
#define RTSER_DEF_TIMESTAMP_HISTORY 0x00


/* Configuration mask bits */
#define RTSER_BAUD                  0x0001
#define RTSER_PARITY                0x0002
#define RTSER_DATA_BITS             0x0004
#define RTSER_STOP_BITS             0x0008
#define RTSER_HANDSHAKE             0x0010
#define RTSER_FIFO_DEPTH            0x0020

#define RTSER_TIMEOUT_RX            0x0100
#define RTSER_TIMEOUT_TX            0x0200
#define RTSER_TIMEOUT_EVENT         0x0400
#define RTSER_TIMESTAMP_HISTORY     0x0800


/* Line status */
#define RTSER_LSR_DATA              0x01
#define RTSER_LSR_OVERRUN_ERR       0x02
#define RTSER_LSR_PARITY_ERR        0x04
#define RTSER_LSR_FRAMING_ERR       0x08
#define RTSER_LSR_BREAK_IND         0x10
#define RTSER_LSR_THR_EMTPY         0x20
#define RTSER_LSR_TRANSM_EMPTY      0x40
#define RTSER_LSR_FIFO_ERR          0x80

#define RTSER_SOFT_OVERRUN_ERR      0x0100


/* Modem status */
#define RTSER_MSR_DCTS              0x01
#define RTSER_MSR_DDSR              0x02
#define RTSER_MSR_TERI              0x04
#define RTSER_MSR_DDCD              0x08
#define RTSER_MSR_CTS               0x10
#define RTSER_MSR_DSR               0x20
#define RTSER_MSR_RI                0x40
#define RTSER_MSR_DCD               0x80


/* Modem control */
#define RTSER_MCR_DTR               0x01
#define RTSER_MCR_RTS               0x02
#define RTSER_MCR_OUT1              0x04
#define RTSER_MCR_OUT2              0x08
#define RTSER_MCR_LOOP              0x10


/* Events */
#define RTSER_EVENT_RXPEND          0x01
#define RTSER_EVENT_ERRPEND         0x02
#define RTSER_EVENT_MODEMHI         0x04
#define RTSER_EVENT_MODEMLO         0x08


typedef struct rtser_config {
    int     config_mask;
    int     baud_rate;
    int     parity;
    int     data_bits;
    int     stop_bits;
    int     handshake;
    int     fifo_depth;

    __u64   rx_timeout;
    __u64   tx_timeout;
    __u64   event_timeout;
    int     timestamp_history;
} rtser_config_t;

typedef struct rtser_status {
    int     line_status;
    int     modem_status;
} rtser_status_t;

typedef struct rtser_event {
    int     events;
    int     rx_pending;
    __u64   last_timestamp;
    __u64   rxpend_timestamp;
} rtser_event_t;


#ifndef RTIOC_TYPE_SERIAL
#define RTIOC_TYPE_SERIAL           RTDM_CLASS_SERIAL

#define RTSER_RTIOC_GET_CONFIG      _IOR(RTIOC_TYPE_SERIAL, 0x00, \
                                         struct rtser_config)
#define RTSER_RTIOC_SET_CONFIG      _IOW(RTIOC_TYPE_SERIAL, 0x01, \
                                         struct rtser_config)
#define RTSER_RTIOC_GET_STATUS      _IOR(RTIOC_TYPE_SERIAL, 0x02, \
                                         struct rtser_status)
#define RTSER_RTIOC_GET_CONTROL     _IOR(RTIOC_TYPE_SERIAL, 0x03, \
                                         int)
#define RTSER_RTIOC_SET_CONTROL     _IOW(RTIOC_TYPE_SERIAL, 0x04, \
                                         int)
#define RTSER_RTIOC_SET_EVENT_MASK  _IOW(RTIOC_TYPE_SERIAL, 0x05, \
                                         int)
#define RTSER_RTIOC_WAIT_EVENT      _IOR(RTIOC_TYPE_SERIAL, 0x06, \
                                         struct rtser_event)
#endif /* !RTIOC_TYPE_SERIAL */

#endif /* _RTSERIAL_H */
