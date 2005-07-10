/**
 *   @ingroup hal
 *   @file
 *
 *   Common Real-Time Abstraction Layer.
 *   Copyright &copy; 2005 Philippe Gerum.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/**
 * @defgroup hal HAL.
 *
 * Real-time hardware abstraction layer used by the real-time nucleus.
 *
 *@{*/

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/console.h>
#include <linux/kallsyms.h>
#include <asm/system.h>
#include <asm/hw_irq.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <nucleus/asm/hal.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif /* CONFIG_PROC_FS */
#include <stdarg.h>

MODULE_LICENSE("GPL");

unsigned long rthal_cpufreq_arg;
module_param_named(cpufreq,rthal_cpufreq_arg,ulong,0444);

unsigned long rthal_timerfreq_arg;
module_param_named(timerfreq,rthal_timerfreq_arg,ulong,0444);

static struct {

    void (*handler)(unsigned irq, void *cookie);
    void *cookie;
    unsigned long hits[RTHAL_NR_CPUS];

} rthal_realtime_irq[IPIPE_NR_IRQS];

static struct {

    unsigned long flags;
    int count;

} rthal_linux_irq[IPIPE_NR_XIRQS];

static struct {

    void (*handler)(void *cookie);
    void *cookie;
    const char *name;
    unsigned long hits[RTHAL_NR_CPUS];

} rthal_apc_table[RTHAL_NR_APCS];

static int rthal_init_done;

static unsigned rthal_apc_virq;

static unsigned long rthal_apc_map;

static unsigned long rthal_apc_pending[RTHAL_NR_CPUS];

static raw_spinlock_t rthal_apc_lock = RAW_SPIN_LOCK_UNLOCKED;

static atomic_t rthal_sync_count = ATOMIC_INIT(1);

adomain_t rthal_domain;

struct rthal_calibration_data rthal_tunables;

rthal_trap_handler_t rthal_trap_handler;

int rthal_realtime_faults[ADEOS_NR_CPUS][ADEOS_NR_FAULTS];

volatile int rthal_sync_op;

volatile unsigned long rthal_cpu_realtime;

unsigned long rthal_critical_enter (void (*synch)(void))

{
    unsigned long flags = adeos_critical_enter(synch);

    if (atomic_dec_and_test(&rthal_sync_count))
	rthal_sync_op = 0;
    else if (synch != NULL)
	printk(KERN_WARNING "RTAI: Nested critical sync will fail.\n");

    return flags;
}

void rthal_critical_exit (unsigned long flags)

{
    atomic_inc(&rthal_sync_count);
    adeos_critical_exit(flags);
}

static void rthal_irq_trampoline (unsigned irq)

{
    rthal_realtime_irq[irq].hits[adeos_processor_id()]++;
    rthal_realtime_irq[irq].handler(irq,rthal_realtime_irq[irq].cookie);
}

/**
 * @fn int rthal_irq_request(unsigned irq,
                             void (*handler)(unsigned irq, void *cookie),
			     int (*ackfn)(unsigned irq),
			     void *cookie)
 *                           
 * @brief Install a real-time interrupt handler.
 *
 * Installs an interrupt handler for the specified IRQ line by
 * requesting the appropriate Adeos virtualization service. The
 * handler is invoked by Adeos on behalf of the RTAI domain context.
 * Once installed, the HAL interrupt handler will be called prior to
 * the regular Linux handler for the same interrupt
 * source.
 *
 * @param irq The hardware interrupt channel to install a handler on.
 * This value is architecture-dependent.
 *
 * @param handler The address of a valid interrupt service routine.
 * This handler will be called each time the corresponding IRQ is
 * delivered, and will be passed the @a cookie value unmodified.
 *
 * @param ackfn The address of an optional interrupt acknowledge
 * routine, aimed at replacing the one provided by Adeos. Only very
 * specific situations actually require to override the default Adeos
 * setting for this parameter, like having to acknowledge non-standard
 * PIC hardware. @a ackfn should return a non-zero value to indicate
 * that the interrupt has been properly acknowledged. If @a ackfn is
 * NULL, the default Adeos routine will be used instead.
 *
 * @param cookie A user-defined opaque cookie the HAL will pass to the
 * interrupt handler as its sole argument.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EBUSY is returned if an interrupt handler is already installed.
 * rthal_irq_release() must be issued first before a handler is
 * installed anew.
 *
 * - -EINVAL is returned if @a irq is invalid or @a handler is NULL.
 *
 * - Other error codes might be returned in case some internal error
 * happens at the Adeos level. Such error might caused by conflicting
 * Adeos requests made by third-party code.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Any domain context.
 */

