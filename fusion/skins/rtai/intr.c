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

static int __intr_trampoline (xnintr_t *_intr)

{
    RT_INTR *intr = (RT_INTR *)_intr->cookie;
    int s;

    /*
     * Careful here:
     * xnintr::isr == &__intr_trampoline()
     * RT_INTR::isr == &user_defined_ISR()
     */

    if (intr->isr)
	s = intr->isr(intr);
    else
	{
	++intr->pending;

	if (xnsynch_nsleepers(&intr->synch_base) > 0)
	    xnsynch_flush(&intr->synch_base,0);

	/* Auto-enable when running in sync mode. */
	s = XN_ISR_HANDLED|(intr->mode & XN_ISR_ENABLE);
	}

    return s;
}

int rt_intr_create (RT_INTR *intr,
		    unsigned irq,
		    rt_isr_t isr,
		    int mode)
{
    char name[XNOBJECT_NAME_LEN];
    int err;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (mode & ~I_AUTOENA)
	return -EINVAL;

    xnintr_init(&intr->intr_base,irq,&__intr_trampoline,0);

    xnsynch_init(&intr->synch_base,XNSYNCH_PRIO);
    intr->isr = isr;
    intr->mode = mode;
    intr->pending = -1;
    intr->magic = RTAI_INTR_MAGIC;
    intr->handle = 0;    /* i.e. (still) unregistered interrupt. */
    inith(&intr->link);
    xnlock_get_irqsave(&nklock,s);
    appendq(&__rtai_intr_q,&intr->link);
    xnlock_put_irqrestore(&nklock,s);
    snprintf(name,sizeof(name),"interrupt/%u",irq);

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    intr->source = RT_KAPI_SOURCE;
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    err = xnintr_attach(&intr->intr_base,intr);

    /* <!> Since rt_register_enter() may reschedule, only register
       complete objects, so that the registry cannot return handles to
       half-baked objects... */

    if (!err)
	err = rt_registry_enter(name,intr,&intr->handle);

    if (err)
	rt_intr_delete(intr);

    return err;
}

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
    rc = xnsynch_destroy(&intr->synch_base);
    xnintr_detach(&intr->intr_base);

    if (intr->handle)
        rt_registry_remove(intr->handle);

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

int rt_intr_wait (RT_INTR *intr,
		  RTIME timeout)
{
    RT_TASK *task;
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    intr = rtai_h2obj_validate(intr,RTAI_INTR_MAGIC,RT_INTR);

    if (!intr)
        {
        err = rtai_handle_error(intr,RTAI_INTR_MAGIC,RT_INTR);
        goto unlock_and_exit;
        }

    if (intr->isr)
	{
	err = -EPERM;
	goto unlock_and_exit;
	}
    
    if (intr->pending < 0)
	{
	if (timeout == TM_NONBLOCK)
	    {
            err = -EWOULDBLOCK;
	    goto unlock_and_exit;
	    }

	xnpod_check_context(XNPOD_THREAD_CONTEXT);

	task = rtai_current_task();

	xnsynch_sleep_on(&intr->synch_base,timeout);
        
	if (xnthread_test_flags(&task->thread_base,XNRMID))
	    err = -EIDRM; /* Interrupt object deleted while pending. */
	else if (xnthread_test_flags(&task->thread_base,XNTIMEO))
	    err = -ETIMEDOUT; /* Timeout.*/
	else if (xnthread_test_flags(&task->thread_base,XNBREAK))
	    err = -EINTR; /* Unblocked.*/
	}
    else
	--intr->pending;
    
 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

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
    
    info->nwaiters = xnsynch_nsleepers(&intr->synch_base);
    info->irq = intr->intr_base.irq;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

EXPORT_SYMBOL(rt_intr_create);
EXPORT_SYMBOL(rt_intr_delete);
EXPORT_SYMBOL(rt_intr_wait);
EXPORT_SYMBOL(rt_intr_enable);
EXPORT_SYMBOL(rt_intr_disable);
EXPORT_SYMBOL(rt_intr_inquire);
