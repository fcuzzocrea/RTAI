/**
 * @ingroup shm
 * @file
 *
 * Implementation of the @ref shm "RTAI SHM module".
 *
 * @author Paolo Mantegazza
 *
 * @note Copyright &copy; 1999-2004 Paolo Mantegazza <mantegazza@aero.polimi.it>
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


// the "_new" in some of the functions below is temporary, to avoid
// clashing with the very same functions in malloc.c, it will disappear 
// when shm.c will become the only real time memory management support
// (using xnheap_alloc/free, in xenomai/heap.c).

/**
 * @defgroup shm Unified RTAI real-time memory management.
 * 
 *@{*/

#define RTAI_SHM_MISC_MINOR  254 // The same minor used to mknod for major 10.

#include <linux/version.h>
#include <linux/module.h>
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>

#include <rtai_trace.h>
#include <rtai_schedcore.h>
#include <rtai_registry.h>
#include "rtai_shm.h"

MODULE_LICENSE("GPL");

#define ALIGN2PAGE(adr)   ((void *)PAGE_ALIGN((unsigned long)adr))
#define RT_SHM_OP_PERM()  (!(_rt_whoami()->is_hard))

static int SUPRT[] = { 0, GFP_KERNEL, GFP_ATOMIC, GFP_DMA };

static inline void *_rt_shm_alloc(unsigned long name, int size, int suprt)
{
	void *adr;

//	suprt = USE_GFP_ATOMIC; // to force some testing
	if (!(adr = rt_get_adr_cnt(name)) && size > 0 && suprt >= 0 && RT_SHM_OP_PERM()) {
		size = ((size - 1) & PAGE_MASK) + PAGE_SIZE;
		if ((adr = suprt ? rkmalloc(&size, SUPRT[suprt]) : rvmalloc(size))) {
			if (!rt_register(name, adr, suprt ? -size : size, 0)) {
				if (suprt) {
                                        rkfree(adr, size);
                                } else {
                                        rvfree(adr, size);
                                }
				return 0;
			}
			memset(ALIGN2PAGE(adr), 0, size);
		}
	}
	return ALIGN2PAGE(adr);
}

static inline int _rt_shm_free(unsigned long name, int size)
{
	void *adr;

	if (size && (adr = rt_get_adr(name))) {
		if (RT_SHM_OP_PERM()) {
			if (!rt_drg_on_name_cnt(name)) {
				if (size < 0) {
					rkfree(adr, -size);
				} else {
					rvfree(adr, size);
				}
			}
		}
		return abs(size);
	}
	return 0;
}

/**
 * Allocate a chunk of memory to be shared inter-intra kernel modules and Linux
 * processes.
 *
 * @internal
 * 
 * rt_shm_alloc is used to allocate shared memory.
 * 
 * @param name is an unsigned long identifier;
 * 
 * @param size is the amount of required shared memory;
 * 
 * @param suprt is the kernel allocation method to be used, it can be:
 * - USE_VMALLOC, use vmalloc;
 * - USE_GFP_KERNEL, use kmalloc with GFP_KERNEL;
 * - USE_GFP_ATOMIC, use kmalloc with GFP_ATOMIC;
 * - USE_GFP_DMA, use kmalloc with GFP_DMA.
 * 
 * Since @a name can be a clumsy identifier, services are provided to
 * convert 6 characters identifiers to unsigned long, and vice versa.
 * 
 * @see nam2num() and num2nam().
 * 
 * It must be remarked that only the very first call does a real allocation, 
 * any following call to allocate with the same name from anywhere will just 
 * increase the usage count and maps the area to the user space, or return 
 * the related pointer to the already allocated space in kernel space. 
 * In any case the functions return a pointer to the allocated memory, 
 * appropriately mapped to the memory space in use. So if one is really sure
 * that the named shared memory has been allocated already parameters size 
 * and suprt are not used and can be assigned any value.
 *
 * @returns a valid address on succes, 0 on failure.
 *
 */