int rthal_irq_request (unsigned irq,
		       void (*handler)(unsigned irq, void *cookie),
		       int (*ackfn)(unsigned irq),
		       void *cookie)
{
    unsigned long flags;
    int err = 0;

    if (handler == NULL || irq >= IPIPE_NR_IRQS)
	return -EINVAL;

    flags = rthal_critical_enter(NULL);

    if (rthal_realtime_irq[irq].handler != NULL)
	{
	err = -EBUSY;
	goto unlock_and_exit;
	}

    err = adeos_virtualize_irq_from(&rthal_domain,
				    irq,
				    &rthal_irq_trampoline,
				    ackfn,
				    IPIPE_DYNAMIC_MASK);
    if (!err)
	{
	rthal_realtime_irq[irq].handler = handler;
	rthal_realtime_irq[irq].cookie = cookie;
	}

 unlock_and_exit:

    rthal_critical_exit(flags);

    return err;
}

/**
 * @fn int rthal_irq_release(unsigned irq)
 *                           
 * @brief Uninstall a real-time interrupt handler.
 *
 * Uninstalls an interrupt handler previously attached using the
 * rthal_request_irq() service.
 *
 * @param irq The hardware interrupt channel to uninstall a handler
 * from.  This value is architecture-dependent.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a irq is invalid.
 *
 * - Other error codes might be returned in case some internal error
 * happens at the Adeos level. Such error might caused by conflicting
 * Adeos requests made by third-party code.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Any domain context.
 */

int rthal_irq_release (unsigned irq)

{
    int err = 0;
	
    if (irq >= IPIPE_NR_IRQS)
	return -EINVAL;

    err = adeos_virtualize_irq_from(&rthal_domain,
				    irq,
				    NULL,
				    NULL,
				    IPIPE_PASS_MASK);
    if (!err)
        xchg(&rthal_realtime_irq[irq].handler,NULL);

    return err;
}

/**
 * @fn int rthal_irq_enable(unsigned irq)
 *                           
 * @brief Enable an interrupt source.
 *
 * Enables an interrupt source at PIC level. Since Adeos masks and
 * acknowledges the associated interrupt source upon IRQ receipt, this
 * action is usually needed whenever the HAL handler does not
 * propagate the IRQ event to the Linux domain, thus preventing the
 * regular Linux interrupt handling code from re-enabling said
 * source. After this call has returned, IRQs from the given source
 * will be enabled again.
 *
 * @param irq The interrupt source to enable.  This value is
 * architecture-dependent.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a irq is invalid.
 *
 * - Other error codes might be returned in case some internal error
 * happens at the Adeos level. Such error might caused by conflicting
 * Adeos requests made by third-party code.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Any domain context.
 */

int rthal_irq_enable (unsigned irq)

{
    if (irq >= IPIPE_NR_XIRQS)
	return -EINVAL;

    if (rthal_irq_descp(irq)->handler == NULL ||
	rthal_irq_descp(irq)->handler->enable == NULL)
	return -ENODEV;

    rthal_irq_descp(irq)->handler->enable(irq);

    return 0;
}

/**
 * @fn int rthal_irq_disable(unsigned irq)
 *                           
 * @brief Disable an interrupt source.
 *
 * Disables an interrupt source at PIC level. After this call has
 * returned, no more IRQs from the given source will be allowed, until
 * the latter is enabled again using rthal_irq_enable().
 *
 * @param irq The interrupt source to disable.  This value is
 * architecture-dependent.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a irq is invalid.
 *
 * - Other error codes might be returned in case some internal error
 * happens at the Adeos level. Such error might caused by conflicting
 * Adeos requests made by third-party code.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Any domain context.
 */

int rthal_irq_disable (unsigned irq)
{

    if (irq >= IPIPE_NR_XIRQS)
	return -EINVAL;

    if (rthal_irq_descp(irq)->handler == NULL ||
	rthal_irq_descp(irq)->handler->disable == NULL)
	return -ENODEV;

    rthal_irq_descp(irq)->handler->disable(irq);

    return 0;
}

