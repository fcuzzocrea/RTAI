/*
 * Copyright (C) 1999-2003 Paolo Mantegazza <mantegazza@aero.polimi.it>
 *		   2000 Pierre Cloutier <pcloutier@poseidoncontrols.com>
		   2002 Steve Papacharalambous <stevep@zentropix.com>
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

#ifndef _RTAI_ASM_PPC_LXRT_H
#define _RTAI_ASM_PPC_LXRT_H

#include <asm/rtai_vectors.h>

#define LOW 1

#ifndef __KERNEL__

union rtai_lxrt_t { RTIME rt; int i[2]; void *v[2]; };

static inline union rtai_lxrt_t rtai_lxrt(short dynx, short lsize, unsigned long srq, void *arg)
{
    /* LXRT is not yet available on PPC. */
    union rtai_lxrt_t retval;
    retval.i[0] = -1;
    retval.i[1] = -1;
    return retval;
}

#endif /* !__KERNEL__ */

#endif /* !_RTAI_ASM_PPC_LXRT_H */
