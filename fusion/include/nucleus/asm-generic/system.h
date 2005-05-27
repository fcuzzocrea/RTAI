/*
 * Copyright (C) 2001,2002,2003,2004,2005 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004,2005 Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * RTAI/fusion is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RTAI/fusion; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _RTAI_ASM_GENERIC_SYSTEM_H
#define _RTAI_ASM_GENERIC_SYSTEM_H

#ifdef __KERNEL__

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/adeos.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include <asm/param.h>
#include <asm/mmu_context.h>
#include <rtai_config.h>
#include <nucleus/asm/hal.h>
#include <nucleus/asm/atomic.h>
#include <nucleus/shadow.h>

#define module_param_value(parm) (parm)

typedef unsigned long spl_t;

#define splhigh(x)  rthal_local_irq_save(x)
#ifdef CONFIG_SMP
#define splexit(x)  rthal_local_irq_restore((x) & 1)
#else /* !CONFIG_SMP */
#define splexit(x)  rthal_local_irq_restore(x)
#endif /* CONFIG_SMP */
#define splnone()   rthal_sti()
#define spltest()   rthal_local_irq_test()
#define splget(x)   rthal_local_irq_flags(x)
#define splsync(x)  rthal_local_irq_sync(x)

typedef struct {

    volatile unsigned long lock;
#ifdef CONFIG_RTAI_OPT_DEBUG
    const char *file;
    const char *function;
    unsigned line;
    int cpu;
#endif /* CONFIG_RTAI_OPT_DEBUG */
} xnlock_t;

#ifndef CONFIG_RTAI_OPT_DEBUG
#define XNARCH_LOCK_UNLOCKED (xnlock_t) { 0 }
#else
#define XNARCH_LOCK_UNLOCKED (xnlock_t) {       \
        0,                                      \
        NULL,                                   \
        0,                                      \
        -1                                      \
        }
#endif /* !CONFIG_RTAI_OPT_DEBUG */

#ifdef CONFIG_SMP

#ifndef CONFIG_RTAI_OPT_DEBUG
#define xnlock_get_irqsave(lock,x)  ((x) = __xnlock_get_irqsave(lock))
#else /* !CONFIG_RTAI_OPT_DEBUG */
#define xnlock_get_irqsave(lock,x) \
    ((x) = __xnlock_get_irqsave(lock, __FILE__, __LINE__,__FUNCTION__))
#endif /* CONFIG_RTAI_OPT_DEBUG */
#define xnlock_clear_irqoff(lock)   xnlock_put_irqrestore(lock,1)
#define xnlock_clear_irqon(lock)    xnlock_put_irqrestore(lock,0)

static inline void xnlock_init (xnlock_t *lock) {

    *lock = XNARCH_LOCK_UNLOCKED;
}

#ifdef CONFIG_RTAI_OPT_DEBUG
#define XNARCH_DEBUG_SPIN_LIMIT 3000000

