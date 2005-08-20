/*
 * RTAI/fusion 64-bit PowerPC adoption
 * Copyright (C) 2005 Taneli Vähäkangas and Heikki Lindholm
 * based on previous work:
 *     
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _RTAI_ASM_PPC64_SYSCALL_H
#define _RTAI_ASM_PPC64_SYSCALL_H

#include <rtai_config.h>
#include <asm/ptrace.h>
#include <nucleus/asm-generic/syscall.h>

/*
 * Some of the following macros have been adapted from Linux's
 * implementation of the syscall mechanism in <asm-ppc64/unistd.h>:
 *
 * The following code defines an inline syscall mechanism used by
 * RTAI/fusion's real-time interfaces to invoke the skin module
 * services in kernel space.
 */

#define XENOMAI_DO_SYSCALL(nr, id, op, args...)			\
  ({								\
	register unsigned long __sc_0  __asm__ ("r0");		\
	register unsigned long __sc_3  __asm__ ("r3");		\
	register unsigned long __sc_4  __asm__ ("r4");		\
	register unsigned long __sc_5  __asm__ ("r5");		\
	register unsigned long __sc_6  __asm__ ("r6");		\
	register unsigned long __sc_7  __asm__ ("r7");		\
								\
	LOADARGS_##nr(__xn_mux_code(id,op), args);		\
	__asm__ __volatile__					\
		("sc           \n\t"				\
		 "mfcr %0      "				\
		: "=&r" (__sc_0),				\
		  "=&r" (__sc_3),  "=&r" (__sc_4),		\
		  "=&r" (__sc_5),  "=&r" (__sc_6),		\
		  "=&r" (__sc_7)				\
		: ASM_INPUT_##nr				\
		: "cr0", "ctr", "memory",			\
		  "r8", "r9", "r10","r11", "r12");		\
	(int)((__sc_0 & (1 << 28)) ? -__sc_3 : __sc_3);		\
  })

#define LOADARGS_0(muxcode, dummy...)				\
	__sc_0 = muxcode
#define LOADARGS_1(muxcode, arg1)				\
	LOADARGS_0(muxcode);					\
	__sc_3 = (unsigned long) (arg1)
#define LOADARGS_2(muxcode, arg1, arg2)				\
	LOADARGS_1(muxcode, arg1);				\
	__sc_4 = (unsigned long) (arg2)
#define LOADARGS_3(muxcode, arg1, arg2, arg3)			\
	LOADARGS_2(muxcode, arg1, arg2);			\
	__sc_5 = (unsigned long) (arg3)
#define LOADARGS_4(muxcode, arg1, arg2, arg3, arg4)		\
	LOADARGS_3(muxcode, arg1, arg2, arg3);			\
	__sc_6 = (unsigned long) (arg4)
#define LOADARGS_5(muxcode, arg1, arg2, arg3, arg4, arg5)	\
	LOADARGS_4(muxcode, arg1, arg2, arg3, arg4);		\
	__sc_7 = (unsigned long) (arg5)

#define ASM_INPUT_0 "0" (__sc_0)
#define ASM_INPUT_1 ASM_INPUT_0, "1" (__sc_3)
#define ASM_INPUT_2 ASM_INPUT_1, "2" (__sc_4)
#define ASM_INPUT_3 ASM_INPUT_2, "3" (__sc_5)
#define ASM_INPUT_4 ASM_INPUT_3, "4" (__sc_6)
#define ASM_INPUT_5 ASM_INPUT_4, "5" (__sc_7)

/* Register mapping for accessing syscall args. */

#define __xn_reg_mux(regs)    ((regs)->gpr[0])
#define __xn_reg_rval(regs)   ((regs)->gpr[3])
#define __xn_reg_arg1(regs)   ((regs)->gpr[3])
#define __xn_reg_arg2(regs)   ((regs)->gpr[4])
#define __xn_reg_arg3(regs)   ((regs)->gpr[5])
#define __xn_reg_arg4(regs)   ((regs)->gpr[6])
#define __xn_reg_arg5(regs)   ((regs)->gpr[7])

