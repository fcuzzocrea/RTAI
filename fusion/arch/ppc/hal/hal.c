/**
 *   @ingroup hal_ppc
 *   @file
 *
 *   Adeos-based Real-Time Abstraction Layer for PPC.
 *
 *   Original RTAI/x86 layer implementation: \n
 *   Copyright &copy; 2000 Paolo Mantegazza, \n
 *   Copyright &copy; 2000 Steve Papacharalambous, \n
 *   Copyright &copy; 2000 Stuart Hughes, \n
 *   and others.
 *
 *   RTAI/x86 rewrite over Adeos: \n
 *   Copyright &copy; 2002 Philippe Gerum.
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
 * @defgroup hal_ppc HAL/ppc.
 *
 * Basic PowerPC-dependent services used by the real-time nucleus.
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
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <nucleus/asm/hal.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif /* CONFIG_PROC_FS */
#include <stdarg.h>

MODULE_LICENSE("GPL");

static unsigned long rthal_cpufreq_arg;
MODULE_PARM(rthal_cpufreq_arg,"i");

static unsigned long rthal_timerfreq_arg;
MODULE_PARM(rthal_timerfreq_arg,"i");

extern struct desc_struct idt_table[];

adomain_t rthal_domain;

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

    void (*handler)(void);
    unsigned label;

} rthal_sysreq_table[RTHAL_NR_SRQS];

static int rthal_init_done;

static unsigned rthal_sysreq_virq;

static unsigned long rthal_sysreq_map = 1; /* #0 is invalid. */

static unsigned long rthal_sysreq_pending;

static unsigned long rthal_sysreq_running;

static spinlock_t rthal_ssrq_lock = SPIN_LOCK_UNLOCKED;

static volatile int rthal_sync_op;

static atomic_t rthal_sync_count = ATOMIC_INIT(1);

static rthal_trap_handler_t rthal_trap_handler;

static int rthal_periodic_p;

struct rthal_calibration_data rthal_tunables;

volatile unsigned long rthal_cpu_realtime;

int rthal_request_timer (void (*handler)(void),
			 unsigned long nstick)
{
    unsigned long flags;
    int err;

    flags = rthal_critical_enter(NULL);

    if (nstick > 0)
	{
	/* Periodic setup --
	   Use the built-in Adeos service directly. */
	err = adeos_tune_timer(nstick,0);
	rthal_periodic_p = 1;
	}
    else
	{
	/* Oneshot setup. */
	disarm_decr[adeos_processor_id()] = 1;
	rthal_periodic_p = 0;
#ifdef CONFIG_40x
        mtspr(SPRN_TCR,mfspr(SPRN_TCR) & ~TCR_ARE); /* Auto-reload off. */
#endif /* CONFIG_40x */
	rthal_set_timer_shot(tb_ticks_per_jiffy);
	}

    rthal_release_irq(RTHAL_TIMER_IRQ);

    err = rthal_request_irq(RTHAL_TIMER_IRQ,
			    (rthal_irq_handler_t)handler,
			    NULL);

    rthal_critical_exit(flags);

    return err;
}

void rthal_release_timer (void)

{
    unsigned long flags;

    flags = rthal_critical_enter(NULL);

    if (rthal_periodic_p)
	adeos_tune_timer(0,ADEOS_RESET_TIMER);
    else
	{
	disarm_decr[adeos_processor_id()] = 0;
#ifdef CONFIG_40x
	mtspr(SPRN_TCR,mfspr(SPRN_TCR)|TCR_ARE); /* Auto-reload on. */
	mtspr(SPRN_PIT,tb_ticks_per_jiffy);
#else /* !CONFIG_40x */
	set_dec(tb_ticks_per_jiffy);
#endif /* CONFIG_40x */
	}

    rthal_release_irq(RTHAL_TIMER_IRQ);

    rthal_critical_exit(flags);
}

