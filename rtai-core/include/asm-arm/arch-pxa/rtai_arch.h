/* rtai/include/asm-arm/arch-sa1100/rtai_arch.h
-------------------------------------------------------------
DON´T include directly - it's included through asm-arm/rtai.h
-------------------------------------------------------------
COPYRIGHT (C) 2002 Guennadi Liakhovetski, DSA GmbH (gl@dsa-ac.de)
COPYRIGHT (C) 2002 Wolfgang Müller (wolfgang.mueller@dsa-ac.de)
Copyright (c) 2001 Alex Züpke, SYSGO RTS GmbH (azu@sysgo.de)

This program is free software; you can redistribute it and/or modify
it under the terms of version 2 of the GNU General Public License as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
/*
--------------------------------------------------------------------------
Acknowledgements
- Paolo Mantegazza	(mantegazza@aero.polimi.it)
	creator of RTAI 
*/

#ifndef _ASM_ARCH_RTAI_ARCH_H_
#define _ASM_ARCH_RTAI_ARCH_H_

#define FREQ_SYS_CLK       3686400
#define LATENCY_MATCH_REG     2000
#define SETUP_TIME_MATCH_REG   600
#define LATENCY_TICKS    (LATENCY_MATCH_REG/(1000000000/FREQ_SYS_CLK))
#define SETUP_TIME_TICKS (SETUP_TIME_MATCH_REG/(1000000000/FREQ_SYS_CLK))

#define TIMER_8254_IRQ		IRQ_OST0

#define ARCH_MUX_IRQ		IRQ_GPIO_2_80

#include <asm/arch/irq.h>
void rtai_pxa_GPIO_2_80_demux( int irq, void *dev_id, struct pt_regs *regs );

static inline void arch_mount_rtai( void )
{
	/* Let's take care about our "special" IRQ11 */
//	free_irq( IRQ_GPIO_2_80 );
	rt_request_global_irq_ext( IRQ_GPIO_2_80, rtai_pxa_GPIO_2_80_demux, NULL );
}

static inline void arch_umount_rtai( void )
{
	rt_free_global_irq( IRQ_GPIO_2_80 );
//	request_irq( IRQ_GPIO_2_80, pxa_GPIO_2_80_demux, SA_INTERRUPT, "GPIO 2-80", NULL );
}

/* Check, if this is a demultiplexed irq */
#define isdemuxirq(irq) (irq >= IRQ_GPIO(2))

#endif