static inline spl_t
__xnlock_get_irqsave (xnlock_t *lock, const char *file, unsigned line, const char *function)
{
    unsigned spin_count = 0;
#else /* !CONFIG_RTAI_OPT_DEBUG */
static inline spl_t __xnlock_get_irqsave (xnlock_t *lock)
{
#endif /* CONFIG_RTAI_OPT_DEBUG */
    adeos_declare_cpuid;
    unsigned long flags;

    rthal_local_irq_save(flags);

    adeos_load_cpuid();

    if (!test_and_set_bit(cpuid,&lock->lock))
	{
        while (test_and_set_bit(BITS_PER_LONG - 1,&lock->lock))
            /* Use an non-locking test in the inner loop, as Linux'es
               bit_spin_lock. */
            while (test_bit(BITS_PER_LONG - 1,&lock->lock))
                {
                cpu_relax();

#ifdef CONFIG_RTAI_OPT_DEBUG
                if (++spin_count == XNARCH_DEBUG_SPIN_LIMIT)
                    {
                    adeos_set_printk_sync(adp_current);
                    printk(KERN_ERR
                           "RTAI: stuck on nucleus lock %p\n"
                           "      waiter = %s:%u (%s(), CPU #%d)\n"
                           "      owner  = %s:%u (%s(), CPU #%d)\n",
                           lock,file,line,function,cpuid,
                           lock->file,lock->line,lock->function,lock->cpu);
                    show_stack(NULL,NULL);
                    for (;;)
                        cpu_relax();
                    }
#endif /* CONFIG_RTAI_OPT_DEBUG */
                }

#ifdef CONFIG_RTAI_OPT_DEBUG
	lock->file = file;
	lock->function = function;
	lock->line = line;
	lock->cpu = cpuid;
#endif /* CONFIG_RTAI_OPT_DEBUG */
	}
    else
        flags |= 2;

    return flags;
}

static inline void xnlock_put_irqrestore (xnlock_t *lock, spl_t flags)

{
    if (!(flags & 2))
        {
        adeos_declare_cpuid;

        rthal_cli();

        adeos_load_cpuid();

        if (test_and_clear_bit(cpuid,&lock->lock))
            clear_bit(BITS_PER_LONG - 1,&lock->lock);
        }

    rthal_local_irq_restore(flags & 1);
}

#else /* !CONFIG_SMP */

#define xnlock_init(lock)              do { } while(0)
#define xnlock_get_irqsave(lock,x)     rthal_local_irq_save(x)
#define xnlock_put_irqrestore(lock,x)  rthal_local_irq_restore(x)
#define xnlock_clear_irqoff(lock)      rthal_cli()
#define xnlock_clear_irqon(lock)       rthal_sti()

#endif /* CONFIG_SMP */

#define XNARCH_NR_CPUS               RTHAL_NR_CPUS

#define XNARCH_ROOT_STACKSZ   0	/* Only a placeholder -- no stack */

#define XNARCH_PROMPT "RTAI: "
#define xnarch_loginfo(fmt,args...)  printk(KERN_INFO XNARCH_PROMPT fmt , ##args)
#define xnarch_logwarn(fmt,args...)  printk(KERN_WARNING XNARCH_PROMPT fmt , ##args)
#define xnarch_logerr(fmt,args...)   printk(KERN_ERR XNARCH_PROMPT fmt , ##args)
#define xnarch_printf(fmt,args...)   printk(KERN_INFO XNARCH_PROMPT fmt , ##args)

#define xnarch_ullmod(ull,uld,rem)   ({ xnarch_ulldiv(ull,uld,rem); (*rem); })
#define xnarch_uldiv(ull, d)         rthal_uldivrem(ull, d, NULL)
#define xnarch_ulmod(ull, d)         ({ u_long _rem;                    \
                                        rthal_uldivrem(ull,d,&_rem); _rem; })

#define xnarch_ullmul                rthal_ullmul
#define xnarch_uldivrem              rthal_uldivrem
#define xnarch_ulldiv                rthal_ulldiv
#define xnarch_imuldiv               rthal_imuldiv
#define xnarch_llimd                 rthal_llimd
#define xnarch_get_cpu_tsc           rthal_rdtsc

typedef cpumask_t xnarch_cpumask_t;
#ifdef CONFIG_SMP
#define xnarch_cpu_online_map            cpu_online_map
#else
#define xnarch_cpu_online_map		 cpumask_of_cpu(0)
#endif
#define xnarch_num_online_cpus()         num_online_cpus()
#define xnarch_cpu_set(cpu, mask)        cpu_set(cpu, mask)
#define xnarch_cpu_clear(cpu, mask)      cpu_clear(cpu, mask)
#define xnarch_cpus_clear(mask)          cpus_clear(mask)
#define xnarch_cpu_isset(cpu, mask)      cpu_isset(cpu, mask)
#define xnarch_cpus_and(dst, src1, src2) cpus_and(dst, src1, src2)
#define xnarch_cpus_equal(mask1, mask2)  cpus_equal(mask1, mask2)
#define xnarch_cpus_empty(mask)          cpus_empty(mask)
#define xnarch_cpumask_of_cpu(cpu)       cpumask_of_cpu(cpu)
#define xnarch_first_cpu(mask)           first_cpu(mask)
#define XNARCH_CPU_MASK_ALL              CPU_MASK_ALL

