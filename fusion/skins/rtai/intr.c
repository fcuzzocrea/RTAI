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
 *
 * \ingroup interrupt
 */

/*!
 * \ingroup native
 * \defgroup interrupt Interrupt management services.
 *
 *@{*/

#include <nucleus/pod.h>
#include <rtai/task.h>
#include <rtai/registry.h>
#include <rtai/intr.h>

static DECLARE_XNQUEUE(__rtai_intr_q);

int __intr_pkg_init (void)

{
    return 0;
}

void __intr_pkg_cleanup (void)

{
    xnholder_t *holder;

    while ((holder = getheadq(&__rtai_intr_q)) != NULL)
	rt_intr_delete(link2intr(holder));
}

/*! 
 * \fn int rt_intr_create (RT_INTR *intr,
                           unsigned irq,
                           rt_isr_t isr)
 * \brief Create an interrupt object.
 *
 * Initializes and associates an interrupt object with an IRQ line.
 *
 * When an interrupt occurs on the given @a irq line, the ISR is fired
 * in order to deal with the hardware event. The interrupt service
 * code may call any non-suspensive RTAI service.
 *
 * Upon receipt of an IRQ, the ISR is immediately called on behalf of
 * the interrupted stack context, the rescheduling procedure is
 * locked, and the interrupt source is masked at hardware level. The
 * status value returned by the ISR is then checked for the following
 * bits:
 *
 * - RT_INTR_ENABLE asks RTAI to re-enable the IRQ line upon return of
 * the interrupt service routine.
 *
 * - RT_INTR_CHAINED tells RTAI to propagate the interrupt down the
 * Adeos interrupt pipeline to other Adeos domains, such as
 * Linux. This is the regular way to share interrupts between RTAI and
 * the Linux kernel. At the opposite, RT_INTR_HANDLED can be used
 * instead to indicate that the interrupt request has been fulfilled.
 *
 * A count of interrupt receipts is tracked into the interrupt
 * descriptor, and reset to zero each time the interrupt object is
 * attached. Since this count could wrap around, it should be used as
 * an indication of interrupt activity only.
 *
 * @param intr The address of a interrupt object descriptor RTAI will
 * use to store the object-specific data.  This descriptor must always
 * be valid while the object is active therefore it must be allocated
 * in permanent memory.
 *
 * @param irq The hardware interrupt channel associated with the
 * interrupt object. This value is architecture-dependent.
 *
 * @param isr The address of a valid interrupt service routine in
 * kernel space. This handler will be called each time the
 * corresponding IRQ is delivered on behalf of an interrupt context.
 * A pointer to an internal information is passed to the routine which
 * can use it to retrieve the descriptor address of the associated
 * interrupt object through the I_DESC() macro.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EBUSY is returned if the interrupt line is already in use by
 * another interrupt object. Only a single interrupt object can be
 * associated to any given interrupt line using rt_intr_create() at
 * any time.
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

int rt_intr_create (RT_INTR *intr,
		    unsigned irq,
		    rt_isr_t isr)
{
    int err;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnintr_init(&intr->intr_base,irq,isr,0);
#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    xnsynch_init(&intr->synch_base,XNSYNCH_PRIO);
    intr->pending = -1;
    intr->source = RT_KAPI_SOURCE;
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */
    intr->magic = RTAI_INTR_MAGIC;
    intr->handle = 0;    /* i.e. (still) unregistered interrupt. */
    inith(&intr->link);
    xnlock_get_irqsave(&nklock,s);
    appendq(&__rtai_intr_q,&intr->link);
    xnlock_put_irqrestore(&nklock,s);
    snprintf(intr->name,sizeof(intr->name),"interrupt/%u",irq);

    err = xnintr_attach(&intr->intr_base,intr);

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    /* <!> Since rt_register_enter() may reschedule, only register
       complete objects, so that the registry cannot return handles to
       half-baked objects... */

    if (!err)
	err = rt_registry_enter(intr->name,intr,&intr->handle);
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    if (err)
	rt_intr_delete(intr);

    return err;
}

/**
 * @fn int rt_intr_delete(RT_INTR *intr)
 * @brief Delete an interrupt object.
 *
 * Destroys an interrupt object.  An interrupt exists in the system
 * since rt_intr_create() has been called to create it, so this
 * service must be called in order to destroy it afterwards.
 *
 * Any user-space task which might be currently pending on the
 * interrupt object through the rt_intr_wait() service will be awaken
 * as a result of the deletion, and return with the -EIDRM status.
 *
 * @param intr The descriptor address of the affected interrupt object.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a intr is not a interrupt object
 * descriptor.
 *
 * - -EIDRM is returned if @a intr is a deleted interrupt object
 * descriptor.
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

int rt_intr_delete (RT_INTR *intr)

{
    int err = 0, rc;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock,s);

    intr = rtai_h2obj_validate(intr,RTAI_INTR_MAGIC,RT_INTR);

    if (!intr)
        {
        err = rtai_handle_error(intr,RTAI_INTR_MAGIC,RT_INTR);
        goto unlock_and_exit;
        }
    
    removeq(&__rtai_intr_q,&intr->link);
#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    rc = xnsynch_destroy(&intr->synch_base);
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */
    xnintr_detach(&intr->intr_base);

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    if (intr->handle)
        rt_registry_remove(intr->handle);
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    xnintr_destroy(&intr->intr_base);

    rtai_mark_deleted(intr);

    if (rc == XNSYNCH_RESCHED)
        /* Some task has been woken up as a result of the deletion:
           reschedule now. */
        xnpod_schedule();

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*! 
 * \fn int rt_intr_enable (RT_INTR *intr)
 * \brief Enable an interrupt object.
 *
 * Enables the hardware interrupt line associated with an interrupt
 * object. Over Adeos-based systems which mask and acknowledge IRQs
 * upon receipt, this operation is necessary to revalidate the
 * interrupt channel so that more interrupts from the same source can
 * be notified.

 * @param intr The descriptor address of the interrupt object to
 * enable.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a intr is not a interrupt object
 * descriptor.
 *
 * - -EIDRM is returned if @a intr is a deleted interrupt object
 * descriptor.
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

