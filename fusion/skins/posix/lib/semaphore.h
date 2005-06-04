/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _RTAI_POSIX_SEMAPHORE_H
#define _RTAI_POSIX_SEMAPHORE_H

#include_next <semaphore.h>

union __fusion_semaphore {
    sem_t native_sem;
    struct __shadow_sem {
#define SHADOW_SEMAPHORE_MAGIC 0x13010d01
	unsigned magic;
	unsigned long handle;
    } shadow_sem;
};

#endif /* _RTAI_POSIX_SEMAPHORE_H */