/**
 * @fn int rthal_irq_host_request (unsigned irq,
                                   irqreturn_t (*handler)(int irq,
				                          void *dev_id,
							  struct pt_regs *regs),
				   char *name,
				   void *dev_id)
 *                           
 * @brief Install a shared Linux interrupt handler.
 *
 * Installs a shared interrupt handler in the Linux domain for the
 * given interrupt source.  The handler is appended to the existing
 * list of Linux handlers for this interrupt source.
 *
 * @param irq The interrupt source to attach the shared handler to.
 * This value is architecture-dependent.
 *
 * @param handler The address of a valid interrupt service routine.
 * This handler will be called each time the corresponding IRQ is
 * delivered, as part of the chain of existing regular Linux handlers
 * for this interrupt source. The handler prototype is the same as the
 * one required by the request_irq() service provided by the Linux
 * kernel.
 *
 * @param name is a symbolic name identifying the handler which will
 * get reported through the /proc/interrupts interface.
 *
 * @param dev_id is a unique device id, identical in essence to the
 * one requested by the request_irq() service.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a irq is invalid or @� handler is NULL.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Linux domain context.
 */

int rthal_irq_host_request (unsigned irq,
			    irqreturn_t (*handler)(int irq,
						   void *dev_id,
						   struct pt_regs *regs), 
			    char *name,
			    void *dev_id)
{
    unsigned long flags;

    if (irq >= IPIPE_NR_XIRQS || !handler)
	return -EINVAL;

    spin_lock_irqsave(&rthal_irq_descp(irq)->lock,flags);

    if (rthal_linux_irq[irq].count++ == 0 && rthal_irq_descp(irq)->action)
	{
	rthal_linux_irq[irq].flags = rthal_irq_descp(irq)->action->flags;
	rthal_irq_descp(irq)->action->flags |= SA_SHIRQ;
	}

    spin_unlock_irqrestore(&rthal_irq_descp(irq)->lock,flags);

    return request_irq(irq,handler,SA_SHIRQ,name,dev_id);
}

/**
 * @fn int rthal_irq_host_release (unsigned irq,
				   void *dev_id)
 *                           
 * @brief Uninstall a shared Linux interrupt handler.
 *
 * Uninstalls a shared interrupt handler from the Linux domain for the
 * given interrupt source.  The handler is removed from the existing
 * list of Linux handlers for this interrupt source.
 *
 * @param irq The interrupt source to detach the shared handler from.
 * This value is architecture-dependent.
 *
 * @param dev_id is a valid device id, identical in essence to the one
 * requested by the free_irq() service provided by the Linux
 * kernel. This value will be used to locate the handler to remove
 * from the chain of existing Linux handlers for the given interrupt
 * source. This parameter must match the device id. passed to
 * rthal_irq_host_request() for the same handler instance.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a irq is invalid.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Linux domain context.
 */

int rthal_irq_host_release (unsigned irq, void *dev_id)

{
    unsigned long flags;

    if (irq >= IPIPE_NR_XIRQS || rthal_linux_irq[irq].count == 0)
	return -EINVAL;

    free_irq(irq,dev_id);

    spin_lock_irqsave(&rthal_irq_descp(irq)->lock,flags);

    if (--rthal_linux_irq[irq].count == 0 && rthal_irq_descp(irq)->action)
	rthal_irq_descp(irq)->action->flags = rthal_linux_irq[irq].flags;

    spin_unlock_irqrestore(&rthal_irq_descp(irq)->lock,flags);

    return 0;
}

/**
 * @fn int rthal_irq_host_pend (unsigned irq)
 *                           
 * @brief Propagate an IRQ event to Linux.
 *
 * Causes the given IRQ to be propagated down to the Adeos pipeline to
 * the Linux kernel. This operation is typically used after the given
 * IRQ has been processed into the RTAI domain by a real-time
 * interrupt handler (see rthal_request_irq()), in case such interrupt
 * must also be handled by the Linux kernel.
 *
 * @param irq The interrupt source to detach the shared handler from.
 * This value is architecture-dependent.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a irq is invalid.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - RTAI domain context.
 */

int rthal_irq_host_pend (unsigned irq)

{
    return adeos_propagate_irq(irq);
}