#define __xn_reg_mux_p(regs)        ((__xn_reg_mux(regs) & 0xffff) == __xn_sys_mux)
#define __xn_mux_id(regs)           ((__xn_reg_mux(regs) >> 16) & 0xff)
#define __xn_mux_op(regs)           ((__xn_reg_mux(regs) >> 24) & 0xff)
#define __xn_mux_code(id,op)        ((op << 24)|((id << 16) & 0xff0000)|(__xn_sys_mux & 0xffff))

#define XENOMAI_SYSCALL0(op)                XENOMAI_DO_SYSCALL(0,0,op)
#define XENOMAI_SYSCALL1(op,a1)             XENOMAI_DO_SYSCALL(1,0,op,a1)
#define XENOMAI_SYSCALL2(op,a1,a2)          XENOMAI_DO_SYSCALL(2,0,op,a1,a2)
#define XENOMAI_SYSCALL3(op,a1,a2,a3)       XENOMAI_DO_SYSCALL(3,0,op,a1,a2,a3)
#define XENOMAI_SYSCALL4(op,a1,a2,a3,a4)    XENOMAI_DO_SYSCALL(4,0,op,a1,a2,a3,a4)
#define XENOMAI_SYSCALL5(op,a1,a2,a3,a4,a5) XENOMAI_DO_SYSCALL(5,0,op,a1,a2,a3,a4,a5)

#define XENOMAI_SKINCALL0(id,op)                XENOMAI_DO_SYSCALL(0,id,op)
#define XENOMAI_SKINCALL1(id,op,a1)             XENOMAI_DO_SYSCALL(1,id,op,a1)
#define XENOMAI_SKINCALL2(id,op,a1,a2)          XENOMAI_DO_SYSCALL(2,id,op,a1,a2)
#define XENOMAI_SKINCALL3(id,op,a1,a2,a3)       XENOMAI_DO_SYSCALL(3,id,op,a1,a2,a3)
#define XENOMAI_SKINCALL4(id,op,a1,a2,a3,a4)    XENOMAI_DO_SYSCALL(4,id,op,a1,a2,a3,a4)
#define XENOMAI_SKINCALL5(id,op,a1,a2,a3,a4,a5) XENOMAI_DO_SYSCALL(5,id,op,a1,a2,a3,a4,a5)

#ifdef __KERNEL__

#include <linux/errno.h>
#include <asm/uaccess.h>

/* Our own set of copy-to/from-user macros which must bypass
   might_sleep() checks. The caller cannot fault and is expected to
   have checked for bad range before using the copy macros, so we
   should not have to care about the result. */
#define __xn_copy_from_user(task,dstP,srcP,n)  \
    ({ int err = __copy_from_user_inatomic(dstP,srcP,n); err; })
#define __xn_copy_to_user(task,dstP,srcP,n)  \
    ({ int err = __copy_to_user_inatomic(dstP,srcP,n); err; })
#define __xn_put_user(task,src,dstP)           __put_user(src,dstP)
#define __xn_get_user(task,dst,srcP)           __get_user(dst,srcP)
#define __xn_strncpy_from_user(task,dstP,srcP,n)    __strncpy_from_user(dstP,srcP,n)

#define __xn_range_ok(task,addr,size) \
	__access_ok(((__force unsigned long)(addr)),(size),(task->thread.fs))

#define __xn_access_ok(task,type,addr,size)  __xn_range_ok(task,addr,size)

/* Purposedly used inlines and not macros for the following routines
   so that we don't risk spurious side-effects on the value arg. */

static inline void __xn_success_return(struct pt_regs *regs, int v) {
    __xn_reg_rval(regs) = v;
}

static inline void __xn_error_return(struct pt_regs *regs, int v) {
    /* We currently never set the SO bit for marking errors, even if
     * we always test it upon syscall return. */
    __xn_reg_rval(regs) = v;
}

static inline void __xn_status_return(struct pt_regs *regs, int v) {
    __xn_reg_rval(regs) = v;
}

static inline int __xn_interrupted_p(struct pt_regs *regs) {
    return __xn_reg_rval(regs) == -EINTR;
}

#else /* !__KERNEL__ */

#define CONFIG_RTAI_HW_DIRECT_TSC 1

static inline unsigned long long __xn_rdtsc (void)

{
    unsigned long long t;

    __asm__ __volatile__ ("mftb %0\n" : "=r" (t));
    return t;
}

#endif /* __KERNEL__ */

#endif /* !_RTAI_ASM_PPC_SYSCALL_H */
