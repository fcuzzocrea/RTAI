/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * As a special exception, the RTAI project gives permission
 * for additional uses of the text contained in its release of
 * Xenomai.
 *
 * The exception is that, if you link the Xenomai libraries with other
 * files to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the Xenomai libraries code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public
 * License.
 *
 * This exception applies only to the code released by the
 * RTAI project under the name Xenomai.  If you copy code from other
 * RTAI project releases into a copy of Xenomai, as the General Public
 * License permits, the exception does not apply to the code that you
 * add in this way.  To avoid misleading anyone as to the status of
 * such modified files, you must delete this exception notice from
 * them.
 *
 * If you write modifications of your own for Xenomai, it is your
 * choice whether to permit this exception to apply to your
 * modifications. If you do not wish that, delete this exception
 * notice.
 */

#ifndef _RTAI_ASM_PPC_SYSCALL_H
#define _RTAI_ASM_PPC_SYSCALL_H

#include <asm/ptrace.h>

#ifdef __KERNEL__

#include <asm/uaccess.h>

#define __xn_copy_from_user(task,dstP,srcP,n)  __copy_from_user(dstP,srcP,n)
#define __xn_copy_to_user(task,dstP,srcP,n)    __copy_to_user(dstP,srcP,n)
#define __xn_put_user(task,src,dstP)           __put_user(src,dstP)
#define __xn_get_user(task,dst,srcP)           __get_user(dst,srcP)

#define __xn_range_ok(task,addr,size) \
        ((addr) <= (task)->thread.fs.seg \
	 && ((size) == 0 || (size) - 1 <= (task)->thread.fs.seg - (addr)))

#define __xn_access_ok(task,type,addr,size)  __xn_range_ok(task,(unsigned long)addr,size)

#define XNARCH_MAX_SYSENT 255

typedef struct _xnsysent {

    int (*svc)(struct task_struct *task,
	       struct pt_regs *regs);

/* Syscall must run into the Linux domain. */
#define __xn_flag_lostage    0x1
/* Syscall must run into the RTAI domain. */
#define __xn_flag_histage    0x2
/* Shadow syscall; caller must be mapped. */
#define __xn_flag_shadow     0x4
/* Context-agnostic syscall. */
#define __xn_flag_anycall    0x0
/* Short-hand for shadow initializing syscall. */
#define __xn_flag_init       __xn_flag_lostage
/* Short-hand for pure shadow syscall in RTAI space. */
#define __xn_flag_regular   (__xn_flag_shadow|__xn_flag_histage)

    u_long flags;

} xnsysent_t;

extern int nkgkptd;

#define xnshadow_ptd(t)    ((t)->ptd[nkgkptd])
#define xnshadow_thread(t) ((xnthread_t *)xnshadow_ptd(t))

#endif /* __KERNEL__ */

/*
 * Some of the following macros have been adapted from Linux's
 * implementation of the syscall mechanism in <asm-ppc/unistd.h>:
 *
 * The following code defines an inline syscall mechanism used by
 * Xenomai's real-time interfaces to invoke the skin module services
 * in kernel space.
 */

/* Xenomai multiplexer syscall. */
#define __xn_sys_mux    555
/* Xenomai nucleus syscalls. */
#define __xn_sys_attach  0	/* muxid = xnshadow_attach_skin(magic,infp) */
#define __xn_sys_detach  1	/* xnshadow_detach_skin(muxid) */
#define __xn_sys_sched   2	/* xnpod_schedule(void) */
#define __xn_sys_sync    3	/* xnshadow_sync(&syncflag) */
#define __xn_sys_migrate 4	/* switched = xnshadow_relax/harden() */

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
	__sc_3;							\
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

typedef struct xnsysinfo {

    unsigned long long cpufreq;	/* CPU frequency */
    unsigned long tickval;	/* Tick duration (ns) */

} xnsysinfo_t;

typedef struct xninquiry {

    char name[32];
    int prio;
    unsigned long status;
    void *khandle;
    void *uhandle;

} xninquiry_t;

struct task_struct;

#endif /* !_RTAI_ASM_PPC_SYSCALL_H */