/**
 * @fn int rthal_irq_affinity (unsigned irq,
                               cpumask_t cpumask,
			       cpumask_t *oldmask)
 *                           
 * @brief Set/Get processor affinity for external interrupt.
 *
 * On SMP systems, this service ensures that the given interrupt is
 * preferably dispatched to the specified set of processors. The
 * previous affinity mask is returned by this service.
 * 
 * @param irq The interrupt source whose processor affinity is
 * affected by the operation. Only external interrupts can have their
 * affinity changed/queried, thus virtual interrupt numbers allocated
 * by adeos_alloc_irq() are invalid values for this parameter.
 *
 * @param cpumask A list of CPU identifiers passed as a bitmask
 * representing the new affinity for this interrupt. A zero value
 * cause this service to return the current affinity mask without
 * changing it.
 * 
 * @param oldmask If non-NULL, a pointer to a memory area which will
 * bve overwritten by the previous affinity mask used for this
 * interrupt source, or a zeroed mask if an error occurred.  This
 * service always returns a zeroed mask on uniprocessor systems.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a irq is invalid.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Linux domain context.
 */

#ifdef CONFIG_SMP

int rthal_irq_affinity (unsigned irq, cpumask_t cpumask, cpumask_t *oldmask)

{
    cpumask_t _oldmask;

    if (irq >= IPIPE_NR_XIRQS)
	return -EINVAL;

    _oldmask = adeos_set_irq_affinity(irq,cpumask);

    if (oldmask)
	*oldmask = _oldmask;

    return cpus_empty(_oldmask) ? -EINVAL : 0;
}

#else /* !CONFIG_SMP */

int rthal_irq_affinity (unsigned irq, cpumask_t cpumask, cpumask_t *oldmask)
{
    return 0;
}

#endif /* CONFIG_SMP */

/**
 * @fn int rthal_trap_catch (rthal_trap_handler_t handler)
 *                           
 * @brief Installs a fault handler.
 *
 * The HAL attempts to invoke a fault handler whenever an uncontrolled
 * exception or fault is caught at machine level. This service allows
 * to install a user-defined handler for such events.
 *
 * @param handler The address of the fault handler to call upon
 * exception condition. The handler is passed the address of the
 * low-level information block describing the fault as passed by
 * Adeos. Its layout is implementation-dependent.
 *
 * @return The address of the fault handler previously installed.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Any domain context.
 */

rthal_trap_handler_t rthal_trap_catch (rthal_trap_handler_t handler) {

    return (rthal_trap_handler_t)xchg(&rthal_trap_handler,handler);
}

static void rthal_apc_handler (unsigned virq)

{
    void (*handler)(void *), *cookie;
    adeos_declare_cpuid;

    adeos_load_cpuid();

    rthal_spin_lock(&rthal_apc_lock);

    /* <!> This loop is not protected against a handler becoming
       unavailable while processing the pending queue; the software
       must make sure to uninstall all apcs before eventually
       unloading any module that may contain apc handlers. We keep the
       handler affinity with the poster's CPU, so that the handler is
       invoked on the same CPU than the code which called
       rthal_apc_schedule(). We spinlock for handling the CPU
       migration case; we might get rid of this some day. */

    while (rthal_apc_pending[cpuid] != 0)
	{
	int apc = ffnz(rthal_apc_pending[cpuid]);
	clear_bit(apc,&rthal_apc_pending[cpuid]);
	handler = rthal_apc_table[apc].handler;
	cookie = rthal_apc_table[apc].cookie;
	rthal_apc_table[apc].hits[cpuid]++;
	rthal_spin_unlock(&rthal_apc_lock);
	handler(cookie);
	rthal_spin_lock(&rthal_apc_lock);
	}

    rthal_spin_unlock(&rthal_apc_lock);
}

#ifdef CONFIG_PREEMPT_RT

/* On PREEMPT_RT, we need to invoke the apc handlers over a process
   context, so that the latter can access non-atomic kernel services
   properly. So the Adeos virq is only used to kick a per-CPU apc
   server process which in turns runs the apc dispatcher. A bit
   twisted, but indeed consistent with the threaded IRQ model of
   PREEMPT_RT. */

#include <linux/kthread.h>

static struct task_struct *rthal_apc_servers[RTHAL_NR_CPUS];

static int rthal_apc_thread (void *data)

{
    unsigned cpu = (unsigned)(unsigned long)data;

    set_cpus_allowed(current, cpumask_of_cpu(cpu));
    sigfillset(&current->blocked);
    current->flags |= PF_NOFREEZE;
    /* Use highest priority here, since some apc handlers might
       require to run as soon as possible after the request has been
       pended. */
    __adeos_setscheduler_root(current,SCHED_FIFO,MAX_RT_PRIO-1);

    while (!kthread_should_stop()) {
	set_current_state(TASK_INTERRUPTIBLE);
	schedule();
	rthal_apc_handler(0);
    }

    __set_current_state(TASK_RUNNING);

    return 0;
}

