/*
 *   ARTI -- RTAI-compatible Adeos-based Real-Time Interface. Based on
 *   the original RTAI layer for x86.
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
   
#ifdef CONFIG_RTAI_FPU_SUPPORT

#define init_fpu(tsk) \
	do { init_xfpu(); tsk->used_math = 1; set_tsk_used_fpu(tsk); } while(0)

#define restore_fpu(tsk) \
	do { restore_fpenv_lxrt((tsk)); set_tsk_used_fpu(tsk); } while (0)

#define load_mxcsr(val) \
	do { \
        	unsigned long __mxcsr = ((unsigned long)(val) & 0xffbf); \
	        __asm__ __volatile__ ("ldmxcsr %0": : "m" (__mxcsr)); \
	} while (0)

#define save_cr0_and_clts(x) \
	do { \
		__asm__ __volatile__ ("movl %%cr0,%0; clts": "=r" (x)); \
	} while (0)

#define restore_cr0(x) \
	do { \
		if (x & 8) { \
                       unsigned long flags; \
                       rtai_hw_save_flags_and_cli(flags); \
			__asm__ __volatile__ ("movl %%cr0, %0": "=r" (x)); \
			__asm__ __volatile__ ("movl %0, %%cr0": :"r" (8 | x)); \
                       rtai_hw_restore_flags(flags); \
		} \
	} while (0)

#define enable_fpu() \
	do { \
		__asm__ __volatile__ ("clts"); \
	} while (0)

#define init_xfpu() \
	do { \
		__asm__ __volatile__ ("clts; fninit"); \
		if (cpu_has_xmm) { \
			load_mxcsr(0x1f80); \
		} \
	} while (0)

#define save_fpenv(x) \
	do { \
		if (cpu_has_fxsr) { \
			__asm__ __volatile__ ("fxsave %0; fnclex": "=m" (x)); \
		} else { \
			__asm__ __volatile__ ("fnsave %0; fwait": "=m" (x)); \
		} \
	} while (0)

#define restore_fpenv(x) \
	do { \
		if (cpu_has_fxsr) { \
			__asm__ __volatile__ ("fxrstor %0": : "m" (x)); \
		} else { \
			__asm__ __volatile__ ("frstor %0": : "m" (x)); \
		} \
	} while (0)

#define restore_task_fpenv(t) \
	do { \
               clts(); \
               restore_fpenv((t)->thread.i387.fsave); \
	} while (0)

#define restore_fpenv_lxrt(t) restore_task_fpenv(t)

#else /* !CONFIG_RTAI_FPU_SUPPORT */

static void init_fpu(struct task_struct *tsk) { }
#define restore_fpu(tsk)
#define save_cr0_and_clts(x)
#define restore_cr0(x)
#define enable_fpu()
#define load_mxcsr(val)
#define init_xfpu()
#define save_fpenv(x)
#define restore_fpenv(x)
#define restore_task_fpenv(t)
#define restore_fpenv_lxrt(t)

#endif /* CONFIG_RTAI_FPU_SUPPORT */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#define set_tsk_used_fpu(t)  do { \
        (t)->flags |= PF_USEDFPU; \
} while(0)
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0) */
#define set_tsk_used_fpu(t)  do { \
        (t)->thread_info->status |= TS_USEDFPU; \
} while(0)
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0) */

#endif /* !_RTAI_ASM_I386_FPU_H */
