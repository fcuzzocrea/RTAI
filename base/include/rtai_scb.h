/**
 * @ingroup shm
 * @file
 *
 * SCB stand for Shared (memory) Circular Buffer. It is a non blocking
 * implementation for just a single writer (producer) and reader
 * (consumer) and, under such a constraint, it can be a specific
 * substitute for RTAI mailboxes. There are other constraints that
 * must be satisfied so it cannot be a general substitute for the more
 * flexible RTAI mailboxes. In fact it provides just functions
 * corresponding to RTAI mailboxes non blocking atomic send/receive of
 * messages, i.e. the equivalents of rt_mbx_send_if and
 * rt_mbx_receive_if. Moreover the circular buffer size must be >= to
 * the largest message to be sent/received. Thus sending/receiving a
 * message either succeeds of fails.  However thanks to the use of
 * shared memory it should be more efficient than mailboxes in atomic
 * exchanges of messages from kernel to user space. So it is a good
 * candidate for supporting drivers development.
 *
 * @author Paolo Mantegazza
 *
 * @note Copyright &copy; 2004 Paolo Mantegazza <mantegazza@aero.polimi.it>
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

#ifndef _RTAI_SCB_H
#define _RTAI_SCB_H

#include <rtai_shm.h>
#include <asm/rtai_atomic.h>

#define SCB     ((void *)(scb))
#define SIZE    ((volatile int *)scb)[-3]
#define FBYTE   ((volatile int *)scb)[-2]
#define LBYTE   ((volatile int *)scb)[-1]
#define HDRSIZ  (3*sizeof(int))

struct task_struct;

/**
 * Allocate and initialize a shared memory circular buffer.
 *
 * @internal
 *
 * rt_scb_init is used to allocate and initialize a shared memory circular buffer.
 *
 * @param name is an unsigned long identifier;
 *
 * @param size is the size of the circular buffer.
 *
 * @param suprt is the kernel allocation method to be used, it can be:
 * - USE_VMALLOC, use vmalloc;
 * - USE_GFP_KERNEL, use kmalloc with GFP_KERNEL;
 * - USE_GFP_ATOMIC, use kmalloc with GFP_ATOMIC;
 * - USE_GFP_DMA, use kmalloc with GFP_DMA.
 * - for use in kernel only applications the user can use "suprt" to pass 
 *   the address of any memory area (s)he has allocated on her/his own.
 *
 * Since @a name can be a clumsy identifier, services are provided to
 * convert 6 characters identifiers to unsigned long, and vice versa.
 *
 * @see nam2num() and num2nam().
 *
 * It must be remarked that only the very first call does a real allocation,
 * any following call to allocate with the same name from anywhere will just
 * increase the usage count and maps the circular buffer to the user space, or 
 * return the related pointer to the already allocated buffer in kernel space.
 * In any case the functions return a pointer to the circular buffer,
 * appropriately mapped to the memory space in use. So if one is really sure
 * that the named circular buffer has been initted already parameters size
 * and suprt are not used and can be assigned any value.
 *
 * @returns a valid address on succes, 0 on failure.
 *
 */

RTAI_PROTO(void *, rt_scb_init, (unsigned long name, int size, unsigned long suprt))
{	
	void *scb;
	scb = suprt > 1000 ? (void *)suprt : rt_shm_alloc(name, size + HDRSIZ + 1, suprt);
	if (scb && !atomic_cmpxchg((atomic_t *)scb, 0, name)) {
		((int *)scb)[1] = ((int *)scb)[2] = 0;
		((int *)scb)[0] = size + 1;
	} else {
		while (!((int *)scb)[0]);
	}			
	return scb ? scb + HDRSIZ : 0;
}

/**
 * Free a shared memory circular buffer.
 *
 * @internal
 *
 * rt_scb_delete is used to release a previously allocated shared memory 
 * circular buffer.
 *
 * @param name is the unsigned long identifier used when the buffer was
 * allocated;
 *
 * Analogously to what done by all the named allocation functions the freeing
 * calls have just the effect of decrementing a usage count, unmapping any
 * user space shared memory being freed, till the last is done, as that is the
 * one the really frees any allocated memory.
 *
 * @returns the size of the succesfully freed buffer, 0 on failure.
 *
 * Do not call this function if you provided your own memory to the circular
 * buffer. 
 *
 */