void *rt_shm_alloc(unsigned long name, int size, int suprt)
{
	TRACE_RTAI_SHM(TRACE_RTAI_EV_SHM_KMALLOC, name, size, 0);
	return _rt_shm_alloc(name, size, suprt);
}

static int rt_shm_alloc_usp(unsigned long name, int size, int suprt)
{
	TRACE_RTAI_SHM(TRACE_RTAI_EV_SHM_MALLOC, name, size, current->pid);

	if (_rt_shm_alloc(name, size, suprt)) {
		((current->mm)->mmap)->vm_private_data = (void *)name;
		return abs(rt_get_type(name));
	}
	return 0;
}

/**
 * Free a chunk of shared memory being shared inter-intra kernel modules and
 * Linux processes.
 *
 * @internal
 * 
 * rt_shm_free is used to free a previously allocated shared memory.
 *
 * @param name is the unsigned long identifier used when the memory was
 * allocated;
 *
 * Analogously to what done by all the named allocation functions the freeing 
 * calls have just the effect of decrementing a usage count, unmapping any 
 * user space shared memory being freed, till the last is done, as that is the 
 * one the really frees any allocated memory.
 *
 * @returns the size of the succesfully freed memory, 0 on failure.
 *
 */

int rt_shm_free(unsigned long name)
{
	TRACE_RTAI_SHM(TRACE_RTAI_EV_SHM_KFREE, name, 0, 0);
	return _rt_shm_free(name, rt_get_type(name));
}

static int rt_shm_size(unsigned long *arg)
{
	int size;
	struct vm_area_struct *vma;

	size = abs(rt_get_type(*arg));
	for (vma = (current->mm)->mmap; vma; vma = vma->vm_next) {
		if (vma->vm_private_data == (void *)arg && (vma->vm_end - vma->vm_start) == size) {
			*arg = vma->vm_start;
			return size;
		}
	}
	return 0;
}

static void rtai_shm_vm_open(struct vm_area_struct *vma)
{
	rt_get_adr_cnt((unsigned long)vma->vm_private_data);
}

static void rtai_shm_vm_close(struct vm_area_struct *vma)
{
	_rt_shm_free((unsigned long)vma->vm_private_data, rt_get_type((unsigned long)vma->vm_private_data));
}

static struct vm_operations_struct rtai_shm_vm_ops = {
	open:  	rtai_shm_vm_open,
	close: 	rtai_shm_vm_close
};

static void rt_set_heap(unsigned long, void *);

static int rtai_shm_f_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
		case SHM_ALLOC: {
			TRACE_RTAI_SHM(TRACE_RTAI_EV_SHM_MALLOC, ((unsigned long *)arg)[0], cmd, current->pid);
			return rt_shm_alloc_usp(((unsigned long *)arg)[0], ((int *)arg)[1], ((int *)arg)[2]);
		}
		case SHM_FREE: {
			TRACE_RTAI_SHM(TRACE_RTAI_EV_SHM_FREE, arg, cmd, current->pid);
			return _rt_shm_free(arg, rt_get_type(arg));	
		}
		case SHM_SIZE: {
			TRACE_RTAI_SHM(TRACE_RTAI_EV_SHM_GET_SIZE, arg, cmd, current->pid);
			return rt_shm_size((unsigned long *)((unsigned long *)arg)[0]);
		}
		case HEAP_SET: {
			rt_set_heap(((unsigned long *)arg)[0], (void *)((unsigned long *)arg)[1]);
			return 0;
		}
	}
	return 0;
}

static int rtai_shm_f_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long name;
	int size;
	if(!vma->vm_ops) {
		vma->vm_ops = &rtai_shm_vm_ops;
		vma->vm_flags |= VM_LOCKED;
		name = (unsigned long)(vma->vm_private_data = ((current->mm)->mmap)->vm_private_data);
		((current->mm)->mmap)->vm_private_data = NULL;
		return (size = rt_get_type(name)) < 0 ? rkmmap(ALIGN2PAGE(rt_get_adr(name)), -size, vma) : rvmmap(rt_get_adr(name), size, vma);
	}
	return -EFAULT;
}

