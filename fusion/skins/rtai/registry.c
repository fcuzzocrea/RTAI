/**
 * @file
 * This file is part of the RTAI project.
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org> 
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
 *
 * \ingroup registry
 */

/*!
 * \ingroup native
 * \defgroup registry Registry services.
 *
 * The registry provides a mean to index real-time object descriptors
 * created by the RTAI skin on unique alphanumeric keys. When labeled
 * this way, a real-time object is globally exported; it can be
 * searched for, and its descriptor returned to the caller for further
 * use; the latter operation is called a "binding". When no object has
 * been registered under the given name yet, the registry can be asked
 * to set up a rendez-vous, blocking the caller until the object is
 * eventually registered.
 *
 * The registry is a simple yet powerful mechanism for sharing
 * real-time objects between kernel-based and user-space tasks, or
 * between tasks belonging to different user-space processes. Once the
 * binding has been done, an exported object can be controlled through
 * the regular API using the ubiquitous descriptor returned by the
 * registry.
 *
 * All high-level real-time objects created by the RTAI skin can be
 * registered. The name parameter passed to the various object
 * creation routines is used to have the new object indexed by the
 * RTAI registry. Such registration is always optional though, and can
 * be avoided by passing a null or empty name string.
 *
 *@{*/

#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <rtai/registry.h>
#include <rtai/task.h>

static RT_OBJECT __rtai_obj_slots[CONFIG_RTAI_OPT_NATIVE_REGISTRY_NRSLOTS];

static xnqueue_t __rtai_obj_busyq,
                 __rtai_obj_freeq;

static u_long __rtai_obj_stamp;

static RT_HASH **__rtai_hash_table;

static int __rtai_hash_entries;

static xnsynch_t __rtai_hash_synch;

int __registry_pkg_init (void)

{
    static const u_short primes[] = {
	101,  211,  307,  401,  503,  601,
	701,  809,  907,  1009, 1103
    };

#define obj_hash_max(n)			 \
((n) < sizeof(primes) / sizeof(u_long) ? \
 (n) : sizeof(primes) / sizeof(u_long) - 1)

    int n;

    initq(&__rtai_obj_freeq);
    initq(&__rtai_obj_busyq);

    for (n = 0; n < CONFIG_RTAI_OPT_NATIVE_REGISTRY_NRSLOTS; n++)
	{
	inith(&__rtai_obj_slots[n].link);
	__rtai_obj_slots[n].objaddr = NULL;
	appendq(&__rtai_obj_freeq,&__rtai_obj_slots[n].link);
	}

    getq(&__rtai_obj_freeq); /* Slot #0 is reserved/invalid. */
    
    __rtai_hash_entries = primes[obj_hash_max(CONFIG_RTAI_OPT_NATIVE_REGISTRY_NRSLOTS / 100)];
    __rtai_hash_table = (RT_HASH **)xnmalloc(sizeof(RT_HASH *) * __rtai_hash_entries);

    for (n = 0; n < __rtai_hash_entries; n++)
	__rtai_hash_table[n] = NULL;

    xnsynch_init(&__rtai_hash_synch,XNSYNCH_FIFO);

    return 0;
}

void __registry_pkg_cleanup (void)

{
    RT_HASH *ecurr, *enext;
    int n;

    for (n = 0 ; n < __rtai_hash_entries; n++)
	{
	for (ecurr = __rtai_hash_table[n]; ecurr; ecurr = enext)
	    {
	    enext = ecurr->next;
	    xnfree(ecurr);
	    }
	}

    xnfree(__rtai_hash_table);

    xnsynch_destroy(&__rtai_hash_synch);
}

static inline RT_OBJECT *__registry_validate (rt_handle_t handle)

{
    if (handle > 0 && handle < CONFIG_RTAI_OPT_NATIVE_REGISTRY_NRSLOTS)
	{
        RT_OBJECT *object = &__rtai_obj_slots[handle];
	return object->objaddr ? object : NULL;
	}

    return NULL;
}

