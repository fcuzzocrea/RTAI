/*
 *   Adeos-based Real-Time Hardware Abstraction Layer for x86. FPU
 *   support.
 *
 *   Copyright (C) 1994 Linus Torvalds
 *   Copyright (C) 2000 Gareth Hughes <gareth@valinux.com>,
 *   Copyright (C) 2000 Pierre Cloutier <pcloutier@PoseidonControls.com>
 *   and others.
 *
 *   RTAI/x86 rewrite over Adeos:
 *   Copyright (C) 2002 Philippe Gerum.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _RTAI_ASM_I386_FPU_H
#define _RTAI_ASM_I386_FPU_H

#ifndef __cplusplus
#include <asm/processor.h>
#endif /* !__cplusplus */

typedef union i387_union FPU_ENV;
   
#ifdef CONFIG_RTAI_HW_FPU

#define rthal_load_mxcsr(val) \
	do { \
        	unsigned long __mxcsr = ((unsigned long)(val) & 0xffbf); \
	        __asm__ __volatile__ ("ldmxcsr %0": : "m" (__mxcsr)); \
	} while (0)

#define rthal_init_xfpu() \
	do { \
		__asm__ __volatile__ ("clts; fninit"); \
		if (cpu_has_xmm) { \
			rthal_load_mxcsr(0x1f80); \
		} \
	} while (0)

#define rthal_save_fpenv(x) \
	do { \
		if (cpu_has_fxsr) { \
			__asm__ __volatile__ ("fxsave %0; fnclex": "=m" (x)); \
		} else { \
			__asm__ __volatile__ ("fnsave %0; fwait": "=m" (x)); \
		} \
	} while (0)

#define rthal_restore_fpenv(x) \
	do { \
		if (cpu_has_fxsr) { \
			__asm__ __volatile__ ("fxrstor %0": : "m" (x)); \
		} else { \
			__asm__ __volatile__ ("frstor %0": : "m" (x)); \
		} \
	} while (0)

#define rthal_restore_linux_fpenv(t) \
	do { \
               clts(); \
               rthal_restore_fpenv((t)->thread.i387.fsave); \
	} while (0)

#else /* !CONFIG_RTAI_HW_FPU */

#define rthal_load_mxcsr(val)
#define rthal_init_xfpu()
#define rthal_save_fpenv(x)
#define rthal_restore_fpenv(x)
#define rthal_restore_linux_fpenv(t)

#endif /* CONFIG_RTAI_HW_FPU */

#endif /* !_RTAI_ASM_I386_FPU_H */