void rthal_apc_kicker (unsigned virq)

{
    wake_up_process(rthal_apc_servers[smp_processor_id()]);
}

#define rthal_apc_trampoline rthal_apc_kicker

#else /* !CONFIG_PREEMPT_RT */

#define rthal_apc_trampoline rthal_apc_handler

#endif /* CONFIG_PREEMPT_RT */

/**
 * @fn int rthal_apc_alloc (const char *name,
                            void (*handler)(void *cookie),
			    void *cookie)
 *
 * @brief Allocate an APC slot.
 *
 * APC is the acronym for Asynchronous Procedure Call, a mean by which
 * activities from the RTAI domain can schedule deferred invocations
 * of handlers to be run into the Linux domain, as soon as possible
 * when the Linux kernel gets back in control. Up to BITS_PER_LONG APC
 * slots can be active at any point in time. APC support is built upon
 * Adeos's virtual interrupt support.
 *
 * The HAL guarantees that any Linux kernel service which would be
 * callable from a regular Linux interrupt handler is also available
 * to APC handlers, including over PREEMPT_RT kernels exhibiting a
 * threaded IRQ model.
 *
 * @param name is a symbolic name identifying the APC which will get
 * reported through the /proc/rtai/apc interface.
 *
 * @param handler The address of the fault handler to call upon
 * exception condition. The handle will be passed the @a cookie value
 * unmodified.
 *
 * @param cookie A user-defined opaque cookie the HAL will pass to the
 * APC handler as its sole argument.
 *
 * @return an valid APC id. is returned upon success, or a negative
 * error code otherwise:
 *
 * - -EINVAL is returned if @a handler is invalid.
 *
 * - -EBUSY is returned if no more APC slots are available.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Any domain context.
 */

int rthal_apc_alloc (const char *name,
		     void (*handler)(void *cookie),
		     void *cookie)
{
    unsigned long flags;
    int apc;

    if (handler == NULL)
	return -EINVAL;

    rthal_spin_lock_irqsave(&rthal_apc_lock,flags);

    if (rthal_apc_map != ~0)
	{
	apc = ffz(rthal_apc_map);
	set_bit(apc,&rthal_apc_map);
	rthal_apc_table[apc].handler = handler;
	rthal_apc_table[apc].cookie = cookie;
	rthal_apc_table[apc].name = name;
	}
    else
	apc = -EBUSY;

    rthal_spin_unlock_irqrestore(&rthal_apc_lock,flags);

    return apc;
}

/**
 * @fn int rthal_apc_free (int apc)
 *
 * @brief Releases an APC slot.
 *
 * This service deallocates an APC slot obtained by rthal_apc_alloc().
 *
 * @param apc The APC id. to release, as returned by a successful call
 * to the rthal_apc_alloc() service.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a apc is invalid.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Any domain context.
 */

int rthal_apc_free (int apc)

{
    if (apc < 0 || apc >= RTHAL_NR_APCS ||
	!test_and_clear_bit(apc,&rthal_apc_map))
	return -EINVAL;

    return 0;
}

/**
 * @fn int rthal_apc_schedule (int apc)
 *                           
 * @brief Schedule an APC invocation.
 *
 * This service marks the APC as pending for the Linux domain, so that
 * its handler will be called as soon as possible, when the Linux
 * domain gets back in control.
 *
 * When posted from the Linux domain, the APC handler is fired as soon
 * as the interrupt mask is explicitely cleared by some kernel
 * code. When posted from the RTAI domain, the APC handler is fired as
 * soon as the Linux domain is resumed, i.e. after RTAI has completed
 * all its pending duties.
 *
 * @param apc The APC id. to schedule.
 *
 * @return 0 or 1 are returned upon success, the former meaning that
 * the APC was already pending. Otherwise:
 *
 * - -EINVAL is returned if @a apc is invalid.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Any domain context, albeit the usual calling place is from the
 * RTAI domain.
 */

int rthal_apc_schedule (int apc)

{
    adeos_declare_cpuid;

    if (apc < 0 || apc >= RTHAL_NR_APCS)
	return -EINVAL;

    adeos_load_cpuid();	/* Migration would be harmless here. */

    if (!test_and_set_bit(apc,&rthal_apc_pending[cpuid]))
	{
	adeos_schedule_irq(rthal_apc_virq);
	return 1;
	}

    return 0;	/* Already pending. */
}

#ifdef CONFIG_PROC_FS

