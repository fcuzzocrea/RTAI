/*
 * Copyright (C) 2004 Philippe Gerum.
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
#include <asm/cputable.h>
#include <asm/page.h>
#include <asm/offsets.h>
#include <asm/ppc_asm.h>
#include <rtai_config.h> 

#define RTHAL_GPSAVE(n, base)	stw n,4*(n)(base)
#define RTHAL_GPSAVE2(n, base)	RTHAL_GPSAVE(n, base); RTHAL_GPSAVE(n+1, base)
#define RTHAL_GPSAVE4(n, base)	RTHAL_GPSAVE2(n, base); RTHAL_GPSAVE2(n+2, base)
#define RTHAL_GPSAVE8(n, base)	RTHAL_GPSAVE4(n, base); RTHAL_GPSAVE4(n+4, base)
#define RTHAL_GPSAVE10(n, base)	RTHAL_GPSAVE8(n, base); RTHAL_GPSAVE2(n+8, base)

#define RTHAL_GPLOAD(n, base)	lwz n,4*(n)(base)
#define RTHAL_GPLOAD2(n, base)	RTHAL_GPLOAD(n, base); RTHAL_GPLOAD(n+1, base)
#define RTHAL_GPLOAD4(n, base)	RTHAL_GPLOAD(n, base); RTHAL_GPLOAD2(n+2, base)
#define RTHAL_GPLOAD8(n, base)	RTHAL_GPLOAD(n, base); RTHAL_GPLOAD4(n+4, base)
#define RTHAL_GPLOAD10(n, base)	RTHAL_GPLOAD(n, base); RTHAL_GPLOAD2(n+8, base)

#define RTHAL_GPSAVEALL(base)	RTHAL_GPSAVE(0, base); RTHAL_GPSAVE2(2, base); \
				RTHAL_GPSAVE(13, base); RTHAL_GPSAVE8(14, base); \
				RTHAL_GPSAVE10(22, base)
#define RTHAL_GPLOADALL(base)	RTHAL_GPLOAD(0, base); RTHAL_GPLOAD2(2, base); \
				RTHAL_GPLOAD(13, base); RTHAL_GPLOAD8(14, base); \
				RTHAL_GPLOAD10(22, base)
/*
 * r3=out_kspp, r4=in_kspp
 */
_GLOBAL(rthal_switch_context)

	stwu	r1,-108(r1)

	/* Save general purpose registers. */
	RTHAL_GPSAVEALL(r1)

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
	stw	 r2,26*4(r1)

	/* Actual switch block. */
	stw	 r1,0(r3)	/* *out_kspp = sp */
#ifdef CONFIG_SMP
	sync
#endif /* CONFIG_SMP */
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
	RTHAL_GPLOADALL(r1)

	addi	r1,r1,108

	blr
