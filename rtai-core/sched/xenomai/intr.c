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
 * \ingroup xenomai
 * \defgroup intr Interrupt management.
 *
 * Interrupt management.
 *
 *@{*/

#define XENO_INTR_MODULE

#include "rtai_config.h"
#include <xenomai/pod.h>
#include <xenomai/mutex.h>
#include <xenomai/intr.h>

static void xnintr_irq_handler(unsigned irq, void *cookie);

static void xnintr_svc_thread(void *cookie);

/*! 
 * \fn int xnintr_init (xnintr_t *intr,
                        unsigned irq,
                        int priority,
                        xnisr_t isr,
                        xnist_t ist,
  			   xnflags_t flags);
 * \brief Initialize an interrupt object.
 *
 * The nanokernel defines a threaded interrupt model in order to:
 *
 * - provide a mean for prioritizing interrupt handling by software.
 *
 * - allow the interrupt code to synchronize with other system code
 * using mutexes, therefore reducing the need for hard interrupt
 * masking in critical sections.
 *
 * Xenomai's nanokernel exhibits a split interrupt handling scheme
 * separated into two parts. The first part is known as the Interrupt
 * Service Routine (ISR), the second is the Interrupt Service Tasklet
 * (IST).
 *
 * When an interrupt occurs, the ISR is fired in order to deal with
 * the hardware event as fast as possible, without any interaction
 * with the nanokernel. If the interrupt service code needs to reenter
 * the nanokernel, the ISR may require an associated interrupt service
 * tasklet to be scheduled immediately upon return. The IST has a
 * lightweight thread context that allows it to invoke all nanokernel
 * services safely. A Xenomai interrupt object may be associated with
 * an ISR and/or an IST to process each IRQ event.
 *
 ********************************************************************
 * [WARNING] Interrupt service threads/tasklets are deprecated in
 * newer versions of the Xenomai nucleus, basically due to design and
 * performance issues. Do not use the IST facility if you plan to port
 * to RTAI/fusion.
 ********************************************************************
 *
 * Upon receipt of an IRQ, the ISR/IST invocation policy is as
 * follows:
 *
 * - if an ISR has been defined, it is immediately called on behalf of
 * the interrupt stack context.
 *
 * - if an IST has been defined, then its is scheduled upon return of
 * the ISR if the XN_ISR_SCHED_T bit set in its return code or if no
 * ISR has been defined for this interrupt object. The tasklet will
 * run after all ISRs have completed and all more prioritary IST have
 * returned. In any cases, the IST will run before any regular
 * real-time threads (i.e. all but interrupt service threads).
 *
 * If an ISR has been defined, the following bits are checked from its
 * return value:
 *
 * - XN_ISR_ENABLE asks the nanokernel to re-enable the IRQ line. Over
 * some real-time control layers which mask and acknowledge IRQs, this
 * operation is necessary to revalidate the interrupt channel so that
 * more interrupts can be notified. The presence of such bit in the
 * ISR's return code causes Xenomai to ask the real-time control layer
 * to re-enable the interrupt.
 *
 * - XN_ISR_CHAINED tells the nanokernel to require the real-time
 * control layer to forward the IRQ. For instance, this would cause the
 * Adeos control layer to propagate the interrupt down the interrupt
 * pipeline to other Adeos domains, such as Linux. This is the regular
 * way to share interrupts between Xenomai and the host system.

 * - XN_ISR_SCHED_T tells the nanokernel to schedule the interrupt
 * service tasklet (IST) which will be in charge of completing the
 * interrupt processing on behalf of a lightweight thread context.
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
 * @param priority The priority level of the interrupt. If a valid
 * interrupt service thread is passed in ist, this value will be used
 * to compute the base priority of the service thread. This value must
 * range from [0 .. XNINTR_MAX_PRIORITY] (inclusive). The higher the
 * value, the higher the priority, whatever the current pod priority
 * scheme is.
 *
 * @param isr The address of a valid low-level interrupt service
 * routine if this parameter is non-zero. This handler will be called
 * each time the corresponding IRQ is delivered on behalf of an
 * interrupt context.  When called, the ISR is passed the descriptor
 * address of the interrupt object.
 *
 * @param ist If non-zero, this parameter should contain the address
 * of a valid hi-level interrupt service tasklet. The tasklet will be
 * called on behalf of an interrupt service thread each time the
 * associated ISR sets the XN_ISR_SCHED_T bit in its return code.  The
 * underlying lightweight thread is immediately started with a stack
 * of XNARCH_THREAD_STACKSZ bytes.
 *
 * @param flags A set of creation flags affecting the operation. Since
 * no flags are currently defined, zero should be passed for this
 * parameter.
 *
 * @return XN_OK is returned upon success. Otherwise XNERR_PARAM is
 * returned if the interrupt priority level is out of range.
 *
 * Side-effect: This routine calls the rescheduling procedure as a
 * result of starting the interrupt service thread (if any).
 *
 * Context: This routine must be called on behalf of a thread context.
 */

int xnintr_init (xnintr_t *intr,
		 unsigned irq,
		 int priority,
		 xnisr_t isr,
		 xnist_t ist,
		 xnflags_t flags)
{
    char name[16];

    if (priority < 0 || priority > XNINTR_MAX_PRIORITY)
	return XNERR_PARAM;

    intr->irq = irq;
    intr->isr = isr;
    intr->ist = ist;
    intr->pending = 0;
    intr->cookie = NULL;
    intr->status = 0;
    intr->priority = XNPOD_ISVC_PRIO_BASE(priority);

    if (ist)
	{
	sprintf(name,"isvc%u",irq);

	xnpod_init_thread(&intr->svcthread,
			  name,
			  intr->priority,
			  XNISVC,
			  XNARCH_THREAD_STACKSZ,
			  NULL,
			  0);

	xnpod_start_thread(&intr->svcthread,
			   0,
			   0,
			   &xnintr_svc_thread,
			   intr);
	}

    return XN_OK;
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
 * @return XN_OK is returned on success. Otherwise, XNERR_BUSY is
 * returned if an error occurred while detaching the interrupt (see
 * xnintr_detach()).
 *
 * Side-effect: This routine calls the rescheduling procedure as a
 * result of an interrupt service thread deleting its own interrupt
 * object (this should rarely happen though).
 *
 * Context: This routine must be called on behalf of a thread or IST
 * context.
 */

int xnintr_destroy (xnintr_t *intr)

{
    int s = xnintr_detach(intr);

    if (s == XN_OK && intr->ist != NULL)
	xnpod_delete_thread(&intr->svcthread,NULL);

    return s;
}

/*! 
 * \fn int xnintr_attach (xnintr_t *intr, void *cookie);
 * \brief Attach an interrupt object.
 *
 * Attach an interrupt object previously initialized by
 * xnintr_init(). After this operation is completed, all IRQs received
 * from the corresponding interrupt channel are directed to the
 * object's ISR/IST handlers.
 *
 * @param intr The descriptor address of the interrupt object to
 * attach.
 *
 * @param cookie A user-defined opaque value which is stored into the
 * interrupt object descriptor for further retrieval by the ISR/ISR
 * handlers.
 *
 * @return XN_OK is returned on success. Otherwise, XNERR_NOSYS is
 * returned if a low-level error occurred while attaching the
 * interrupt. XNERR_BUSY is specifically returned if the interrupt
 * object was already attached.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread or IST
 * context.
 */

int xnintr_attach (xnintr_t *intr,
		   void *cookie)
{
    spl_t s;
    int err;

    splhigh(s);

    intr->cookie = cookie;

    switch (xnarch_hook_irq(intr->irq,&xnintr_irq_handler,intr))
	{
	case -EINVAL:

	    err = XNERR_NOSYS;
	    break;
	    
	case -EBUSY:

	    err = XNERR_BUSY;
	    break;

	default:

	    err = XN_OK;
	    break;
	}

    setbits(intr->status,XNINTR_ATTACHED);

    splexit(s);

    return err;
}

/*! 
 * \fn int xnintr_detach (xnintr_t *intr);
 * \brief Detach an interrupt object.
 *
 * Detach an interrupt object previously attached by
 * xnintr_attach(). After this operation is completed, no more IRQs
 * are directed to the object's ISR/IST handlers, but the interrupt
 * object itself remains valid. A detached interrupt object can be
 * attached again by a subsequent call to xnintr_attach().
 *
 * @param intr The descriptor address of the interrupt object to
 * detach.
 *
 * @return XN_OK is returned on success. Otherwise, XNERR_BUSY is
 * returned if a low-level error occurred while detaching the
 * interrupt. Detaching a non-attached interrupt object leads to a
 * null-effect and returns XN_OK.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread or IST
 * context.
 */

int xnintr_detach (xnintr_t *intr)

{
    int err = XN_OK;
    spl_t s;

    splhigh(s);

    if (testbits(intr->status,XNINTR_ATTACHED))
	{
	if (xnarch_release_irq(intr->irq) == -EINVAL)
	    err = XNERR_BUSY;
	else
	    clrbits(intr->status,XNINTR_ATTACHED);
	}

    splexit(s);

    return err;
}

static void xnintr_svc_thread (void *cookie)

{
    xnsched_t *sched = xnpod_current_sched();
    xnintr_t *intr = (xnintr_t *)cookie;
    int hits;
    spl_t s;

    splhigh(s);

    for (;;)
	{
	xnpod_renice_isvc(&intr->svcthread,XNPOD_ISVC_PRIO_IDLE);
	sched->inesting++;

	while (intr->pending > 0)
	    {
	    hits = intr->pending;
	    intr->pending = 0;
	    splexit(s);
	    intr->ist(intr,hits);
	    splhigh(s);
	    }

	sched->inesting--;
	}
}

static void xnintr_irq_handler (unsigned irq, void *cookie)

{
    xnsched_t *sched = xnpod_current_sched();
    xnintr_t *intr = (xnintr_t *)cookie;
    int s = XN_ISR_SCHED_T;

    xnarch_memory_barrier();

    intr->pending++;

    /* If a raw interrupt handler has been given, fire it. */

    if (intr->isr != NULL)
	{
	sched->inesting++;
	s = intr->isr(intr);
	sched->inesting--;

	if (s & XN_ISR_ENABLE)
	    xnarch_isr_enable_irq(irq);

	if (s & XN_ISR_CHAINED)
	    xnarch_isr_chain_irq(irq);
	}

    /* If an interrupt service task has been given AND if the raw
       interrupt handler asked for the interrupt service task to be
       scheduled (or if no raw interrupt handler exists), resume the
       interrupt service thread immediately. */

    if (intr->ist != NULL && (s & XN_ISR_SCHED_T) != 0)
	{
	if (xnpod_priocompare(intr->svcthread.cprio,intr->priority) < 0)
	    xnpod_renice_isvc(&intr->svcthread,intr->priority);
	}
    else
	{
	intr->pending = 0;

	if (testbits(nkpod->status,XNSCHED))
	    xnpod_schedule_runnable(xnpod_current_thread(),XNPOD_SCHEDLIFO);
	}
}

/*@{*/

EXPORT_SYMBOL(xnintr_attach);
EXPORT_SYMBOL(xnintr_destroy);
EXPORT_SYMBOL(xnintr_detach);
EXPORT_SYMBOL(xnintr_init);
