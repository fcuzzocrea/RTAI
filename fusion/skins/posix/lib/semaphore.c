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

#include <errno.h>
#include <posix/syscall.h>
#include <posix/lib/semaphore.h>

extern int __pse51_muxid;

int __wrap_sem_init (sem_t *sem,
		     int pshared,
		     unsigned value)
{
    union __fusion_semaphore *_sem = (union __fusion_semaphore *)sem;
    int err;

    err = -XENOMAI_SKINCALL3(__pse51_muxid,
			     __pse51_sem_init,
			     &_sem->shadow_sem.handle,
			     pshared,
			     value);
    if (!err)
	{
	_sem->shadow_sem.magic = SHADOW_SEMAPHORE_MAGIC;
	return 0;
	}

    errno = err;

    return -1;
}

int __wrap_sem_destroy (sem_t *sem)

{
    union __fusion_semaphore *_sem = (union __fusion_semaphore *)sem;
    int err;

    if (_sem->shadow_sem.magic != SHADOW_SEMAPHORE_MAGIC)
	return EINVAL;

    err = -XENOMAI_SKINCALL1(__pse51_muxid,
			     __pse51_sem_destroy,
			     _sem->shadow_sem.handle);
    if (!err)
	return 0;

    errno = err;

    return -1;
}

int __wrap_sem_post (sem_t *sem)

{
    union __fusion_semaphore *_sem = (union __fusion_semaphore *)sem;
    int err;

    if (_sem->shadow_sem.magic != SHADOW_SEMAPHORE_MAGIC)
	return EINVAL;

    err = -XENOMAI_SKINCALL1(__pse51_muxid,
			     __pse51_sem_post,
			     _sem->shadow_sem.handle);
    if (!err)
	return 0;

    errno = err;

    return -1;
}

int __wrap_sem_wait (sem_t *sem)

{
    union __fusion_semaphore *_sem = (union __fusion_semaphore *)sem;
    int err;

    if (_sem->shadow_sem.magic != SHADOW_SEMAPHORE_MAGIC)
	return EINVAL;

    err = -XENOMAI_SKINCALL1(__pse51_muxid,
			     __pse51_sem_wait,
			     _sem->shadow_sem.handle);
    if (!err)
	return 0;

    errno = err;

    return -1;
}