static struct file_operations rtai_shm_fops = {
	ioctl:	rtai_shm_f_ioctl,
	mmap:	rtai_shm_f_mmap
};

static struct miscdevice rtai_shm_dev = 
	{ RTAI_SHM_MISC_MINOR, "RTAI_SHM", &rtai_shm_fops, NULL, NULL };

/*
 * core functions taken from RTAI malloc.c, restyled a bit
 */

#define MINSIZE   16
#define OVERHEAD  (sizeof(struct chkhdr) + sizeof(struct blkhdr))

struct chkhdr {
	void *chk_addr;
	struct chkhdr *next_chk;
	struct blkhdr *free_list;
	int chk_limit;
	char data[0];
};

struct blkhdr {
	void *chk_addr;
	struct chkhdr *just2algn;
	struct blkhdr *next_free;
	int free_size;
	char data[0];
};

static inline void *alloc_heap_chunk(struct chkhdr *chk, int size)
{
	struct blkhdr *cur, *next, *prev = NULL;

	for (cur = chk->free_list; cur; prev = cur, cur = cur->next_free) {
		if(cur->free_size >= size) {
			if ((cur->free_size - size) >= (sizeof(struct blkhdr) + MINSIZE)) {
				next = (struct blkhdr *)(cur->data + size);
				next->chk_addr  = chk->chk_addr;
				next->next_free = cur->next_free;
				next->free_size = cur->free_size - (size + sizeof(struct blkhdr));
			} else {
				next = cur->next_free;
			}
			if (!prev) {
				chk->free_list  = next;
			} else {
				prev->next_free = next;
			}
			cur->next_free = cur;
			cur->free_size = size;
			return cur->data;
		}
	}
	return NULL;
}

static inline void *_rt_halloc(int size, struct rt_heap_t *heap)
{
	unsigned long flags;
	struct chkhdr *chk_list_ptr;
	void *mem_ptr = NULL;

	size = (size + (MINSIZE - 1)) & ~(MINSIZE - 1);
	if (size > (heap->hsize - OVERHEAD)) {
		return NULL;
	}
	chk_list_ptr = heap->hkadr;
	flags = rt_spin_lock_irqsave(heap->hlock);
	for ( ; chk_list_ptr; chk_list_ptr = chk_list_ptr->next_chk) {
		if (chk_list_ptr->free_list && (mem_ptr = alloc_heap_chunk(chk_list_ptr, size))) {
			mem_ptr = heap->huadr + (mem_ptr - heap->hkadr);
			break;
		}
	}
	rt_spin_unlock_irqrestore(flags, heap->hlock);
	return mem_ptr;
}

static inline void _rt_hfree(void *addr, struct rt_heap_t *heap)
{
	unsigned long flags;
	struct chkhdr *chk_ptr;
	struct blkhdr *blk_ptr, *cur_blk_ptr, *prev_blk_ptr;

	addr = heap->hkadr + (addr - heap->huadr);
	blk_ptr = (struct blkhdr *)(addr - sizeof(struct blkhdr));
	chk_ptr = blk_ptr->chk_addr;
	if (chk_ptr != chk_ptr->chk_addr || blk_ptr->next_free != blk_ptr || !blk_ptr->free_size || ((unsigned long)blk_ptr->free_size) > (heap->hsize - OVERHEAD) || ((unsigned long)blk_ptr->free_size)%MINSIZE) {
		return;
	} else {
		flags = rt_spin_lock_irqsave(heap->hlock);
		cur_blk_ptr  = chk_ptr->free_list;
		prev_blk_ptr = NULL;
		while (cur_blk_ptr && cur_blk_ptr->data < (char *)addr) {
			prev_blk_ptr = cur_blk_ptr;
			cur_blk_ptr  = cur_blk_ptr->next_free;
		}
		if (prev_blk_ptr && ((struct blkhdr *)(prev_blk_ptr->data + prev_blk_ptr->free_size) == blk_ptr)) {
			prev_blk_ptr->free_size += blk_ptr->free_size + sizeof(struct blkhdr);
			blk_ptr = prev_blk_ptr;
		} else if (prev_blk_ptr) {
			blk_ptr->next_free      = prev_blk_ptr->next_free;
			prev_blk_ptr->next_free = blk_ptr;
		} else {
			blk_ptr->next_free = chk_ptr->free_list;
			chk_ptr->free_list = blk_ptr;
		}
		if ((struct blkhdr *)(blk_ptr->data + blk_ptr->free_size) == blk_ptr->next_free) {
			blk_ptr->free_size += blk_ptr->next_free->free_size + sizeof(struct blkhdr);
			blk_ptr->next_free  = blk_ptr->next_free->next_free;
		}
		rt_spin_unlock_irqrestore(flags, heap->hlock);
	}
}