struct proc_dir_entry *rthal_proc_root;

static int hal_read_proc (char *page,
			  char **start,
			  off_t off,
			  int count,
			  int *eof,
			  void *data)
{
    int len, major, minor, patchlevel;

    /* Canonicalize the Adeos relno-candidate information to some
       major.minor.patchlevel format to be parser-friendly. */

    major = ADEOS_MAJOR_NUMBER;

    if (ADEOS_MINOR_NUMBER < 255)
	{
	--major;
	minor = 99;
	patchlevel = ADEOS_MINOR_NUMBER;
	}
    else
	{
	minor = 0;
	patchlevel = 0;
	}

    len = sprintf(page,"%d.%d.%d\n",major,minor,patchlevel);
    len -= off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if(len > count) len = count;
    if(len < 0) len = 0;

    return len;
}

static int compiler_read_proc (char *page,
			       char **start,
			       off_t off,
			       int count,
			       int *eof,
			       void *data)
{
    int len;

    len = sprintf(page,"%s\n",CONFIG_RTAI_COMPILER);
    len -= off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if(len > count) len = count;
    if(len < 0) len = 0;

    return len;
}

static int irq_read_proc (char *page,
			  char **start,
			  off_t off,
			  int count,
			  int *eof,
			  void *data)
{
    int len = 0, cpu, irq;
    char *p = page;

    p += sprintf(p,"IRQ ");

    for_each_online_cpu(cpu) {
	p += sprintf(p,"        CPU%d",cpu);
    }

    for (irq = 0; irq < IPIPE_NR_IRQS; irq++) {

	if (rthal_realtime_irq[irq].handler == NULL)
	    continue;

	p += sprintf(p,"\n%3d:",irq);

	for_each_online_cpu(cpu) {
	    p += sprintf(p,"%12lu",rthal_realtime_irq[irq].hits[cpu]);
	}
    }

    p += sprintf(p,"\n");

    len = p - page - off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if (len > count) len = count;
    if (len < 0) len = 0;

    return len;
}

static int faults_read_proc (char *page,
			     char **start,
			     off_t off,
			     int count,
			     int *eof,
			     void *data)
{
    int len = 0, cpu, trap;
    char *p = page;

    p += sprintf(p,"TRAP ");

    for_each_online_cpu(cpu) {
	p += sprintf(p,"        CPU%d",cpu);
    }

    for (trap = 0; rthal_fault_labels[trap] != NULL; trap++) {

	if (!*rthal_fault_labels[trap])
	    continue;

	p += sprintf(p,"\n%3d: ",trap);

	for_each_online_cpu(cpu) {
	    p += sprintf(p,"%12d",
			 rthal_realtime_faults[cpu][trap]);
	}

	p += sprintf(p,"    (%s)",rthal_fault_labels[trap]);
    }

    p += sprintf(p,"\n");

    len = p - page - off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if (len > count) len = count;
    if (len < 0) len = 0;

    return len;
}

static int apc_read_proc (char *page,
			     char **start,
			     off_t off,
			     int count,
			     int *eof,
			     void *data)
{
    int len = 0, cpu, apc;
    char *p = page;

    p += sprintf(p,"APC ");

    for_each_online_cpu(cpu) {
	p += sprintf(p,"         CPU%d",cpu);
    }

    for (apc = 0; apc < BITS_PER_LONG; apc++)
	{
	if (!test_bit(apc,&rthal_apc_map))
	    continue;	/* Not hooked. */

	p += sprintf(p,"\n%3d: ",apc);

	for_each_online_cpu(cpu) {
	    p += sprintf(p,"%12lu",
			 rthal_apc_table[apc].hits[cpu]);
	}

	p += sprintf(p,"    (%s)",rthal_apc_table[apc].name);
	}

    p += sprintf(p,"\n");

    len = p - page - off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if (len > count) len = count;
    if (len < 0) len = 0;

    return len;
}

static struct proc_dir_entry *add_proc_leaf (const char *name,
					     read_proc_t rdproc,
					     write_proc_t wrproc,
					     void *data,
					     struct proc_dir_entry *parent)
{
    int mode = wrproc ? 0644 : 0444;
    struct proc_dir_entry *entry;

    entry = create_proc_entry(name,mode,parent);

    if (entry)
	{
	entry->nlink = 1;
	entry->data = data;
	entry->read_proc = rdproc;
	entry->write_proc = wrproc;
	entry->owner = THIS_MODULE;
	}

    return entry;
}

