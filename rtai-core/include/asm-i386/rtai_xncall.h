/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _RTAI_ASM_I386_XNCALL_H
#define _RTAI_ASM_I386_XNCALL_H

#include <asm/ptrace.h>

#ifdef __KERNEL__

#include <asm/uaccess.h>
#include <rtai_wrappers.h>

#define __xn_copy_from_user(task,dstP,srcP,n)  __copy_from_user(dstP,srcP,n)
#define __xn_copy_to_user(task,dstP,srcP,n)    __copy_to_user(dstP,srcP,n)
#define __xn_put_user(task,src,dstP)           __put_user(src,dstP)
#define __xn_get_user(task,dst,srcP)           __get_user(dst,srcP)

#define __xn_range_ok(task,addr,size) ({ \
	unsigned long flag,sum; \
	asm("addl %3,%1 ; sbbl %0,%0; cmpl %1,%4; sbbl $0,%0" \
		:"=&r" (flag), "=r" (sum) \
	        :"1" (addr),"g" ((int)(size)),"g" get_tsk_addr_limit(task)); \
	flag; })

/* WP bit must work for using the shadow support, so we only need
   trivial range checking here. */
#define __xn_access_ok(task,type,addr,size)    (__xn_range_ok(task,addr,size) == 0)

#define XNARCH_MAX_SYSENT 255

typedef struct _xnsysent {

    int (*svc)(struct task_struct *task,
	       struct pt_regs *regs);

#define __xn_flag_suspensive 0x1
#define __xn_flag_anycontext 0x2

    u_long flags;

} xnsysent_t;

extern int nkgkptd;

#define xnshadow_ptd(t)    ((t)->ptd[nkgkptd])
#define xnshadow_thread(t) ((xnthread_t *)xnshadow_ptd(t))

#endif /* __KERNEL__ */

/*
 * Some of the following macros have been adapted from glibc's syscall
 * mechanism implementation:
 * Copyright (C) 1992,1993,1995-2000,2002,2003 Free Software Foundation, Inc.
 * Contributed by Ulrich Drepper, <drepper@gnu.org>, August 1995.
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

asm (".L__X'%ebx = 1\n\t"
     ".L__X'%ecx = 2\n\t"
     ".L__X'%edx = 2\n\t"
     ".L__X'%eax = 3\n\t"
     ".L__X'%esi = 3\n\t"
     ".L__X'%edi = 3\n\t"
     ".L__X'%ebp = 3\n\t"
     ".L__X'%esp = 3\n\t"
     ".macro bpushl name reg\n\t"
     ".if 1 - \\name\n\t"
     ".if 2 - \\name\n\t"
     "pushl %ebx\n\t"
     ".else\n\t"
     "xchgl \\reg, %ebx\n\t"
     ".endif\n\t"
     ".endif\n\t"
     ".endm\n\t"
     ".macro bpopl name reg\n\t"
     ".if 1 - \\name\n\t"
     ".if 2 - \\name\n\t"
     "popl %ebx\n\t"
     ".else\n\t"
     "xchgl \\reg, %ebx\n\t"
     ".endif\n\t"
     ".endif\n\t"
     ".endm\n\t"
     ".macro bmovl name reg\n\t"
     ".if 1 - \\name\n\t"
     ".if 2 - \\name\n\t"
     "movl \\reg, %ebx\n\t"
     ".endif\n\t"
     ".endif\n\t"
     ".endm\n\t");

#define XENOMAI_SYS_MUX(nr, op, args...) \
  ({								      \
    unsigned resultvar;						      \
    asm volatile (						      \
    LOADARGS_##nr						      \
    "movl %1, %%eax\n\t"					      \
    "int $0x80\n\t"						      \
    RESTOREARGS_##nr						      \
    : "=a" (resultvar)						      \
    : "i" (__xn_mux_code(0,op)) ASMFMT_##nr(args) : "memory", "cc");  \
    (int) resultvar; })

#define XENOMAI_SKIN_MUX(nr, id, op, args...) \
  ({								      \
    int muxcode = __xn_mux_code(id,op);                               \
    unsigned resultvar;						      \
    asm volatile (						      \
    LOADARGS_##nr						      \
    "movl %1, %%eax\n\t"					      \
    "int $0x80\n\t"						      \
    RESTOREARGS_##nr						      \
    : "=a" (resultvar)						      \
    : "m" (muxcode) ASMFMT_##nr(args) : "memory", "cc");	      \
    (int) resultvar; })

#define LOADARGS_0
#define LOADARGS_1 \
    "bpushl .L__X'%k2, %k2\n\t" \
    "bmovl .L__X'%k2, %k2\n\t"
#define LOADARGS_2	LOADARGS_1
#define LOADARGS_3	LOADARGS_1
#define LOADARGS_4	LOADARGS_1
#define LOADARGS_5	LOADARGS_1

#define RESTOREARGS_0
#define RESTOREARGS_1 \
    "bpopl .L__X'%k2, %k2\n\t"
#define RESTOREARGS_2	RESTOREARGS_1
#define RESTOREARGS_3	RESTOREARGS_1
#define RESTOREARGS_4	RESTOREARGS_1
#define RESTOREARGS_5	RESTOREARGS_1

#define ASMFMT_0()
#define ASMFMT_1(arg1) \
	, "acdSD" (arg1)
#define ASMFMT_2(arg1, arg2) \
	, "adSD" (arg1), "c" (arg2)
#define ASMFMT_3(arg1, arg2, arg3) \
	, "aSD" (arg1), "c" (arg2), "d" (arg3)
#define ASMFMT_4(arg1, arg2, arg3, arg4) \
	, "aD" (arg1), "c" (arg2), "d" (arg3), "S" (arg4)
#define ASMFMT_5(arg1, arg2, arg3, arg4, arg5) \
	, "a" (arg1), "c" (arg2), "d" (arg3), "S" (arg4), "D" (arg5)

/* Register mapping for accessing syscall args. */