/*
 * end of core functions taken from RTAI malloc.c
 */

#define GLOBAL    0
#define SPECIFIC  1

/**
 * Allocate a chunk of the global real time heap in kernel/user space. Since 
 * it is not named there is no chance of retrieving and sharing it elsewhere.
 *
 * @internal
 * 
 * rt_malloc is used to allocate a non sharable piece of the global real time 
 * heap.
 *
 * @param size is the size of the requested memory in bytes;
 *
 * @returns the pointer to the allocated memory, 0 on failure.
 *
 */

void *rt_malloc_new(int size)
{
	return _rt_halloc(size, &rt_smp_linux_task->heap[GLOBAL]);
}

/**
 * Free a chunk of the global real time heap.
 *
 * @internal
 * 
 * rt_free is used to free a previously allocated chunck of the global real 
 * time heap.
 *
 * @param addr is the address of the memory to be freed.
 *
 */

void rt_free_new(void *addr)
{
	_rt_hfree(addr, &rt_smp_linux_task->heap[GLOBAL]);
}

/**
 * Allocate a chunk of the global real time heap in kernel/user space. Since 
 * it is named it can be retrieved and shared everywhere.
 *
 * @internal
 * 
 * rt_named_malloc is used to allocate a sharable piece of the global real 
 * time heap.
 *
 * @param name is an unsigned long identifier;
 * 
 * @param size is the amount of required shared memory;
 * 
 * Since @a name can be a clumsy identifier, services are provided to
 * convert 6 characters identifiers to unsigned long, and vice versa.
 * 
 * @see nam2num() and num2nam().
 * 
 * It must be remarked that only the very first call does a real allocation,
 * any subsequent call to allocate with the same name will just increase the
 * usage count and return the appropriate pointer to the already allocated 
 * memory having the same name. So if one is really sure that the named chunk 
 * has been allocated already the size parameter is not used and can be 
 * assigned any value.
 *
 * @returns a valid address on succes, 0 on failure.
 *
 */

void *rt_named_malloc(unsigned long name, int size)
{
	void *mem_ptr;

	if ((mem_ptr = rt_get_adr_cnt(name))) {
		return mem_ptr;
	}
	if ((mem_ptr = _rt_halloc(size, &rt_smp_linux_task->heap[GLOBAL]))) {
		if (rt_register(name, mem_ptr, IS_HPCK, 0)) {
                        return mem_ptr;
                }
                rt_hfree(mem_ptr);
	}
	return NULL;
}

/**
 * Free a named chunk of the global real time heap. 
 *
 * @internal
 * 
 * rt_named_free is used to free a previously allocated chunk of the global
 * real time heap.
 *
 * @param adr is the address of the memory to be freed.
 *
 * Analogously to what done by all the named allocation functions the freeing 
 * calls of named memory chunks have just the effect of decrementing its usage
 * count, any shared piece of the global heap being freed only when the last 
 * is done, as that is the one the really frees any allocated memory.
 * So one must be carefull not to use rt_free on a named global heap chunk, 
 * since it will force its unconditional immediate freeing.
 *
 */