static int rthal_proc_register (void)

{
    rthal_proc_root = create_proc_entry("rtai",S_IFDIR, 0);

    if (!rthal_proc_root)
	{
	printk(KERN_ERR "RTAI: Unable to initialize /proc/rtai.\n");
	return -1;
        }

    rthal_proc_root->owner = THIS_MODULE;

    add_proc_leaf("hal",
		  &hal_read_proc,
		  NULL,
		  NULL,
		  rthal_proc_root);

    add_proc_leaf("compiler",
		  &compiler_read_proc,
		  NULL,
		  NULL,
		  rthal_proc_root);

    add_proc_leaf("irq",
		  &irq_read_proc,
		  NULL,
		  NULL,
		  rthal_proc_root);

    add_proc_leaf("faults",
		  &faults_read_proc,
		  NULL,
		  NULL,
		  rthal_proc_root);

    add_proc_leaf("apc",
		  &apc_read_proc,
		  NULL,
		  NULL,
		  rthal_proc_root);
    return 0;
}

static void rthal_proc_unregister (void)

{
    remove_proc_entry("hal",rthal_proc_root);
    remove_proc_entry("compiler",rthal_proc_root);
    remove_proc_entry("irq",rthal_proc_root);
    remove_proc_entry("faults",rthal_proc_root);
    remove_proc_entry("apc",rthal_proc_root);
    remove_proc_entry("rtai",NULL);
}

#endif /* CONFIG_PROC_FS */

int __rthal_init (void)

{
    adattr_t attr;
    int err;

#ifdef CONFIG_SMP
    /* The nucleus also sets the same CPU affinity so that both
       modules keep their execution sequence on SMP boxen. */
    set_cpus_allowed(current,cpumask_of_cpu(0));
#endif /* CONFIG_SMP */

    err = rthal_arch_init();

    if (err)
	goto out;

    /* The arch-dependent support must have updated the frequency args
       as required. */
    rthal_tunables.cpu_freq = rthal_cpufreq_arg;
    rthal_tunables.timer_freq = rthal_timerfreq_arg;

    /* Allocate a virtual interrupt to handle apcs within the Linux
       domain. */
    rthal_apc_virq = adeos_alloc_irq();

    if (!rthal_apc_virq)
    {
        printk(KERN_ERR "RTAI: No virtual interrupt available.\n");
	    err = -EBUSY;
        goto out_arch_cleanup;
    }

    err = adeos_virtualize_irq(rthal_apc_virq,
			       &rthal_apc_trampoline,
			       NULL,
			       IPIPE_HANDLE_MASK);
    if (err)
    {
        printk(KERN_ERR "RTAI: Failed to virtualize IRQ.\n");
        goto out_free_irq;
    }

#ifdef CONFIG_PREEMPT_RT
    {
    int cpu;
    for_each_online_cpu(cpu) {
       rthal_apc_servers[cpu] =
	   kthread_create(&rthal_apc_thread,(void *)(unsigned long)cpu,"apc/%d",cpu);
       if (!rthal_apc_servers[cpu])
	   goto out_kthread_stop;
       wake_up_process(rthal_apc_servers[cpu]);
      }
    }
#endif /* CONFIG_PREEMPT_RT */

#ifdef CONFIG_PROC_FS
    rthal_proc_register();
#endif /* CONFIG_PROC_FS */

    /* Let Adeos do its magic for our real-time domain. */
    adeos_init_attr(&attr);
    attr.name = "RTAI";
    attr.domid = RTHAL_DOMAIN_ID;
    attr.entry = &rthal_domain_entry;
    attr.priority = ADEOS_ROOT_PRI + 100; /* Precede Linux in the pipeline */

    err = adeos_register_domain(&rthal_domain,&attr);

    if (!err)
    	rthal_init_done = 1;
    else 
    {
        printk(KERN_ERR "RTAI: Domain registration failed.\n");
        goto out_proc_unregister;
    }

    return 0;

out_proc_unregister:
#ifdef CONFIG_PROC_FS
    rthal_proc_unregister();
#endif
#ifdef CONFIG_PREEMPT_RT
out_kthread_stop:
    {
    int cpu;
    for_each_online_cpu(cpu) {
        if (rthal_apc_servers[cpu])
            kthread_stop(rthal_apc_servers[cpu]);
      }
    }
#endif /* CONFIG_PREEMPT_RT */
    adeos_virtualize_irq(rthal_apc_virq,NULL,NULL,0);
   
out_free_irq:
    adeos_free_irq(rthal_apc_virq);

 out_arch_cleanup:
    rthal_arch_cleanup();

 out:
    return err;
}