typedef struct xnarch_heapcb {

    atomic_t numaps;	/* # of active user-space mappings. */

    int kmflags;	/* Kernel memory flags (0 if vmalloc()). */

    void *heapbase;	/* Shared heap memory base. */

} xnarch_heapcb_t;

#ifdef __cplusplus
extern "C" {
#endif

static inline long long xnarch_tsc_to_ns (long long ts) {
    return xnarch_llimd(ts,1000000000,RTHAL_CPU_FREQ);
}

static inline long long xnarch_ns_to_tsc (long long ns) {
    return xnarch_llimd(ns,RTHAL_CPU_FREQ,1000000000);
}

static inline unsigned long long xnarch_get_cpu_time (void) {
    return xnarch_tsc_to_ns(xnarch_get_cpu_tsc());
}

static inline unsigned long long xnarch_get_cpu_freq (void) {
    return RTHAL_CPU_FREQ;
}

static inline unsigned xnarch_current_cpu (void) {
    return adeos_processor_id();
}

#define xnarch_declare_cpuid  adeos_declare_cpuid
#define xnarch_get_cpu(flags) adeos_get_cpu(flags)
#define xnarch_put_cpu(flags) adeos_put_cpu(flags)

#define xnarch_halt(emsg) \
do { \
    adeos_set_printk_sync(adp_current); \
    xnarch_logerr("fatal: %s\n",emsg); \
    show_stack(NULL,NULL);			\
    for (;;) cpu_relax();			\
} while(0)

static inline int xnarch_setimask (int imask)

{
    spl_t s;
    splhigh(s);
    splexit(!!imask);
    return !!s;
}

#ifdef XENO_INTR_MODULE

static inline int xnarch_hook_irq (unsigned irq,
				   void (*handler)(unsigned irq,
						   void *cookie),
				   void *cookie)
{
    return rthal_irq_request(irq,handler,cookie);
}

static inline int xnarch_release_irq (unsigned irq)

{
    return rthal_irq_release(irq);
}

static inline int xnarch_enable_irq (unsigned irq)

{
    return rthal_irq_enable(irq);
}

static inline int xnarch_disable_irq (unsigned irq)

{
    return rthal_irq_disable(irq);
}

static inline void xnarch_chain_irq (unsigned irq)

{
    rthal_irq_host_pend(irq);
}

static inline cpumask_t xnarch_set_irq_affinity (unsigned irq,
                                                 xnarch_cpumask_t affinity)
{
    return adeos_set_irq_affinity(irq,affinity);
}

#endif /* XENO_INTR_MODULE */

#ifdef XENO_HEAP_MODULE

#include <linux/mm.h>

static inline void xnarch_init_heapcb (xnarch_heapcb_t *hcb)

{
    atomic_set(&hcb->numaps,0);
    hcb->kmflags = 0;
    hcb->heapbase = NULL;
}

static inline int xnarch_remap_page_range(struct vm_area_struct *vma,
					  unsigned long uvaddr,
					  unsigned long paddr,
					  unsigned long size,
					  pgprot_t prot)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,10)
    return remap_pfn_range(vma,uvaddr,paddr >> PAGE_SHIFT,size,prot);
#else /* Linux version < 2.6.10 */
    return remap_page_range(vma,uvaddr,paddr,size,prot);
#endif /* Linux version >= 2.6.10 */
}

#endif /* XENO_HEAP_MODULE */

#ifdef __cplusplus
}
#endif

/* Dashboard and graph control. */
#define XNARCH_DECL_DISPLAY_CONTEXT();
#define xnarch_init_display_context(obj)
#define xnarch_create_display(obj,name,tag)
#define xnarch_delete_display(obj)
#define xnarch_post_graph(obj,state)
#define xnarch_post_graph_if(obj,state,cond)

#else /* !__KERNEL__ */

#include <nucleus/system.h>

#endif /* __KERNEL__ */

#endif /* !_RTAI_ASM_GENERIC_SYSTEM_H */