void rt_named_free(void *adr)
{
	unsigned long name;

	name = rt_get_name(adr);
	if (!rt_drg_on_name_cnt(name)) {
		_rt_hfree(adr, &rt_smp_linux_task->heap[GLOBAL]);
	}
}

/* 
 * we must care of this because LXRT callable functions are set as non 
 * blocking, so they are called directly.
 */
#define RTAI_TASK(return_instr) \
do { \
	if (!(task = _rt_whoami())->is_hard) { \
		if (!(task = current->this_rt_task[0])) { \
			return_instr; \
		} \
	} \
} while (0)

static inline void *rt_halloc_typed(int size, int htype)
{
	RT_TASK *task;

	RTAI_TASK(return NULL);
	return _rt_halloc(size, &task->heap[htype]);
}

static inline void rt_hfree_typed(void *addr, int htype)
{
	RT_TASK *task;

	RTAI_TASK(return);
	_rt_hfree(addr, &task->heap[htype]);
}

static inline void *rt_named_halloc_typed(unsigned long name, int size, int htype)
{
	RT_TASK *task;
	void *mem_ptr;

	RTAI_TASK(return NULL);
	if ((mem_ptr = rt_get_adr_cnt(name))) {
		return task->heap[htype].huadr + (mem_ptr - task->heap[htype].hkadr);
	}
	if ((mem_ptr = _rt_halloc(size, &task->heap[htype]))) {
		if (rt_register(name, task->heap[htype].hkadr + (mem_ptr - task->heap[htype].huadr), IS_HPCK, 0)) {
                        return mem_ptr;
                }
		_rt_hfree(mem_ptr, &task->heap[htype]);
	}
	return NULL;
}

static inline void rt_named_hfree_typed(void *adr, int htype)
{
	RT_TASK *task;
	unsigned long name;

	RTAI_TASK(return);
	name = rt_get_name(task->heap[htype].hkadr + (adr - task->heap[htype].huadr));
	if (!rt_drg_on_name_cnt(name)) {
		_rt_hfree(adr, &task->heap[htype]);
	}
}

/**
 * Allocate a chunk of a group real time heap in kernel/user space. Since 
 * it is not named there is no chance to retrieve and share it elsewhere.
 *
 * @internal
 * 
 * rt_halloc is used to allocate a non sharable piece of a group real time 
 * heap.
 *
 * @param size is the size of the requested memory in bytes;
 *
 * A process/task must have opened the real time group heap to use and can use
 * just one real time group heap. Be careful and avoid opening more than one 
 * group real time heap per process/task. If more than one is opened then just 
 * the last will used.
 *
 * @returns the pointer to the allocated memory, 0 on failure.
 *
 */

void *rt_halloc(int size)
{
	return rt_halloc_typed(size, SPECIFIC);
}

/**
 * Free a chunk of a group real time heap.
 *
 * @internal
 * 
 * rt_hfree is used to free a previously allocated chunck of a group real 
 * time heap.
 *
 * @param adr is the address of the memory to be freed.
 *
 */

void rt_hfree(void *adr)
{
	rt_hfree_typed(adr, SPECIFIC);
}

/**
 * Allocate a chunk of a group real time heap in kernel/user space. Since 
 * it is named it can be retrieved and shared everywhere among the group 
 * peers, i.e all processes/tasks that have opened the same group heap.
 *
 * @internal
 * 
 * rt_named_halloc is used to allocate a sharable piece of a group real 
 * time heap.
 *
 * @param name is an unsigned long identifier;
 * 
 * @param size is the amount of required shared memory;
 * 
 * Since @a name can be a clumsy identifier, services are provided to
 * convert 6 characters identifiers to unsigned long, and vice versa.
 * 
 * @see nam2num() and num2nam().
 * 
 * A process/task must have opened the real time group heap to use and can use
 * just one real time group heap. Be careful and avoid opening more than one
 * group real time heap per process/task. If more than one is opened then just
 * the last will used. It must be remarked that only the very first call does 
 * a real allocation, any subsequent call with the same name will just 
 * increase the usage count and receive the appropriate pointer to the already
 * allocated memory having the same name.
 *
 * @returns a valid address on succes, 0 on failure.
 *
 */

