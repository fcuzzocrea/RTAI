/*!\file intr.c
 * \brief Interrupt management.
 * \author Philippe Gerum
 *
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * As a special exception, the RTAI project gives permission
 * for additional uses of the text contained in its release of
 * Xenomai.
 *
 * The exception is that, if you link the Xenomai libraries with other
 * files to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the Xenomai libraries code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public
 * License.
 *
 * This exception applies only to the code released by the
 * RTAI project under the name Xenomai.  If you copy code from other
 * RTAI project releases into a copy of Xenomai, as the General Public
 * License permits, the exception does not apply to the code that you
 * add in this way.  To avoid misleading anyone as to the status of
 * such modified files, you must delete this exception notice from
 * them.
 *
 * If you write modifications of your own for Xenomai, it is your
 * choice whether to permit this exception to apply to your
 * modifications. If you do not wish that, delete this exception
 * notice.
 *
 * \ingroup intr
 */

/*!
 * \ingroup nucleus
 * \defgroup intr Interrupt management.
 *
 * Interrupt management.
 *
 *@{*/

#define XENO_INTR_MODULE

#include <nucleus/pod.h>
#include <nucleus/intr.h>

static void xnintr_irq_handler(unsigned irq,
			       void *cookie);

xnintr_t nkclock;

/*! 
 * \fn int xnintr_init (xnintr_t *intr,
                        unsigned irq,
                        xnisr_t isr,
  			   xnflags_t flags);
 * \brief Initialize an interrupt object.
 *
 * Associates an interrupt object with an IRQ line.
 *
 * When an interrupt occurs from the given @a irq line, the ISR is
 * fired in order to deal with the hardware event. The interrupt
 * service code may call any non-suspensive service from the
 * nucleus.
 *
 * Upon receipt of an IRQ, the ISR is immediately called on behalf of
 * the interrupted stack context. The status value returned by the ISR
 * is then checked for the following bits:
 *
 * - XN_ISR_ENABLE asks the nucleus to re-enable the IRQ line. Over
 * some real-time control layers which mask and acknowledge IRQs, this
 * operation is necessary to revalidate the interrupt channel so that
 * more interrupts can be notified. The presence of such bit in the
 * ISR's return code causes Xenomai to ask the real-time control layer
 * to re-enable the interrupt.
 *
 * - XN_ISR_CHAINED tells the nucleus to require the real-time control
 * layer to forward the IRQ. For instance, this would cause the Adeos
 * control layer to propagate the interrupt down the interrupt
 * pipeline to other Adeos domains, such as Linux. This is the regular
 * way to share interrupts between Xenomai and the host system.
 *
 * @param intr The address of a interrupt object descriptor Xenomai
 * will use to store the object-specific data.  This descriptor must
 * always be valid while the object is active therefore it must be
 * allocated in permanent memory.
 *
 * @param irq The hardware interrupt channel associated with the
 * interrupt object. This value is architecture-dependent. An
 * interrupt object must then be attached to the hardware interrupt
 * vector using the xnintr_attach() service for the associated IRQs
 * to be directed to this object.
 *
 * @param isr The address of a valid low-level interrupt service
 * routine if this parameter is non-zero. This handler will be called
 * each time the corresponding IRQ is delivered on behalf of an
 * interrupt context.  When called, the ISR is passed the descriptor
 * address of the interrupt object.
 *
 * @param flags A set of creation flags affecting the operation. Since
 * no flags are currently defined, zero should be passed for this
 * parameter.
 *
 * @return No error condition being defined, 0 is always returned.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread.
 */

int xnintr_init (xnintr_t *intr,
		 unsigned irq,
		 xnisr_t isr,
		 xnflags_t flags)
{
    intr->irq = irq;
    intr->isr = isr;
    intr->cookie = NULL;
    intr->status = 0;
    intr->affinity = ~0;

    return 0;
}

/*! 
 * \fn int xnintr_destroy (xnintr_t *intr);
 * \brief Destroy an interrupt object.
 *
 * Destroys an interrupt object previously initialized by
 * xnintr_init(). The interrupt object is automatically detached by a
 * call to xnintr_detach(). No more IRQs will be dispatched by this
 * object after this service has returned.
 *
 * @param intr The descriptor address of the interrupt object to
 * destroy.
 *
 * @return 0 is returned on success. Otherwise, -EBUSY is returned if
 * an error occurred while detaching the interrupt (see
 * xnintr_detach()).
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread.
 */

int xnintr_destroy (xnintr_t *intr) {

    return xnintr_detach(intr);
}

/*! 
 * \fn int xnintr_attach (xnintr_t *intr, void *cookie);
 * \brief Attach an interrupt object.
 *
 * Attach an interrupt object previously initialized by
 * xnintr_init(). After this operation is completed, all IRQs received
 * from the corresponding interrupt channel are directed to the
 * object's ISR.
 *
 * @param intr The descriptor address of the interrupt object to
 * attach.
 *
 * @param cookie A user-defined opaque value which is stored into the
 * interrupt object descriptor for further retrieval by the ISR/ISR
 * handlers.
 *
 * @return 0 is returned on success. Otherwise, -EINVAL is returned if
 * a low-level error occurred while attaching the interrupt. -EBUSY is
 * specifically returned if the interrupt object was already attached.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread.
 */

