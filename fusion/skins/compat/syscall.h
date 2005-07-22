/**
 *
 * @note Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org> 
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

#ifndef _COMPAT_SYSCALL_H
#define _COMPAT_SYSCALL_H

#include <compat/types.h>
#include <nucleus/asm/syscall.h>

#define __compat_nop         0

#ifdef __KERNEL__

int __compat_syscall_init(void);

void __compat_syscall_cleanup(void);

#endif /* __KERNEL__ */

#endif /* !_COMPAT_SYSCALL_H */