void *rt_named_halloc(unsigned long name, int size)
{
	return rt_named_halloc_typed(name, size, SPECIFIC);
}

/**
 * Free a chunk of a group real time heap. 
 *
 * @internal
 * 
 * rt_named_hfree is used to free a previously allocated chunk of the global
 * real time heap.
 *
 * @param adr is the address of the memory to be freed.
 *
 * Analogously to what done by all the named allocation functions the freeing 
 * calls of named memory chunks have just the effect of decrementing a usage
 * count, any shared piece of the global heap being freed only when the last 
 * is done, as that is the one the really frees any allocated memory.
 * So one must be carefull not to use rt_hfree on a named global heap chunk, 
 * since it will force its unconditional immediate freeing.
 *
 */

void rt_named_hfree(void *adr)
{
	rt_named_hfree_typed(adr, SPECIFIC);
}

static void *rt_malloc_new_usp(int size)
{
	return rt_halloc_typed(size, GLOBAL);
}

static void rt_free_new_usp(void *adr)
{
	rt_hfree_typed(adr, GLOBAL);
}

static void *rt_named_malloc_usp(unsigned long name, int size)
{
	return rt_named_halloc_typed(name, size, GLOBAL);
}

static void rt_named_free_usp(void *adr)
{
	rt_named_hfree_typed(adr, GLOBAL);
}

static void rt_set_heap(unsigned long name, void *adr)
{
	int size, htype;
	struct chkhdr *chk_ptr;
	struct blkhdr *blk_ptr;
	RT_TASK *task;

	chk_ptr = ALIGN2PAGE(rt_get_adr(name));
	size = abs(rt_get_type(name)) - sizeof(spinlock_t);
	if (!atomic_cmpxchg((int *)chk_ptr, 0, name)) {
		chk_ptr->chk_addr  = (void *)chk_ptr;
		chk_ptr->chk_limit = size;
		chk_ptr->next_chk  = NULL;
		chk_ptr->free_list = 
		blk_ptr            = (struct blkhdr *)chk_ptr->data;
		blk_ptr->next_free = NULL;
		blk_ptr->free_size = size - OVERHEAD;
		blk_ptr->chk_addr  = (void *)chk_ptr;
		spin_lock_init((spinlock_t *)((void *)chk_ptr + size));
		if (name == GLOBAL_HEAP_ID) {
			rt_smp_linux_task->heap[GLOBAL].hkadr = (void *)chk_ptr;
			rt_smp_linux_task->heap[GLOBAL].huadr = adr;
			rt_smp_linux_task->heap[GLOBAL].hsize = size;
			rt_smp_linux_task->heap[GLOBAL].hlock = (spinlock_t *)((void *)chk_ptr + size);
		}
	}
	RTAI_TASK(return);
	htype = name == GLOBAL_HEAP_ID ? GLOBAL : SPECIFIC;
	task->heap[htype].hkadr = (void *)chk_ptr;
	task->heap[htype].huadr = adr;
	task->heap[htype].hsize = size;
	task->heap[htype].hlock = (spinlock_t *)((void *)chk_ptr + size);
}

/**
 * Open/create a named group real time heap to be shared inter-intra kernel 
 * modules and Linux processes.
 *
 * @internal
 * 
 * rt_heap_open is used to allocate open/create a shared real time heap.
 * 
 * @param name is an unsigned long identifier;
 * 
 * @param size is the amount of required shared memory;
 * 
 * @param suprt is the kernel allocation method to be used, it can be:
 * - USE_VMALLOC, use vmalloc;
 * - USE_GFP_KERNEL, use kmalloc with GFP_KERNEL;
 * - USE_GFP_ATOMIC, use kmalloc with GFP_ATOMIC;
 * - USE_GFP_DMA, use kmalloc with GFP_DMA.
 *
 * Since @a name can be a clumsy identifier, services are provided to
 * convert 6 characters identifiers to unsigned long, and vice versa.
 * 
 * @see nam2num() and num2nam().
 * 
 * It must be remarked that only the very first open does a real allocation, 
 * any subsequent one with the same name from anywhere will just map the area 
 * to the user space, or return the related pointer to the already allocated 
 * memory in kernel space. In any case the functions return a pointer to the 
 * allocated memory, appropriately mapped to the memory space in use.
 * Be careful and avoid opening more than one group heap per process/task, if 
 * more than one is opened then just the last will used.
 *
 * @returns a valid address on succes, 0 on failure.
 *
 */