static unsigned __registry_hash_crunch (const char *key)

{
    unsigned h = 0, g;

#define HQON    24		/* Higher byte position */
#define HBYTE   0xf0000000	/* Higher nibble on */

    while (*key)
	{
	h = (h << 4) + *key++;
	if ((g = (h & HBYTE)) != 0)
	    h = (h ^ (g >> HQON)) ^ g;
	}

    return h % __rtai_hash_entries;
}

static inline int __registry_hash_enter (const char *key,
					 RT_OBJECT *object)
{
    RT_HASH *enew, *ecurr;
    unsigned s;

    object->key = key;
    s = __registry_hash_crunch(key);

    for (ecurr = __rtai_hash_table[s]; ecurr != NULL; ecurr = ecurr->next)
	{
	if (ecurr->object == object || !strcmp(key,ecurr->object->key))
	    return -EEXIST;
	}

    enew = (RT_HASH *)xnmalloc(sizeof(*enew));

    if (!enew)
	return -ENOMEM;

    enew->object = object;
    enew->next = __rtai_hash_table[s];
    __rtai_hash_table[s] = enew;
    
    return 0;
}

static inline int __registry_hash_remove (RT_OBJECT *object)

{
    unsigned s = __registry_hash_crunch(object->key);
    RT_HASH *ecurr, *eprev;

    for (ecurr = __rtai_hash_table[s], eprev = NULL;
	 ecurr != NULL; eprev = ecurr, ecurr = ecurr->next)
	{
	if (ecurr->object == object)
	    {
	    if (eprev)
		eprev->next = ecurr->next;
	    else
		__rtai_hash_table[s] = ecurr->next;

	    xnfree(ecurr);

	    return 0;
	    }
	}

    return -ENOENT;
}

static RT_OBJECT *__registry_hash_find (const char *key)

{
    RT_HASH *ecurr;

    for (ecurr = __rtai_hash_table[__registry_hash_crunch(key)];
	 ecurr != NULL; ecurr = ecurr->next)
	{
	if (!strcmp(key,ecurr->object->key))
	    return ecurr->object;
	}

    return NULL;
}

