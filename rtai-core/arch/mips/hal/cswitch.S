/*
 * COPYRIGHT (C) 2001  Steve Papacharalambous (stevep@lineo.com)
 * COPYRIGHT (C) 2001  Paolo Mantegazza (mantegazza@aero.polimi.it)
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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

/*
 * Low level task context switch function for the RTAI UP scheduler.
 */


#include <asm/asm.h>
#include <asm/regdef.h>

#include <asm/rtai_stackframe.h>

	.text
	.set	push
	.set	noreorder
	.set	noat
	.set	nomacro
	.align	4
	NESTED(up_task_sw, RT_SIZE, sp)
	.set	push

	/*
	 * Save the current thread context.
	 */
	subu	sp, RT_SIZE
	sw	$1, RT_R1(sp)
	sw	$2, RT_R2(sp)
	sw	$3, RT_R3(sp)
	sw	$4, RT_R4(sp)
	sw	$5, RT_R5(sp)
	sw	$6, RT_R6(sp)
	sw	$7, RT_R7(sp)
	sw	$8, RT_R8(sp)
	sw	$9, RT_R9(sp)
	sw	$10, RT_R10(sp)
	sw	$11, RT_R11(sp)
	sw	$12, RT_R12(sp)
	sw	$13, RT_R13(sp)
	sw	$14, RT_R14(sp)
	sw	$15, RT_R15(sp)
	sw	$16, RT_R16(sp)
	sw	$17, RT_R17(sp)
	sw	$18, RT_R18(sp)
	sw	$19, RT_R19(sp)
	sw	$20, RT_R20(sp)
	sw	$21, RT_R21(sp)
	sw	$22, RT_R22(sp)
	sw	$23, RT_R23(sp)
	sw	$24, RT_R24(sp)
	sw	$25, RT_R25(sp)
	sw	$26, RT_R26(sp)
	sw	$27, RT_R27(sp)
	sw	$28, RT_R28(sp)
	sw	$29, RT_R29(sp)
	sw	$30, RT_R30(sp)
	sw	$31, RT_R31(sp)

	/*
	 * Switch thread stacks.
	 */
	lw	$8, ($4)		/* Load rt_current RT_TASK *. */
	sw	$29, ($8)		/* Save rt_current thread stack. */
	sw	$5, ($4)		/* Update rt_current. */
	lw	$29, ($5)		/* Load stack for new thread. */

	/*
	 * Restore new thread context.
	 */
	.set	pop
	lw	$1, RT_R1(sp)
	lw	$2, RT_R2(sp)
	lw	$3, RT_R3(sp)
	lw	$4, RT_R4(sp)
	lw	$5, RT_R5(sp)
	lw	$6, RT_R6(sp)
	lw	$7, RT_R7(sp)
	lw	$8, RT_R8(sp)
	lw	$9, RT_R9(sp)
	lw	$10, RT_R10(sp)
	lw	$11, RT_R11(sp)
	lw	$12, RT_R12(sp)
	lw	$13, RT_R13(sp)
	lw	$14, RT_R14(sp)
	lw	$15, RT_R15(sp)
	lw	$16, RT_R16(sp)
	lw	$17, RT_R17(sp)
	lw	$18, RT_R18(sp)
	lw	$19, RT_R19(sp)
	lw	$20, RT_R20(sp)
	lw	$21, RT_R21(sp)
	lw	$22, RT_R22(sp)
	lw	$23, RT_R23(sp)
	lw	$24, RT_R24(sp)
	lw	$25, RT_R25(sp)
	lw	$26, RT_R26(sp)
	lw	$27, RT_R27(sp)
	lw	$28, RT_R28(sp)
	lw	$30, RT_R30(sp)
	lw	$31, RT_R31(sp)
	addu	sp, RT_SIZE

	j	$31
	nop
	.set	at
	.set	reorder
	.set	macro

	END(up_task_sw)