unsigned long rthal_calibrate_timer (void) {
    /* On PowerPC systems, the cost of setting the decrementer or the
       PIT does not induce significant latency. In such a case, let's
       return the shortest possible delay for a one-shot setup. In any
       case, always return a non-zero value.  e.g. 1 decrementer tick
       here. */
    return rthal_llimd(1,1000000000,RTHAL_CPU_FREQ); /* Convert as ns. */
}

unsigned long rthal_critical_enter (void (*synch)(void))

{
    unsigned long flags = adeos_critical_enter(synch);

    if (atomic_dec_and_test(&rthal_sync_count))
	rthal_sync_op = 0;
    else if (synch != NULL)
	printk(KERN_WARNING "RTAI[hal]: Nested sync will fail.\n");

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

int rthal_request_irq (unsigned irq,
		       void (*handler)(unsigned irq, void *cookie),
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
				    NULL,
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

int rthal_release_irq (unsigned irq)

{
    if (irq >= IPIPE_NR_IRQS)
	return -EINVAL;

    adeos_virtualize_irq_from(&rthal_domain,
			      irq,
			      NULL,
			      NULL,
			      IPIPE_PASS_MASK);

    xchg(&rthal_realtime_irq[irq].handler,NULL);

    return 0;
}

/**
 * Enable an IRQ source.
 *
 */
int rthal_enable_irq (unsigned irq)

{
    if (irq >= IPIPE_NR_XIRQS)
	return -EINVAL;

    if (irq_desc[irq].handler == NULL ||
	irq_desc[irq].handler->enable == NULL)
	return -ENODEV;

    irq_desc[irq].handler->enable(irq);

    return 0;
}

/**
 * Disable an IRQ source.
 *
 */
int rthal_disable_irq (unsigned irq)

{
    if (irq >= IPIPE_NR_XIRQS)
	return -EINVAL;

    if (irq_desc[irq].handler == NULL ||
	irq_desc[irq].handler->disable == NULL)
	return -ENODEV;

    irq_desc[irq].handler->disable(irq);

    return 0;
}

/**
 * Install a shared Linux interrupt handler.
 *
 * rthal_request_linux_irq installs function @a handler as a standard
 * Linux interrupt service routine for IRQ level @a irq forcing Linux
 * to share the IRQ with other interrupt handlers. The handler is
 * appended to any already existing Linux handler for the same irq and
 * is run by Linux irq as any of its handler. In this way a real time
 * application can monitor Linux interrupts handling at its will. The
 * handler appears in /proc/interrupts.
 *
 * @param irq is the IRQ level to which the handler will be associated.
 *
 * @param handler pointer on the interrupt service routine to be installed.
 *
 * @param name is a name for /proc/interrupts.
 *
 * @param dev_id is to pass to the interrupt handler, in the same way as the
 * standard Linux irq request call.
 *
 * The interrupt service routine can be uninstalled using
 * rthal_release_linux_irq().
 *
 * @retval 0 on success.
 * @retval -EINVAL if @a irq is not a valid external IRQ number or handler
 * is @c NULL.
 */
int rthal_request_linux_irq (unsigned irq,
			     irqreturn_t (*handler)(int irq,
						    void *dev_id,
						    struct pt_regs *regs), 
			     char *name,
			     void *dev_id)
{
    unsigned long flags;

    if (irq >= IPIPE_NR_XIRQS || !handler)
	return -EINVAL;

    rthal_local_irq_save(flags);

    rthal_spin_lock(&irq_desc[irq].lock);

    if (rthal_linux_irq[irq].count++ == 0 && irq_desc[irq].action)
	{
	rthal_linux_irq[irq].flags = irq_desc[irq].action->flags;
	irq_desc[irq].action->flags |= SA_SHIRQ;
	}

    rthal_spin_unlock(&irq_desc[irq].lock);

    rthal_local_irq_restore(flags);

    request_irq(irq,handler,SA_SHIRQ,name,dev_id);

    return 0;
}

/**
 * Uninstall shared Linux interrupt handler.
 *
 * @param dev_id is to pass to the interrupt handler, in the same way as the
 * standard Linux irq request call.
 *
 * @param irq is the IRQ level of the interrupt handler to be freed.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @a irq is not a valid external IRQ number.
 */
int rthal_release_linux_irq (unsigned irq, void *dev_id)

{
    unsigned long flags;

    if (irq >= IPIPE_NR_XIRQS ||
	rthal_linux_irq[irq].count == 0)
	return -EINVAL;

    rthal_local_irq_save(flags);

    free_irq(irq,dev_id);

    rthal_spin_lock(&irq_desc[irq].lock);

    if (--rthal_linux_irq[irq].count == 0 && irq_desc[irq].action)
	irq_desc[irq].action->flags = rthal_linux_irq[irq].flags;

    rthal_spin_unlock(&irq_desc[irq].lock);

    rthal_local_irq_restore(flags);

    return 0;
}

/**
 * Pend an IRQ to Linux.
 *
 * rthal_pend_linux_irq appends a Linux interrupt irq for processing in Linux IRQ
 * mode, i.e. with hardware interrupts fully enabled.
 */
int rthal_pend_linux_irq (unsigned irq)

{
    if (irq >= IPIPE_NR_IRQS)
	return -EINVAL;

    return adeos_propagate_irq(irq);
}

/**
 * Install a system request handler
 *
 * rthal_request_srq installs a RTAI system request (srq) by assigning
 * @a handler, the function to be called in kernel space following its
 * activation by a call to rthal_pend_linux_srq(). @a handler is in
 * practice used to request a service from the kernel. In fact Linux
 * system requests cannot be used safely from RTAI so you can setup a
 * handler that receives real time requests and safely executes them
 * when Linux is running.
 *
 * @return the number of the assigned system request on success.
 * @retval -EINVAL if @a handler is @c NULL.
 * @retval -EBUSY if no free srq slot is available.
 */
int rthal_request_srq (unsigned label,
		       void (*handler)(void))
{
    unsigned long flags;
    int srq;

    if (handler == NULL)
	return -EINVAL;

    flags = rthal_spin_lock_irqsave(&rthal_ssrq_lock);

    if (rthal_sysreq_map != ~0)
	{
	srq = ffz(rthal_sysreq_map);
	set_bit(srq,&rthal_sysreq_map);
	rthal_sysreq_table[srq].handler = handler;
	rthal_sysreq_table[srq].label = label;
	}
    else
	srq = -EBUSY;

    rthal_spin_unlock_irqrestore(flags,&rthal_ssrq_lock);

    return srq;
}

/**
 * Uninstall a system request handler
 *
 * rthal_release_srq uninstalls the specified system call @a srq, returned by
 * installing the related handler with a previous call to rthal_request_srq().
 *
 * @retval EINVAL if @a srq is invalid.
 */
int rthal_release_srq (unsigned srq)

{
    if (srq < 1 ||
	srq >= RTHAL_NR_SRQS ||
	!test_and_clear_bit(srq,&rthal_sysreq_map))
	return -EINVAL;

    return 0;
}

/**
 * Append a Linux IRQ.
 *
 * rthal_pend_linux_srq appends a system call request srq to be used as a service
 * request to the Linux kernel.
 *
 * @param srq is the value returned by rthal_request_srq.
 */
int rthal_pend_linux_srq (unsigned srq)

{
    if (srq > 0 && srq < RTHAL_NR_SRQS)
	{
	if (!test_and_set_bit(srq,&rthal_sysreq_pending))
	    {
	    adeos_schedule_irq(rthal_sysreq_virq);
	    return 1;
	    }

	return 0;	/* Already pending. */
	}

    return -EINVAL;
}

#ifdef CONFIG_SMP

int rthal_set_irq_affinity (unsigned irq, cpumask_t cpumask, cpumask_t *oldmask)

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

int rthal_set_irq_affinity (unsigned irq, cpumask_t cpumask, cpumask_t *oldmask) {

    return 0;
}

#endif /* CONFIG_SMP */

rthal_trap_handler_t rthal_set_trap_handler (rthal_trap_handler_t handler) {

    return (rthal_trap_handler_t)xchg(&rthal_trap_handler,handler);
}

static void rthal_trap_fault (adevinfo_t *evinfo)

{
    adeos_declare_cpuid;

    adeos_load_cpuid();

    if (evinfo->domid == RTHAL_DOMAIN_ID &&
	rthal_trap_handler != NULL &&
	test_bit(cpuid,&rthal_cpu_realtime) &&
	rthal_trap_handler(evinfo) != 0)
	return;

    adeos_propagate_event(evinfo);
}

static void rthal_ssrq_trampoline (unsigned virq)

{
    unsigned long pending;

    rthal_spin_lock(&rthal_ssrq_lock);

    while ((pending = rthal_sysreq_pending & ~rthal_sysreq_running) != 0)
	{
	unsigned srq = ffnz(pending);
	set_bit(srq,&rthal_sysreq_running);
	clear_bit(srq,&rthal_sysreq_pending);
	rthal_spin_unlock(&rthal_ssrq_lock);

	if (test_bit(srq,&rthal_sysreq_map))
	    rthal_sysreq_table[srq].handler();

	clear_bit(srq,&rthal_sysreq_running);
	rthal_spin_lock(&rthal_ssrq_lock);
	}

    rthal_spin_unlock(&rthal_ssrq_lock);
}

static int errno;

static inline _syscall3(int,
			sched_setscheduler,
			pid_t,pid,
			int,policy,
			struct sched_param *,param)

void rthal_set_linux_task_priority (struct task_struct *task, int policy, int prio)

{
    struct sched_param __user param;
    mm_segment_t old_fs;
    int rc;

    param.sched_priority = prio;
    old_fs = get_fs();
    set_fs(KERNEL_DS);
    rc = sched_setscheduler(task->pid,policy,&param);
    set_fs(old_fs);

    if (rc)
	printk(KERN_ERR "RTAI[hal]: setscheduler(policy=%d,prio=%d)=%d (%s -- pid=%d)\n",
	       policy,
	       prio,
	       rc,
	       task->comm,
	       task->pid);
}

static void rthal_domain_entry (int iflag)

{
    unsigned trapnr;

#if !defined(CONFIG_ADEOS_NOTHREADS)
    if (!iflag)
	goto spin;
#endif /* !CONFIG_ADEOS_NOTHREADS */

    /* Trap all faults. */
    for (trapnr = 0; trapnr < ADEOS_NR_FAULTS; trapnr++)
	adeos_catch_event(trapnr,&rthal_trap_fault);

    printk(KERN_INFO "RTAI[hal]: Loaded over Adeos %s.\n",ADEOS_VERSION_STRING);

#if !defined(CONFIG_ADEOS_NOTHREADS)
 spin:

    for (;;)
	adeos_suspend_domain();
#endif /* !CONFIG_ADEOS_NOTHREADS */
}

#ifdef CONFIG_PROC_FS

struct proc_dir_entry *rthal_proc_root;

static ssize_t hal_read_proc (char *page,
			      char **start,
			      off_t off,
			      int count,
			      int *eof,
			      void *data)
{
    int len;

    len = sprintf(page,"%s\n",ADEOS_VERSION_STRING);
    len -= off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if(len > count) len = count;
    if(len < 0) len = 0;

    return len;
}

static ssize_t compiler_read_proc (char *page,
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
    int len = 0, cpuid, irq;
    char *p = page;

    p += sprintf(p,"IRQ  ");

    for (cpuid = 0; cpuid < num_online_cpus(); cpuid++)
	p += sprintf(p,"        CPU%d",cpuid);

    for (irq = 0; irq < IPIPE_NR_IRQS; irq++)
	{
	if (rthal_realtime_irq[irq].handler == NULL)
	    continue;

	p += sprintf(p,"\n%3d:",irq);

	for (cpuid = 0; cpuid < num_online_cpus(); cpuid++)
	    p += sprintf(p,"  %12lu",rthal_realtime_irq[irq].hits[cpuid]);
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
	printk(KERN_ERR "RTAI[hal]: Unable to initialize /proc/rtai.\n");
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
    return 0;
}

static void rthal_proc_unregister (void)

{
    remove_proc_entry("hal",rthal_proc_root);
    remove_proc_entry("compiler",rthal_proc_root);
    remove_proc_entry("irq",rthal_proc_root);
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

    /* Allocate a virtual interrupt to handle sysreqs within the Linux
       domain. */
    rthal_sysreq_virq = adeos_alloc_irq();

    if (!rthal_sysreq_virq)
	{
	printk(KERN_ERR "RTAI[hal]: No virtual interrupt available.\n");
	return -EBUSY;
	}

    adeos_virtualize_irq(rthal_sysreq_virq,
			 &rthal_ssrq_trampoline,
			 NULL,
			 IPIPE_HANDLE_MASK);

    if (rthal_cpufreq_arg == 0)
	{
	adsysinfo_t sysinfo;
	adeos_get_sysinfo(&sysinfo);
	/* The CPU frequency is expressed as the timebase frequency
	   for this port. */
	rthal_cpufreq_arg = (unsigned long)sysinfo.cpufreq;
	}

    rthal_tunables.cpu_freq = rthal_cpufreq_arg;

    if (rthal_timerfreq_arg == 0)
	rthal_timerfreq_arg = rthal_tunables.cpu_freq;

    rthal_tunables.timer_freq = rthal_timerfreq_arg;

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
#endif

    if (rthal_sysreq_virq)
	{
	adeos_virtualize_irq(rthal_sysreq_virq,NULL,NULL,0);
	adeos_free_irq(rthal_sysreq_virq);
	}

    if (rthal_init_done)
	adeos_unregister_domain(&rthal_domain);

    printk(KERN_INFO "RTAI[hal]: Unloaded.\n");
}

/*@}*/

module_init(__rthal_init);
module_exit(__rthal_exit);

EXPORT_SYMBOL(rthal_request_irq);
EXPORT_SYMBOL(rthal_release_irq);
EXPORT_SYMBOL(rthal_enable_irq);
EXPORT_SYMBOL(rthal_disable_irq);
EXPORT_SYMBOL(rthal_request_linux_irq);
EXPORT_SYMBOL(rthal_release_linux_irq);
EXPORT_SYMBOL(rthal_pend_linux_irq);
EXPORT_SYMBOL(rthal_request_srq);
EXPORT_SYMBOL(rthal_release_srq);
EXPORT_SYMBOL(rthal_pend_linux_srq);
EXPORT_SYMBOL(rthal_set_irq_affinity);
EXPORT_SYMBOL(rthal_request_timer);
EXPORT_SYMBOL(rthal_release_timer);
EXPORT_SYMBOL(rthal_set_trap_handler);
EXPORT_SYMBOL(rthal_calibrate_timer);

EXPORT_SYMBOL(rthal_critical_enter);
EXPORT_SYMBOL(rthal_critical_exit);
EXPORT_SYMBOL(rthal_set_linux_task_priority);
EXPORT_SYMBOL(rthal_switch_context);

EXPORT_SYMBOL(rthal_domain);
EXPORT_SYMBOL(rthal_tunables);
EXPORT_SYMBOL(rthal_cpu_realtime);
#ifdef CONFIG_PROC_FS
EXPORT_SYMBOL(rthal_proc_root);
#endif /* CONFIG_PROC_FS */

#ifdef CONFIG_RTAI_HW_FPU
EXPORT_SYMBOL(rthal_init_fpu);
EXPORT_SYMBOL(rthal_save_fpu);
EXPORT_SYMBOL(rthal_restore_fpu);
#endif /* CONFIG_RTAI_HW_FPU */
