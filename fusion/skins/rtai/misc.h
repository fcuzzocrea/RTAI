/**
 * @file
 * This file is part of the RTAI project.
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

#ifndef _RTAI_MISC_H
#define _RTAI_MISC_H

#include <rtai/types.h>

#if !defined(__KERNEL__)  && !defined(__RTAI_SIM__)

#ifdef __cplusplus
extern "C" {
#endif

/* Public interface. */

int rt_misc_get_io_region(unsigned long start,
			  unsigned long len,
			  const char *label);

int rt_misc_put_io_region(unsigned long start,
			  unsigned long len);

#ifdef __cplusplus
}
#endif

#endif /* !(__KERNEL__ || __RTAI_SIM__) */

#endif /* !_RTAI_MISC_H */