/**
 * @fn int rt_registry_enter(const char *key,
		             void *objaddr,
			     rt_handle_t *phandle)
 * @brief Register a real-time object.
 *
 * This service allocates a new registry slot for an associated
 * object, and indexes it by an alphanumeric key for later retrieval.
 *
 * @param key A valid NULL-terminated string by which the object will
 * be indexed and later retrieved in the registry. Since it is assumed
 * that such key is stored into the registered object, it will *not*
 * be copied but only kept by reference in the registry.
 *
 * @param objaddr An opaque pointer to the object to index by @a
 * key.
 *
 * @param phandle A pointer to a generic handle defined by the
 * registry which will uniquely identify the indexed object, until the
 * latter is unregistered using the rt_registry_remove() service.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a key or @a objaddr are NULL.
 *
 * - -ENOMEM is returned if the system fails to get enough dynamic
 * memory from the global real-time heap in order to register the
 * object.
 *
 * - -EEXIST is returned if the @a key is already in use.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

int rt_registry_enter (const char *key,
		       void *objaddr,
		       rt_handle_t *phandle)
{
    xnholder_t *holder;
    RT_OBJECT *object;
    spl_t s;
    int err;

    if (!key || !objaddr)
	return -EINVAL;

    xnlock_get_irqsave(&nklock,s);

    holder = getq(&__rtai_obj_freeq);

    if (!holder)
	{
	err = -ENOMEM;
	goto unlock_and_exit;
	}

    object = link2rtobj(holder);

    err = __registry_hash_enter(key,object);

    if (err != 0)
	{
	appendq(&__rtai_obj_freeq,holder);
	goto unlock_and_exit;
	}

    xnsynch_init(&object->safesynch,XNSYNCH_FIFO);
    object->objaddr = objaddr;
    object->cstamp = ++__rtai_obj_stamp;
    object->safelock = 0;
    appendq(&__rtai_obj_busyq,holder);

    /* <!> Make sure the handle is written back before the
       rescheduling takes place. */
    *phandle = object - __rtai_obj_slots;

    if (xnsynch_nsleepers(&__rtai_hash_synch) > 0)
	{
	xnsynch_flush(&__rtai_hash_synch,RT_REGISTRY_RECHECK);
	xnpod_schedule();
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_registry_bind(const char *key,
                            RTIME timeout,
			    rt_handle_t *phandle)
 * @brief Bind to a real-time object.
 *
 * This service retrieves the registry handle of a given object
 * identified by its key. Unless otherwise specified, this service
 * will block the caller if the object is not registered yet, waiting
 * for such registration to occur.
 *
 * @param key A valid NULL-terminated string which identifies the
 * object to bind to.
 *
 * @param timeout The number of clock ticks to wait for the
 * registration to occur (see note). Passing RT_TIME_INFINITE causes
 * the caller to block indefinitely until the object is
 * registered. Passing RT_TIME_NONBLOCK causes the service to return
 * immediately without waiting if the object is not registered on
 * entry.
 *
 * @param phandle A pointer to a memory location which will be written
 * upon success with the generic handle defined by the registry for
 * the retrieved object. Contents of this memory is undefined on
 * failure.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a key is NULL.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before the retrieval has completed.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to
 * RT_TIME_NONBLOCK and the searched object is not registered on
 * entry.
 *
 * - -ETIMEDOUT is returned if the object cannot be retrieved within
 * the specified amount of time.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @a timeout is equal to RT_TIME_NONBLOCK.
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are expressed as periodic jiffies. In
 * oneshot mode, clock ticks are expressed in nanoseconds.
 */

int rt_registry_bind (const char *key,
		      RTIME timeout,
		      rt_handle_t *phandle)
{
    RT_OBJECT *object;
    xnticks_t stime;
    RT_TASK *task;
    int err = 0;
    spl_t s;

    if (timeout != RT_TIME_NONBLOCK)
	xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (!key)
	return -EINVAL;

    task = rtai_current_task();

    xnlock_get_irqsave(&nklock,s);

    stime = xnpod_get_time();

    for (;;)
	{
	object = __registry_hash_find(key);

	if (object)
	    {
	    *phandle = object - __rtai_obj_slots;
	    goto unlock_and_exit;
	    }

	if (timeout == RT_TIME_NONBLOCK)
	    {
	    err = -EWOULDBLOCK;
	    goto unlock_and_exit;
	    }

	xnthread_clear_flags(&task->thread_base,RT_REGISTRY_RECHECK);

	if (timeout != RT_TIME_INFINITE)
	    {
	    xnticks_t now = xnpod_get_time();

	    if (stime + timeout >= now)
		break;

	    timeout -= (now - stime);
	    stime = now;
	    }

	xnsynch_sleep_on(&__rtai_hash_synch,timeout);

	if (xnthread_test_flags(&task->thread_base,XNTIMEO))
	    {
	    err = -ETIMEDOUT;
	    goto unlock_and_exit;
	    }

	if (xnthread_test_flags(&task->thread_base,XNBREAK))
	    {
	    err = -EINTR;
	    goto unlock_and_exit;
	    }
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_registry_remove(rt_handle_t handle)
 * @brief Forcibly unregister a real-time object.
 *
 * This service forcibly removes an object from the registry. The
 * removal is performed regardless of the current object's locking
 * status.
 *
 * @param handle The generic handle of the object to remove.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -ENOENT is returned if @a handle does not reference a registered
 * object.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int rt_registry_remove (rt_handle_t handle)

{
    RT_OBJECT *object;
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    object = __registry_validate(handle);

    if (!object)
	{
	err = -ENOENT;
	goto unlock_and_exit;
	}

    __registry_hash_remove(object);
    object->objaddr = NULL;
    object->cstamp = 0;
    removeq(&__rtai_obj_busyq,&object->link);
    appendq(&__rtai_obj_freeq,&object->link);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_registry_remove_safe(rt_handle_t handle,
                                   RTIME timeout)
 * @brief Unregister an idle real-time object.
 *
 * This service removes an object from the registry. The caller might
 * sleep as a result of waiting for the target object to be unlocked
 * prior to the removal (see rt_registry_put()).
 *
 * @param handle The generic handle of the object to remove.
 *
 * @param timeout If the object is locked on entry, @a param gives the
 * number of clock ticks to wait for the unlocking to occur (see
 * note). Passing RT_TIME_INFINITE causes the caller to block
 * indefinitely until the object is unlocked. Passing RT_TIME_NONBLOCK
 * causes the service to return immediately without waiting if the
 * object is locked on entry.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -ENOENT is returned if @a handle does not reference a registered
 * object.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to
 * RT_TIME_NONBLOCK and the object is locked on entry.
 *
 * - -EBUSY is returned if @a handle refers to a locked object and the
 * caller could not sleep until it is unlocked.
 *
 * - -ETIMEDOUT is returned if the object cannot be removed within the
 * specified amount of time.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * calling task waiting for the object to be unlocked.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @a timeout is equal to RT_TIME_NONBLOCK.
 *
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible if the object to remove is currently locked
 * and the calling context can sleep.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are expressed as periodic jiffies. In
 * oneshot mode, clock ticks are expressed in nanoseconds.
 */

int rt_registry_remove_safe (rt_handle_t handle, RTIME timeout)

{
    RT_OBJECT *object;
    u_long cstamp;
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    object = __registry_validate(handle);

    if (!object)
	{
	err = -ENOENT;
	goto unlock_and_exit;
	}

    if (object->safelock > 0)
	{
	if (timeout == RT_TIME_NONBLOCK)
	    {
	    err = -EWOULDBLOCK;
	    goto unlock_and_exit;
	    }
	
	if (!xnpod_pendable_p())
	    {
	    err = -EBUSY;
	    goto unlock_and_exit;
	    }
	}

    /*
     * The object creation stamp is here to deal with situations like this
     * one:
     *
     * Thread(A) locks Object(T) using rt_registry_get()
     * Thread(B) attempts to delete Object(T) using __registry_delete()
     * Thread(C) attempts the same deletion, waiting like Thread(B) for
     * the object's safe count to fall down to zero.
     * Thread(A) unlocks Object(T), unblocking Thread(B) and (C).
     * Thread(B) wakes up and successfully deletes Object(T)
     * Thread(D) preempts Thread(C) and recycles Object(T) for another object
     * Thread(C) wakes up and attempts to finalize the deletion of the
     * _former_ Object(T), which leads to the spurious deletion of the
     * _new_ Object(T).
     */

    cstamp = object->cstamp;

    do
	{
	xnsynch_sleep_on(&object->safesynch,timeout);

	if (xnthread_test_flags(&rtai_current_task()->thread_base,XNBREAK))
	    {
	    err = -EINTR;
	    goto unlock_and_exit;
	    }

	if (xnthread_test_flags(&rtai_current_task()->thread_base,XNTIMEO))
	    {
	    err = -ETIMEDOUT;
	    goto unlock_and_exit;
	    }
	}
    while (object->safelock > 0);

    if (object->cstamp == cstamp)
	err = rt_registry_remove(handle);
    else
	/* The caller should silently abort the deletion process. */
	err = -ENOENT;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn void *rt_registry_get(rt_handle_t handle)
 * @brief Find and lock a real-time object into the registry.
 *
 * This service retrieves an object from its handle into the registry
 * and prevents it deletion atomically. A locking count is tracked, so
 * that rt_registry_get() and rt_registry_put() must be used in pair.
 *
 * @param handle The generic handle of the object to find and lock. If
 * RT_REGISTRY_SELF is passed, the object is the calling RTAI task.
 *
 * @return The memory address of the object's descriptor is returned
 * on success. Otherwise, NULL is returned if @a handle does not
 * reference a registered object.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * only if @a handle is different from RT_REGISTRY_SELF.
 *
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

void *rt_registry_get (rt_handle_t handle)

{
    RT_OBJECT *object;
    void *objaddr;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    if (handle == RT_REGISTRY_SELF)
	{
	xnpod_check_context(XNPOD_THREAD_CONTEXT);

	if (xnpod_current_thread()->magic == RTAI_SKIN_MAGIC)
	    {
	    objaddr = rtai_current_task();
	    goto unlock_and_exit;
	    }
	}

    object = __registry_validate(handle);

    if (object)
	{
	++object->safelock;
	objaddr = object->objaddr;
	}
    else
	objaddr = NULL;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return objaddr;
}

/**
 * @fn u_long rt_registry_put(rt_handle_t handle)
 * @brief Unlock a real-time object from the registry.
 *
 * This service decrements the lock count of a registered object
 * previously locked by a call to rt_registry_get(). The object is
 * actually unlocked from the registry when the locking count falls
 * down to zero, thus waking up any task currently waiting inside
 * rt_registry_remove() for unregistering it.
 *
 * @param handle The generic handle of the object to unlock. If
 * RT_REGISTRY_SELF is passed, the object is the calling RTAI task.
 *
 * @return The decremented lock count is returned upon success. Zero
 * is also returned if @a handle does not reference a registered
 * object.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * only if @a handle is different from RT_REGISTRY_SELF.
 *
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible if the lock count falls down to zero and
 * some task is currently waiting for the object to be unlocked.
 */

u_long rt_registry_put (rt_handle_t handle)

{
    RT_OBJECT *object;
    u_long newlock;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    if (handle == RT_REGISTRY_SELF)
	{
	xnpod_check_context(XNPOD_THREAD_CONTEXT);

	if (xnpod_current_thread()->magic == RTAI_SKIN_MAGIC)
	    handle = rtai_current_task()->handle;
	}

    object = __registry_validate(handle);

    if (!object)
	{
	newlock = 0;
	goto unlock_and_exit;
	}

    if ((newlock = object->safelock) > 0 &&
	(newlock = --object->safelock) == 0 &&
	xnsynch_nsleepers(&object->safesynch) > 0)
	{
	xnsynch_flush(&object->safesynch,XNBREAK);
	xnpod_schedule();
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return newlock;
}

/**
 * @fn u_long rt_registry_fetch(rt_handle_t handle)
 * @brief Find a real-time object into the registry.
 *
 * This service retrieves an object from its handle into the registry
 * and returns the memory address of its descriptor.
 *
 * @param handle The generic handle of the object to fetch. If
 * RT_REGISTRY_SELF is passed, the object is the calling RTAI task.
 *
 * @return The memory address of the object's descriptor is returned
 * on success. Otherwise, NULL is returned if @a handle does not
 * reference a registered object.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * only if @a handle is different from RT_REGISTRY_SELF.
 *
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

void *rt_registry_fetch (rt_handle_t handle)

{
    RT_OBJECT *object;
    void *objaddr;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    if (handle == RT_REGISTRY_SELF)
	{
	xnpod_check_context(XNPOD_THREAD_CONTEXT);

	if (xnpod_current_thread()->magic == RTAI_SKIN_MAGIC)
	    {
	    objaddr = rtai_current_task();
	    goto unlock_and_exit;
	    }
	}

    object = __registry_validate(handle);

    if (object)
	objaddr = object->objaddr;
    else
	objaddr = NULL;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return objaddr;
}

/*@}*/

EXPORT_SYMBOL(rt_registry_enter);
EXPORT_SYMBOL(rt_registry_bind);
EXPORT_SYMBOL(rt_registry_remove);
EXPORT_SYMBOL(rt_registry_remove_safe);
EXPORT_SYMBOL(rt_registry_get);
EXPORT_SYMBOL(rt_registry_fetch);
EXPORT_SYMBOL(rt_registry_put);
