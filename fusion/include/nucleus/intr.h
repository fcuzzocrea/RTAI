/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _RTAI_NUCLEUS_INTR_H
#define _RTAI_NUCLEUS_INTR_H

#include <nucleus/types.h>

#define XN_ISR_HANDLED   0x0
#define XN_ISR_CHAINED   0x1
#define XN_ISR_ENABLE    0x2

#if defined(__KERNEL__) || defined(__RTAI_UVM__) || defined(__RTAI_SIM__)

struct xnintr;

typedef int (*xnisr_t)(struct xnintr *intr);

typedef struct xnintr {

    xnflags_t status;	/*!< Status bitmask. */

    unsigned irq;	/* !< IRQ number. */

    xnisr_t isr;	/* !< Interrupt service routine. */

    void *cookie;	/* !< User-defined cookie value. */

} xnintr_t;

extern xnintr_t nkclock;

#ifdef __cplusplus
extern "C" {
#endif

void xnintr_clock_handler(void);

    /* Public interface. */

int xnintr_init(xnintr_t *intr,
		unsigned irq,
		xnisr_t isr,
		xnflags_t flags);

int xnintr_destroy(xnintr_t *intr);

int xnintr_attach(xnintr_t *intr,
		  void *cookie);

int xnintr_detach(xnintr_t *intr);

int xnintr_enable(xnintr_t *intr);

int xnintr_disable(xnintr_t *intr);
    
xnarch_cpumask_t xnintr_affinity(xnintr_t *intr,
                                 xnarch_cpumask_t cpumask);

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ || __RTAI_UVM__ || __RTAI_SIM__ */

#endif /* !_RTAI_NUCLEUS_INTR_H */