#define __xn_reg_mux(regs)    ((regs)->orig_eax)
#define __xn_reg_rval(regs)   ((regs)->eax)
#define __xn_reg_arg1(regs)   ((regs)->ebx)
#define __xn_reg_arg2(regs)   ((regs)->ecx)
#define __xn_reg_arg3(regs)   ((regs)->edx)
#define __xn_reg_arg4(regs)   ((regs)->esi)
#define __xn_reg_arg5(regs)   ((regs)->edi)

#define __xn_reg_mux_p(regs)        ((__xn_reg_mux(regs) & 0xffff) == __xn_sys_mux)
#define __xn_mux_id(regs)           ((__xn_reg_mux(regs) >> 16) & 0xff)
#define __xn_mux_op(regs)           ((__xn_reg_mux(regs) >> 24) & 0xff)
#define __xn_mux_code(id,op)        ((op << 24)|((id << 16) & 0xff0000)|(__xn_sys_mux & 0xffff))

#define XENOMAI_SYSCALL0(op)                XENOMAI_SYS_MUX(0,op)
#define XENOMAI_SYSCALL1(op,a1)             XENOMAI_SYS_MUX(1,op,a1)
#define XENOMAI_SYSCALL2(op,a1,a2)          XENOMAI_SYS_MUX(2,op,a1,a2)
#define XENOMAI_SYSCALL3(op,a1,a2,a3)       XENOMAI_SYS_MUX(3,op,a1,a2,a3)
#define XENOMAI_SYSCALL4(op,a1,a2,a3,a4)    XENOMAI_SYS_MUX(4,op,a1,a2,a3,a4)
#define XENOMAI_SYSCALL5(op,a1,a2,a3,a4,a5) XENOMAI_SYS_MUX(5,op,a1,a2,a3,a4,a5)

#define XENOMAI_SKINCALL0(id,op)                XENOMAI_SKIN_MUX(0,id,op)
#define XENOMAI_SKINCALL1(id,op,a1)             XENOMAI_SKIN_MUX(1,id,op,a1)
#define XENOMAI_SKINCALL2(id,op,a1,a2)          XENOMAI_SKIN_MUX(2,id,op,a1,a2)
#define XENOMAI_SKINCALL3(id,op,a1,a2,a3)       XENOMAI_SKIN_MUX(3,id,op,a1,a2,a3)
#define XENOMAI_SKINCALL4(id,op,a1,a2,a3,a4)    XENOMAI_SKIN_MUX(4,id,op,a1,a2,a3,a4)
#define XENOMAI_SKINCALL5(id,op,a1,a2,a3,a4,a5) XENOMAI_SKIN_MUX(5,id,op,a1,a2,a3,a4,a5)

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

#endif /* !_RTAI_ASM_I386_XNCALL_H */
