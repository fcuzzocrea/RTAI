/*
asm-arm/arch-clps711x/arch.h - ARM/CLPS711x specific stuff comes here

Don't include directly - it's included through asm-arm/rtai.h

Copyright (c) 2002, Alex Züpke, SYSGO RTS GmbH (azu@sysgo.de)
Copyright (c) 2002, Thomas Gleixner, autronix automation (gleixner@autronix.de)
 
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
/*
-------------------------------------------------------------------
Acknowledgements
- Paolo Mantegazza	(mantegazza@aero.polimi.it)
	creator of RTAI 
*/

#ifndef _ASM_ARCH_RTAI_ARCH_H_
#define _ASM_ARCH_RTAI_ARCH_H_

#define FREQ_SYS_CLK        512000
#define LATENCY_MATCH_REG     2500
#define SETUP_TIME_MATCH_REG   500

#define TIMER_8254_IRQ 9

#define arch_mount_rtai()			\
{						\
	extern unsigned long rtai_lasttsc;	\
	extern union rtai_tsc rtai_tsc;		\
	/* setup our tsc compare register */	\
	rtai_tsc.tsc = 0LL;			\
	rtai_lasttsc = ( unsigned long) (clps_readl(TC1D) & 0xffff); \
}

#define arch_umount_rtai()	// nothing to do for umount

#define ARCH_EXPORTS EXPORT_SYMBOL(rtai_lasttsc); \
EXPORT_SYMBOL(rtai_TC2latch);

#endif