void __rthal_exit (void)

{
#ifdef CONFIG_SMP
    /* The nucleus also sets the same CPU affinity so that both
       modules keep their execution sequence on SMP boxen. */
    set_cpus_allowed(current,cpumask_of_cpu(0));
#endif /* CONFIG_SMP */

#ifdef CONFIG_PROC_FS
    rthal_proc_unregister();
#endif /* CONFIG_PROC_FS */

    if (rthal_apc_virq)
	{
	adeos_virtualize_irq(rthal_apc_virq,NULL,NULL,0);
	adeos_free_irq(rthal_apc_virq);
#ifdef CONFIG_PREEMPT_RT
	{
	int cpu;
	for_each_online_cpu(cpu) {
            kthread_stop(rthal_apc_servers[cpu]);
	  }
	}
#endif /* CONFIG_PREEMPT_RT */
	}

    if (rthal_init_done)
	adeos_unregister_domain(&rthal_domain);

    rthal_arch_cleanup();
}

/*! 
 * \fn int rthal_timer_request(void (*handler)(void),
                               unsigned long nstick)
 * \brief Grab the hardware timer.
 *
 * rthal_timer_request() grabs and tunes the hardware timer so that a
 * user-defined routine is called according to a given frequency. On
 * architectures that provide a oneshot-programmable time source, the
 * hardware timer can operate either in aperiodic or periodic
 * mode. Using the aperiodic mode still allows to run periodic timings
 * over it: the underlying hardware simply needs to be reprogrammed
 * after each tick using the appropriate interval value
 *
 * The time interval that elapses between two consecutive invocations
 * of the handler is called a tick. The user-supplied handler will
 * always be invoked on behalf of the RTAI domain for each incoming
 * tick.
 *
 * @param handler The address of the tick handler which will process
 * each incoming tick.
 *
 * @param nstick The timer period in nanoseconds. If this parameter is
 * zero, the underlying hardware timer is set to operate in
 * oneshot-programming mode. In this mode, timing accuracy is higher -
 * since it is not rounded to a constant time slice - at the expense
 * of a lesser efficicency due to the timer chip programming
 * duties. On the other hand, the shorter the period, the higher the
 * overhead induced by the periodic mode, since the handler will end
 * up consuming a lot of CPU power to process useless ticks.
 *
 * @return 0 is returned on success. Otherwise:
 *
 * - -EBUSY is returned if the hardware timer has already been
 * grabbed.  rthal_timer_request() must be issued before
 * rthal_timer_request() is called again.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Linux domain context.
 */

/*! 
 * \fn void rthal_timer_release(void)
 * \brief Release the hardware timer.
 *
 * Releases the hardware timer, thus reverting the effect of a
 * previous call to rthal_timer_request(). In case the timer hardware
 * is shared with Linux, a periodic setup suitable for the Linux
 * kernel will be reset.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Linux domain context.
 */

/*@}*/

module_init(__rthal_init);
module_exit(__rthal_exit);

EXPORT_SYMBOL(rthal_irq_request);
EXPORT_SYMBOL(rthal_irq_release);
EXPORT_SYMBOL(rthal_irq_enable);
EXPORT_SYMBOL(rthal_irq_disable);
EXPORT_SYMBOL(rthal_irq_host_request);
EXPORT_SYMBOL(rthal_irq_host_release);
EXPORT_SYMBOL(rthal_irq_host_pend);
EXPORT_SYMBOL(rthal_irq_affinity);
EXPORT_SYMBOL(rthal_trap_catch);
EXPORT_SYMBOL(rthal_timer_request);
EXPORT_SYMBOL(rthal_timer_release);
EXPORT_SYMBOL(rthal_timer_calibrate);
EXPORT_SYMBOL(rthal_apc_alloc);
EXPORT_SYMBOL(rthal_apc_free);
EXPORT_SYMBOL(rthal_apc_schedule);

EXPORT_SYMBOL(rthal_critical_enter);
EXPORT_SYMBOL(rthal_critical_exit);

EXPORT_SYMBOL(rthal_domain);
EXPORT_SYMBOL(rthal_tunables);
EXPORT_SYMBOL(rthal_cpu_realtime);
#ifdef CONFIG_PROC_FS
EXPORT_SYMBOL(rthal_proc_root);
#endif /* CONFIG_PROC_FS */
