/* 020207 arm_task_sw.S - Task-switching for ARM-V processors
Copyright (c) 2001 Alex Züpke, SYSGO RTS GmbH (azu@sysgo.de)
COPYRIGHT (C) 2002 Wolfgang Müller (wolfgang.mueller@dsa-ac.de)

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

#include <asm/proc/assembler.h>
#include <asm/proc/ptrace.h>
	
/*
static inline void up_task_sw(	void *,			void *,			void *)
{	//			^^			^^			^^
	//	we expect	r0 = RT_TASK **old	r1 = RT_TASK *new	r2 = &rt_linux_task
}	//	r0..r9, r10=sl, r11=fp, r12=ip, r13=sp, r14=lr, r15=pc
*/
	.globl up_task_sw		@ hacked from kernel ENTRY(__switch_to)
up_task_sw:
	stmfd	sp!, {r0 - sl, fp, lr}	@ push most registers onto stack (scratch ip never)
	mrs	ip, cpsr		@ get current cpsr_SVC and
	str	ip, [sp, #-4]!		@ push it onto stack
	mrc	p15, 0, r4, c3, c0	@ get current domain_access_control and
	str	r4, [sp, #-4]!		@ push it onto stack

	ldr	r4, [r0]		@ load pointer to (RT_TASK) old	from rt_current
	str	sp, [r4]		@ save current sp_SVC in (RT_TASK) old->stack
/*
 *	goodbye old, hello new
 */
	str	r1, [r0]		@ save pointer to (RT_TASK) new to rt_current
	ldr	sp, [r1]		@ load previous sp_SVC from (RT_TASK) new->stack
	ldr	r4, [sp], #4		@ pop previous domain_access_control from stack
	mcr	p15, 0, r4, c3, c0	@ restore previous domain_access_control

	cmp	r2, r1			@ check, if we are switching to linux
	bne	1f
	ldr	r1, .rthal
	mov	lr, pc
	ldr	pc, [r1, #20]		@ rthal.getflags
	tst	r0, #I_BIT
	bne	1f			@ branch, if linux IRQs disabled
	mov	r0, #MODE_SVC
	msr	cpsr_c, r0		@ hard enable interrupts
	mov	lr, pc
	ldr	pc, [r1, #16]		@ rthal.enint()
1:
	mov	r0, #MODE_SVC | I_BIT
	msr	cpsr_c, r0		@ hard disable interrupts
	ldr	ip, [sp], #4		@ pop previous cpsr_SVC from stack
	msr	spsr, ip		@ replace current spsr_SVC with previous cpsr_SVC
	ldmfd	sp!, {r0 - sl, fp, pc}^	@ pop previous registers, pc = previous lr

.rthal:		.word	rthal

/*
 * There is no FPU on SA1100, so we don't care ...
 */
#ifdef CONFIG_RTAI_FP_SUPPORT

	.globl save_fpenv
save_fpenv:
	mov	pc, lr

	.globl restore_fpenv
restore_fpenv:
	mov	pc, lr

#endif