int rt_intr_enable (RT_INTR *intr)

{
    int err;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    intr = rtai_h2obj_validate(intr,RTAI_INTR_MAGIC,RT_INTR);

    if (!intr)
        {
        err = rtai_handle_error(intr,RTAI_INTR_MAGIC,RT_INTR);
        goto unlock_and_exit;
        }
    
    err = xnintr_enable(&intr->intr_base);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*! 
 * \fn int rt_intr_disable (RT_INTR *intr)
 * \brief Disable an interrupt object.
 *
 * Disables the hardware interrupt line associated with an interrupt
 * object. This operation invalidates further interrupt requests from
 * the given source until the IRQ line is re-enabled anew through
 * rt_intr_enable().
 *
 * @param intr The descriptor address of the interrupt object to
 * enable.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a intr is not a interrupt object
 * descriptor.
 *
 * - -EIDRM is returned if @a intr is a deleted interrupt object
 * descriptor.
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

int rt_intr_disable (RT_INTR *intr)

{
    int err;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    intr = rtai_h2obj_validate(intr,RTAI_INTR_MAGIC,RT_INTR);

    if (!intr)
        {
        err = rtai_handle_error(intr,RTAI_INTR_MAGIC,RT_INTR);
        goto unlock_and_exit;
        }
    
    err = xnintr_disable(&intr->intr_base);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_intr_inquire(RT_INTR *intr, RT_INTR_INFO *info)
 * @brief Inquire about an interrupt object.
 *
 * Return various information about the status of a given interrupt
 * object.
 *
 * @param intr The descriptor address of the inquired interrupt
 * object.
 *
 * @param info The address of a structure the interrupt object
 * information will be written to.

 * @return 0 is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a intr is not a interrupt object
 * descriptor.
 *
 * - -EIDRM is returned if @a intr is a deleted interrupt object
 * descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int rt_intr_inquire (RT_INTR *intr,
		     RT_INTR_INFO *info)
{
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    intr = rtai_h2obj_validate(intr,RTAI_INTR_MAGIC,RT_INTR);

    if (!intr)
        {
        err = rtai_handle_error(intr,RTAI_INTR_MAGIC,RT_INTR);
        goto unlock_and_exit;
        }
    
    strcpy(info->name,intr->name);
    info->hits = intr->intr_base.hits;
    info->irq = intr->intr_base.irq;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_intr_wait(RT_INTR *intr, RTIME timeout)
 * @brief Wait for the next interrupt.
 *
 * This user-space only call allows the current task to suspend
 * execution until the associated interrupt event triggers. The
 * priority of the current task is raised above all other RTAI tasks -
 * except those also undergoing an interrupt or alarm wait (see
 * rt_alarm_wait()) - so that it would preempt any of them under
 * normal circumstances (i.e. no scheduler lock).
 *
 * @param intr The descriptor address of the awaited interrupt.
 *
 * @param timeout The number of clock ticks to wait for an interrupt
 * to occur (see note). Passing TM_INFINITE causes the caller to block
 * indefinitely until an interrupt triggers. Passing TM_NONBLOCK is
 * invalid.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -ETIMEDOUT is returned if no interrupt occurred within the
 * specified amount of time.
 *
 * - -EINVAL is returned if @a intr is not an interrupt object
 * descriptor, or @a timeout is equal to TM_NONBLOCK.
 *
 * - -EACCES is returned if this service was not called from a task
 * context.
 *
 * - -EIDRM is returned if @a intr is a deleted interrupt object
 * descriptor, including if the deletion occurred while the caller was
 * waiting for its next interrupt.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * current task before the next interrupt occurrence.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - User-space task
 *
 * Rescheduling: always.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are interpreted as periodic jiffies. In
 * oneshot mode, clock ticks are interpreted as nanoseconds.
 */

/*@}*/

EXPORT_SYMBOL(rt_intr_create);
EXPORT_SYMBOL(rt_intr_delete);
EXPORT_SYMBOL(rt_intr_enable);
EXPORT_SYMBOL(rt_intr_disable);
EXPORT_SYMBOL(rt_intr_inquire);
