/*
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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

#ifndef _RTAI_ASM_MIPS_ATOMIC_H
#define _RTAI_ASM_MIPS_ATOMIC_H

#ifdef __KERNEL__

#include <linux/bitops.h>
#include <asm/system.h>

#define atomic_xchg(ptr,v)      xchg(ptr,v)

/* Poor man's cmpxchg(). */
#define atomic_cmpxchg(p, o, n) ({	\
	typeof(*(p)) __o = (o);		\
	typeof(*(p)) __n = (n);		\
	typeof(*(p)) __prev;		\
	unsigned long flags;		\
	hard_save_flags_and_cli(flags);	\
	__prev = *(p);			\
	if (__prev == __o)		\
		*(p) = __n;		\
	hard_restore_flags(flags);	\
	__prev; })

#endif /* __KERNEL__ */

#endif /* !_RTAI_ASM_MIPS_ATOMIC_H */
