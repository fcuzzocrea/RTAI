/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 * USA; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <rtai_config.h> 

/*
 * r3 = out_kspp, r4 = in_kspp
 * r5 = out_proc, r6 = in_proc
 */
_GLOBAL(rthal_switch_context)

	stwu	r1,-108(r1)

	/* Save general purpose registers. */
	stw	r31,0*4(r1)
	stw	r30,1*4(r1)
	stw	r29,2*4(r1)
	stw	r28,3*4(r1)
	stw	r27,4*4(r1)
	stw	r26,5*4(r1)
	stw	r25,6*4(r1)
	stw	r24,7*4(r1)
	stw	r23,8*4(r1)
	stw	r22,9*4(r1)
	stw	r21,10*4(r1)
	stw	r20,11*4(r1)
	stw	r19,12*4(r1)
	stw	r18,13*4(r1)
	stw	r17,14*4(r1)
	stw	r16,15*4(r1)
	stw	r15,16*4(r1)
	stw	r14,17*4(r1)
	stw	r13,18*4(r1)
	stw	 r3,19*4(r1)
	stw	 r2,20*4(r1)
	stw	 r0,21*4(r1)

	/* Save special registers. */
	mfctr	 r2
	stw	 r2,22*4(r1)
	mfcr	 r2
	stw	 r2,23*4(r1)
	mfxer	 r2
	stw	 r2,24*4(r1)
	mflr	 r2
	stw	 r2,25*4(r1)
	mfmsr	 r2
	rlwinm	 r2,r2,0,17,15	/* Interrupts off now and upon return. */
	mtmsr	 r2
	stw	 r2,26*4(r1)

	/* Actual switch block. */
	stw	 r1,0(r3)	/* *out_kspp = sp */
	lwz	 r1,0(r4)	/* sp = *in_kspp */

	/* Restore special registers. */
	lwz	 r2,26*4(r1)
	mtmsr	 r2
	lwz	 r2,25*4(r1)
	mtlr	 r2
	lwz	 r2,24*4(r1)
	mtxer	 r2
	lwz	 r2,23*4(r1)
	mtcr	 r2
	lwz	 r2,22*4(r1)
	mtctr	 r2

	/* Restore general purpose registers. */
	lwz	 r0,21*4(r1)
	lwz	 r2,20*4(r1)
	lwz	 r3,19*4(r1)
	lwz	r13,18*4(r1)
	lwz	r14,17*4(r1)
	lwz	r15,16*4(r1)
	lwz	r16,15*4(r1)
	lwz	r17,14*4(r1)
	lwz	r18,13*4(r1)
	lwz	r19,12*4(r1)
	lwz	r20,11*4(r1)
	lwz	r21,10*4(r1)
	lwz	r22,9*4(r1)
	lwz	r23,8*4(r1)
	lwz	r24,7*4(r1)
	lwz	r25,6*4(r1)
	lwz	r26,5*4(r1)
	lwz	r27,4*4(r1)
	lwz	r28,3*4(r1)
	lwz	r29,2*4(r1)
	lwz	r30,1*4(r1)
	lwz	r31,0*4(r1)

	addi	r1,r1,108

	blr