void *rt_heap_open(unsigned long name, int size, int suprt)
{
	void *adr;
	if ((adr = rt_shm_alloc(name, size + sizeof(spinlock_t *) + 1, suprt))) {
		rt_set_heap(name, adr);
		return adr;
	}
	return 0;
}

struct rt_native_fun_entry rt_shm_entries[] = {
        { { 0, rt_shm_alloc_usp },		SHM_ALLOC },
        { { 0, rt_shm_free },			SHM_FREE },
        { { 0, rt_shm_size },			SHM_SIZE },
        { { 0, rt_set_heap },			HEAP_SET},
        { { 0, rt_halloc },			HEAP_ALLOC },
        { { 0, rt_hfree },			HEAP_FREE },
        { { 0, rt_named_halloc },		HEAP_NAMED_ALLOC },
        { { 0, rt_named_hfree },		HEAP_NAMED_FREE },
        { { 0, rt_malloc_new_usp },		MALLOC },
        { { 0, rt_free_new_usp },		FREE },
        { { 0, rt_named_malloc_usp },		NAMED_MALLOC },
        { { 0, rt_named_free_usp },		NAMED_FREE },
        { { 0, 0 },				000 }
};

extern int set_rt_fun_entries(struct rt_native_fun_entry *entry);
extern void reset_rt_fun_entries(struct rt_native_fun_entry *entry);

#define GLOBAL_HEAP_SIZE  PAGE_SIZE*31;  // just to have something at the moment
static int GlobalHeapSize = GLOBAL_HEAP_SIZE;
MODULE_PARM(GlobalHeapSize, "i");

static void *global_heap;

int SHM_INIT_MODULE (void)
{
	if (misc_register(&rtai_shm_dev) < 0) {
		printk("***** UNABLE TO REGISTER THE SHARED MEMORY DEVICE (miscdev minor: %d) *****\n", RTAI_SHM_MISC_MINOR);
		return -EBUSY;
	}
	if (!(global_heap = rt_heap_open(GLOBAL_HEAP_ID, GlobalHeapSize, 
#ifdef CONFIG_RTAI_MALLOC_VMALLOC
USE_VMALLOC
#else 
USE_GFP_KERNEL
#endif
))) {
		misc_deregister(&rtai_shm_dev);
		printk("***** UNABLE TO CREATE THE GLOBAL REAL TIME HEAP (size: %d) *****\n", GlobalHeapSize);
		return -ENOMEM;
	}
	return set_rt_fun_entries(rt_shm_entries);
}

void SHM_CLEANUP_MODULE (void)
{
        int slot;
        struct rt_registry_entry_struct entry;

	rt_heap_close(GLOBAL_HEAP_ID, global_heap);
	for (slot = 1; slot <= MAX_SLOTS; slot++) {
		if (rt_get_registry_slot(slot, &entry) && entry.adr) {
			if (abs(entry.type) >= PAGE_SIZE) {
        			char name[8];
				while (_rt_shm_free(entry.name, entry.type));
                        	num2nam(entry.name, name);
	                        rt_printk("\nSHM_CLEANUP_MODULE releases: '%s':0x%lx:%lu (%d).\n", name, entry.name, entry.name, entry.type);
                        }
		}
	}
	reset_rt_fun_entries(rt_shm_entries);
	misc_deregister(&rtai_shm_dev);
//	printk("***** GLOBAL_HEAP_ID = 0x%lx *****\n", nam2num("RTGLBH"));
	return;
}

/*@}*/
