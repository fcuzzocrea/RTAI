/*
 * Copyright (C) 1999-2003 Paolo Mantegazza <mantegazza@aero.polimi.it>
 * extensions for user space modules are jointly copyrighted (2000) with:
 *		Pierre Cloutier <pcloutier@poseidoncontrols.com>,
 *		Steve Papacharalambous <stevep@zentropix.com>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#ifndef _RTAI_ASM_I386_LXRT_H
#define _RTAI_ASM_I386_LXRT_H

#include <linux/version.h>

#include <asm/rtai_vectors.h>

#define LOW  0
#define HIGH 1

union rtai_lxrt_t {

    RTIME rt;
    int i[2];
    void *v[2];
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef __KERNEL__

#include <asm/segment.h>
#include <asm/mmu_context.h>

#define RTAI_LXRT_HANDLER rtai_lxrt_handler

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#define __LXRT_GET_DATASEG(reg) "movl $" STR(__KERNEL_DS) ",%" #reg "\n\t"
#else /* KERNEL_VERSION >= 2.6.0 */
#define __LXRT_GET_DATASEG(reg) "movl $" STR(__USER_DS) ",%" #reg "\n\t"
#endif  /* KERNEL_VERSION < 2.6.0 */

#define DEFINE_LXRT_HANDLER() \
asmlinkage long long rtai_lxrt_invoke(unsigned int lxsrq, void *arg); \
asmlinkage int rtai_lxrt_fastpath(void); \
asmlinkage void RTAI_LXRT_HANDLER(void); \
__asm__( \
	"\n" __ALIGN_STR"\n\t" \
        SYMBOL_NAME_STR(rtai_lxrt_handler) ":\n\t" \
        "cld\n\t" \
        "pushl $0\n\t" \
	"pushl %es\n\t" \
	"pushl %ds\n\t" \
	"pushl %eax\n\t" \
	"pushl %ebp\n\t" \
	"pushl %edi\n\t" \
	"pushl %esi\n\t" \
	"pushl %edx\n\t" \
	"pushl %ecx\n\t" \
        "pushl %ebx\n\t" \
	__LXRT_GET_DATASEG(ebx) \
	"movl %ebx,%ds\t\n"  \
	"movl %ebx,%es\t\n" \
	"pushl %edx\n\t" \
	"pushl %eax\n\t" \
	"call "SYMBOL_NAME_STR(rtai_lxrt_invoke)"\n\t" \
	"addl $8,%esp;\n\t" \
	"movl %edx,8(%esp);\n\t" \
	"movl %eax,24(%esp);\n\t" \
	"call "SYMBOL_NAME_STR(rtai_lxrt_fastpath)"\n\t" \
        "testl %eax,%eax;\n\t" \
	"jz "SYMBOL_NAME_STR(ret_from_intr)"\n\t" \
	"popl %ebx\n\t" \
	"popl %ecx\n\t" \
        "popl %edx\n\t" \
	"popl %esi\n\t" \
	"popl %edi\n\t" \
	"popl %ebp\n\t" \
        "popl %eax\n\t" \
	"popl %ds\n\t" \
	"popl %es\n\t" \
	"addl $4,%esp\n\t" \
	"iret\n\t")

static inline void _lxrt_context_switch (struct task_struct *prev,
					struct task_struct *next,
					int cpuid)
{
    struct mm_struct *oldmm = prev->active_mm;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
    switch_mm(oldmm,next->active_mm,next,cpuid);

    if (!next->mm)
	enter_lazy_tlb(oldmm,next,cpuid);
#else /* >= 2.6.0 */
    switch_mm(oldmm,next->active_mm,next);

    if (!next->mm)
        enter_lazy_tlb(oldmm,next);
#endif /* < 2.6.0 */

/* NOTE: Do not use switch_to() directly: this is a compiler
   compatibility issue. */

    __asm__ __volatile__(						\
		 "pushfl\n\t"				       		\
		 "cli\n\t"				       		\
		 "pushl %%esi\n\t"				        \
		 "pushl %%edi\n\t"					\
		 "pushl %%ebp\n\t"					\
		 "movl %%esp,%0\n\t"	/* save ESP */		\
		 "movl %3,%%esp\n\t"	/* restore ESP */		\
		 "movl $1f,%1\n\t"	/* save EIP */		\
		 "pushl %4\n\t"		/* restore EIP */		\
		 "jmp "SYMBOL_NAME_STR(__switch_to)"\n"			\
		 "1:\t"							\
		 "popl %%ebp\n\t"					\
		 "popl %%edi\n\t"					\
		 "popl %%esi\n\t"					\
		 "popfl\n\t"						\
		 :"=m" (prev->thread.esp),"=m" (prev->thread.eip),	\
		  "=b" (prev)						\
		 :"m" (next->thread.esp),"m" (next->thread.eip),	\
		  "a" (prev), "d" (next),				\
		  "b" (prev));						

    barrier();
}

#endif /* __KERNEL__ */

/* NOTE: Keep the following routines unfold: this is a compiler
   compatibility issue. */

static union rtai_lxrt_t _rtai_lxrt(int srq, void *arg)
{
    union rtai_lxrt_t retval;
    RTAI_DO_TRAP(RTAI_LXRT_VECTOR,retval,srq,arg);
    return retval;
}

static inline union rtai_lxrt_t rtai_lxrt(short int dynx, short int lsize, int srq, void *arg)
{
    return _rtai_lxrt((dynx << 28) | ((srq & 0xFFF) << 16) | lsize, arg);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !_RTAI_ASM_I386_LXRT_H */
