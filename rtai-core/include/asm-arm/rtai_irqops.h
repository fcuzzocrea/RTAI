/*
COPYRIGHT (C) 2001 Guennadi Liakhovetski, DSA GmbH (gl@dsa-ac.de)

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
*/

#ifndef _ASM_IRQOPS_H
#define _ASM_IRQOPS_H

typedef struct {
	struct list_head pending;
	volatile int pend_count;
} linux_irq_t;

static linux_irq_t linux_irqs[NR_IRQS];

#define INC_WRAP(x,y) do { x = ++(x) & ((y)-1); } while (0)

static inline int llffnz( unsigned long long ull )
{
	return ((unsigned long *)&ull)[0] ?
		ffnz( ((unsigned long *)&ull)[0] ) :
		( ((unsigned long *)&ull)[1] ?
		  ffnz( ((unsigned long *)&ull)[1] ) + 32 :
		  -1 );
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,19)
#define linux_unmasked(desc) ((desc)->enabled)
#else
#define linux_unmasked(desc) (!((desc)->masked || (desc)->disable_depth))
#endif

/* Called with interrupts hard-disabled */
static inline int have_pending_irq( void )
{
	linux_irq_t *irq_p;
	struct list_head *listhead_p;
	struct irqdesc *desc;
	int irq;

	list_for_each(listhead_p, &global.pending_linux_irq) {
		irq_p = list_entry(listhead_p, linux_irq_t, pending);
		irq = irq_p - linux_irqs;
		desc = irq_desc + irq;
		if (linux_unmasked(desc) &&
		    !(isdemuxirq(irq) && irq_desc[ARCH_MUX_IRQ].running))
			return irq;
	}
	return NO_IRQ;
}

static inline int have_pending_srq( void )
{
	return ((global.pending_srqs & ~global.active_srqs) != 0);
}

static inline int active_irq(unsigned int irq)
{
	return !linux_unmasked(irq_desc + irq);
}

static inline unsigned int pending_srq( void )
{
	return llffnz(global.pending_srqs & ~global.active_srqs);
}

void rt_pend_linux_irq(unsigned int irq)
{
	if (!linux_irqs[irq].pend_count++)
		list_add_tail(&linux_irqs[irq].pending, &global.pending_linux_irq);
}

static inline void clear_pending_irq( unsigned int irq )
{
	linux_irqs[irq].pend_count = 0;
	list_del(&linux_irqs[irq].pending);
}

static inline void activate_srq( unsigned int srq )
{
	set_bit(srq, &global.active_srqs);
}

static inline void deactivate_srq( unsigned int srq )
{
	clear_bit(srq, &global.active_srqs);
}

void rt_pend_linux_srq(unsigned int srq)
{
	if ( srq < 0 || srq >= NR_SYSRQS ) {
		printk( "Invalid SRQ! (pend:) %d\n", srq );
		return;
	}
	set_bit(srq, &global.pending_srqs);
}

static inline void clear_pending_srq( unsigned int srq )
{
	if ( srq < 0 || srq >= NR_SYSRQS ) {
		printk( "Invalid SRQ! (clear:) %d\n", srq );
		return;
	}
	clear_bit(srq, &global.pending_srqs);
}

static inline void init_pending_srqs( void )
{
	global.pending_srqs = global.active_srqs = 0;
}
#endif