int xnintr_attach (xnintr_t *intr,
		   void *cookie)
{
    spl_t s;
    int err;

    xnlock_get_irqsave(&nklock,s);
    intr->cookie = cookie;
    err = xnarch_hook_irq(intr->irq,&xnintr_irq_handler,intr);
    setbits(intr->status,XNINTR_ATTACHED);
    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*! 
 * \fn int xnintr_detach (xnintr_t *intr);
 * \brief Detach an interrupt object.
 *
 * Detach an interrupt object previously attached by
 * xnintr_attach(). After this operation is completed, no more IRQs
 * are directed to the object's ISR, but the interrupt object itself
 * remains valid. A detached interrupt object can be attached again by
 * a subsequent call to xnintr_attach().
 *
 * @param intr The descriptor address of the interrupt object to
 * detach.
 *
 * @return 0 is returned on success. Otherwise, -EINVAL is returned if
 * a low-level error occurred while detaching the interrupt. Detaching
 * a non-attached interrupt object leads to a null-effect and returns
 * 0.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread.
 */

int xnintr_detach (xnintr_t *intr)

{
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    if (testbits(intr->status,XNINTR_ATTACHED))
	{
	err = xnarch_release_irq(intr->irq);

	if (!err)
	    clrbits(intr->status,XNINTR_ATTACHED);
	}

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*! 
 * \fn int xnintr_enable (xnintr_t *intr);
 * \brief Enable an interrupt object.
 *
 * Enables the hardware interrupt line associated with an interrupt
 * object. Over real-time control layers which mask and acknowledge
 * IRQs, this operation is necessary to revalidate the interrupt
 * channel so that more interrupts can be notified.

 * @param intr The descriptor address of the interrupt object to
 * enable.
 *
 * @return 0 is returned on success. Otherwise, -EINVAL is returned if
 * a low-level error occurred while enabling the interrupt.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread.
 */

int xnintr_enable (xnintr_t *intr)

{
    spl_t s;
    int err;

    xnlock_get_irqsave(&nklock,s);
    err = xnarch_enable_irq(intr->irq);
    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*! 
 * \fn int xnintr_disable (xnintr_t *intr);
 * \brief Disable an interrupt object.
 *
 * Disables the hardware interrupt line associated with an interrupt
 * object. This operation invalidates further interrupt requests from
 * the given source until the IRQ line is re-enabled anew.
 *
 * @param intr The descriptor address of the interrupt object to
 * disable.
 *
 * @return 0 is returned on success. Otherwise, -EINVAL is returned if
 * a low-level error occurred while disabling the interrupt.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread.
 */

int xnintr_disable (xnintr_t *intr)

{
    spl_t s;
    int err;

    xnlock_get_irqsave(&nklock,s);
    err = xnarch_disable_irq(intr->irq);
    xnlock_put_irqrestore(&nklock,s);

    return err;
}
    
int xnintr_affinity (xnintr_t *intr, unsigned long cpumask) {

    spl_t s;
    int err;

    xnlock_get_irqsave(&nklock,s);

    err = xnarch_set_irq_affinity(intr->irq,cpumask);

    if (!err)
	intr->affinity = cpumask;

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/* Low-level clock irq handler. */

void xnintr_clock_handler (void) {

    xnintr_irq_handler(nkclock.irq,&nkclock);
}

/*
 * Low-level interrupt handler dispatching the user-defined ISR for
 * interrupts other than the clock IRQ -- Called with interrupts off.
 */

static void xnintr_irq_handler (unsigned irq, void *cookie)

{
    xnsched_t *sched = xnpod_current_sched();
    xnintr_t *intr = (xnintr_t *)cookie;
    int s;

    xnarch_memory_barrier();

    sched->inesting++;
    s = intr->isr(intr);
    sched->inesting--;

    if (s & XN_ISR_ENABLE)
	xnarch_isr_enable_irq(irq);

    if (s & XN_ISR_CHAINED)
	xnarch_isr_chain_irq(irq);

    if (sched->inesting == 0 && xnsched_resched_p())
	xnpod_schedule();

    /* Since the host tick is low priority, we can wait for returning
       from the rescheduling procedure before actually calling the
       propagation service, if it is pending. */

    if (testbits(sched->status,XNHTICK))
	{
	clrbits(sched->status,XNHTICK);
	xnarch_relay_tick();
	}
}

/*@}*/

EXPORT_SYMBOL(xnintr_attach);
EXPORT_SYMBOL(xnintr_destroy);
EXPORT_SYMBOL(xnintr_detach);
EXPORT_SYMBOL(xnintr_disable);
EXPORT_SYMBOL(xnintr_enable);
EXPORT_SYMBOL(xnintr_init);