RTAI_PROTO(int, rt_scb_delete, (unsigned long name))
{ 
	return rt_shm_free(name);
}

/**
 * Get the number of bytes avaiable in a shared memory circular buffer.
 *
 * @internal
 *
 * rt_scb_bytes is used to get the number of bytes avaiable in a shared 
 * memory circular buffer.
 *
 * @param scb is the pointer handle returned when the buffer was initted.
 *
 * @returns the available number of bytes.
 *
 */

RTAI_PROTO (int, rt_scb_bytes, (void *scb))
{ 
	int size = SIZE, fbyte = FBYTE, lbyte = LBYTE;
	return (lbyte >= fbyte ? lbyte - fbyte : size + lbyte - fbyte);
}

/**
 * @brief Gets (receives) a message, only if the whole message can be passed 
 * all at once.
 *
 * rt_scb_get tries to atomically receive the message @e msg of @e
 * msg_size bytes from the shared memory circular buffer @e scb.
 * It returns immediately and the caller is never blocked.
 *
 * @return On success, i.e. message got, it returns 0, msg_size on failure.
 *
 */

RTAI_PROTO(int, rt_scb_get, (void *scb, void *msg, int msg_size))
{ 
	int size = SIZE, fbyte = FBYTE, lbyte = LBYTE;
	if (msg_size > 0 && (lbyte >= fbyte ? lbyte - fbyte : size + lbyte - fbyte) >= msg_size) {
		int tocpy;
		if ((tocpy = size - fbyte) > msg_size) {
			memcpy(msg, SCB + fbyte, msg_size);
			FBYTE = fbyte + msg_size;
		} else {
			memcpy(msg, SCB + fbyte, tocpy);
			if ((msg_size -= tocpy)) {
				memcpy(msg + tocpy, SCB, msg_size);
			}
			FBYTE = msg_size;
		}
		return 0;
	}
	return msg_size;
}

/**
 * @brief eavedrops a message.
 *
 * rt_scb_evdrp atomically spies the message @e msg of @e
 * msg_size bytes from the shared memory circular buffer @e scb.
 * It returns immediately and the caller is never blocked. It is like 
 * rt_scb_get but leaves the message in the shared memory circular buffer.
 *
 * @return On success, i.e. message got, it returns 0, msg_size on failure.
 *
 */

RTAI_PROTO(int, rt_scb_evdrp, (void *scb, void *msg, int msg_size))
{ 
	int size = SIZE, fbyte = FBYTE, lbyte = LBYTE;
	if (msg_size > 0 && (lbyte >= fbyte ? lbyte - fbyte : size + lbyte - fbyte) >= msg_size) {
		int tocpy;
		if ((tocpy = size - fbyte) > msg_size) {
			memcpy(msg, SCB + fbyte, msg_size);
		} else {
			memcpy(msg, SCB + fbyte, tocpy);
			if ((msg_size -= tocpy)) {
				memcpy(msg + tocpy, SCB, msg_size);
			}
		}
		return 0;
	}
	return msg_size;
}

/**
 * @brief Puts (sends) a message, only if the whole message can be passed all
 * at once.
 *
 * rt_scb_put tries to atomically send the message @e msg of @e
 * msg_size bytes to the shared memory circular buffer @e scb. 
 * It returns immediately and the caller is never blocked.
 *
 * @return On success, i.e. message put, it returns 0, msg_size on failure.
 *
 */

RTAI_PROTO(int, rt_scb_put, (void *scb, void *msg, int msg_size))
{ 
	int size = SIZE, fbyte = FBYTE, lbyte = LBYTE;
	if (msg_size > 0 && (lbyte >= fbyte ? size - (lbyte - fbyte) : fbyte - lbyte) > msg_size) {
		int tocpy;
		if ((tocpy = size - lbyte) > msg_size) {
			memcpy(SCB + lbyte, msg, msg_size);
			LBYTE = lbyte + msg_size;
		} else {
			memcpy(SCB + lbyte, msg, tocpy);
			if ((msg_size -= tocpy)) {
				memcpy(SCB, msg + tocpy, msg_size);
			}
			LBYTE = msg_size;
		}
		return 0;
	}
	return msg_size;
}

#endif /* _RTAI_SCB_H */
