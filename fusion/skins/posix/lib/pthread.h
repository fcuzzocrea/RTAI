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

#ifndef _RTAI_POSIX_PTHREAD_H
#define _RTAI_POSIX_PTHREAD_H

#include_next <pthread.h>

union __fusion_mutex {
    pthread_mutex_t native_mutex;
    struct __shadow_mutex {
#define SHADOW_MUTEX_MAGIC 0x0d140518
	unsigned magic;
	unsigned long handle;
    } shadow_mutex;
};

#undef PTHREAD_MUTEX_INITIALIZER
#define PTHREAD_MUTEX_INITIALIZER {~SHADOW_MUTEX_MAGIC,0}

union __fusion_cond {
    pthread_cond_t native_cond;
    struct __shadow_cond {
#define SHADOW_COND_MAGIC 0x030f0e04
	unsigned magic;
	unsigned long handle;
    } shadow_cond;
};

#undef PTHREAD_COND_INITIALIZER
#define PTHREAD_COND_INITIALIZER {~SHADOW_COND_MAGIC,0}

#ifdef __cplusplus
extern "C" {
#endif

int pthread_make_periodic_np(pthread_t thread,
			     struct timespec *starttp,
			     struct timespec *periodtp);

int pthread_wait_np(void);

int pthread_create_unwrapped(pthread_t *tid,
			     const pthread_attr_t *attr,
			     void *(*start) (void *),
			     void *arg);

int pthread_mutex_init_unwrapped(pthread_mutex_t *mutex,
				 const pthread_mutexattr_t *attr);

#ifdef __cplusplus
}
#endif

#endif /* _RTAI_POSIX_PTHREAD_H */
