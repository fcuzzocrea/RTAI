/*
 * Copyright (C) 2005 Paolo Mantegazza <mantegazza@aero.polimi.it>
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


#ifndef _RTAI_XNCORE_H
#define _RTAI_XNCORE_H

#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#include <rtai_schedcore.h>

//#include <asm/rtai_hal.h>
//#include <rtai_registry.h>
//#include <rtai_malloc.h>
//#include <rtai_sched.h>
//#include <rtai_sem.h>
//#include <rtai_lxrt.h>


#define xnthread_t  RT_TASK
#define xnticks_t   RTIME
#define xnsticks_t  RTIME
#define xnflags_t   long

#define XNSUSP    (1 << 0)
#define XNTIMEO   (1 << 1)
#define XNRMID    (1 << 2)
#define XNBREAK   (1 << 3)
#define XNDELAY   (1 << 4)
#define XNZOMBIE  (1 << 5)

#define XN_INFINITE  (0)

#define XNSYNCH_PRIO    (BIN_SEM | PRIO_Q)
#define XNSYNCH_FIFO    (BIN_SEM | FIFO_Q)
#define XNSYNCH_PIP     RES_SEM
#define XNSYNCH_SPARE0  (1 << 0)

#define XNPOD_NORMAL_EXIT  0x0

#define XNSYNCH_RESCHED  1

#define XNTMPER  1

// renamed direct use of ADEOS stuff
#define rthal_root_domain     adp_root
#define rthal_current_domain  adp_current
#define rthal_alloc_virq      adeos_alloc_irq
#define rthal_free_virq       adeos_free_irq
#define rthal_virtualize_irq  adeos_virtualize_irq_from
#define rthal_trigger_irq     adeos_trigger_irq

#ifdef __KERNEL__

// atomicity stuff

#define XNARCH_LOCK_UNLOCKED  (xnlock_t) { 0 }

typedef unsigned long spl_t;
typedef struct { volatile unsigned long lock; } xnlock_t;

#ifdef CONFIG_SMP

static inline void xnlock_init(xnlock_t *lock)
{
	*lock = XNARCH_LOCK_UNLOCKED;
}

static inline spl_t __xnlock_get_irqsave(xnlock_t *lock)
{
	unsigned long flags;

	barrier();
	rtai_save_flags_and_cli(flags);
	flags &= (1 << RTAI_IFLAG);
	if (!test_and_set_bit(adeos_processor_id(), &lock->lock)) {
		while (test_and_set_bit(31, &lock->lock)) {
			cpu_relax();
		}
		barrier();
		return flags | 1;
	}
	barrier();
	return flags;
}

#define xnlock_get_irqsave(lock, flags)  ((flags) = __xnlock_get_irqsave(lock))

static inline void xnlock_put_irqrestore(xnlock_t *lock, spl_t flags)
{
	barrier();
	rtai_cli();
	if (test_and_clear_bit(0, &flags)) {
		if (test_and_clear_bit(adeos_processor_id(), &lock->lock)) {
			test_and_clear_bit(31, &lock->lock);
			cpu_relax();
		}
	} else {
		if (!test_and_set_bit(adeos_processor_id(), &lock->lock)) {
			while (test_and_set_bit(31, &lock->lock)) {
				cpu_relax();
			}
		}
	}
	if (flags) {
		rtai_sti();
	}
	barrier();
}

#else /* !CONFIG_SMP */

#define xnlock_init(lock)                   do { } while(0)
#define xnlock_get_irqsave(lock, flags)     rtai_save_flags_and_cli(flags)
#define xnlock_put_irqrestore(lock, flags)  rtai_restore_flags(flags)

#endif /* CONFIG_SMP */

#define rthal_spin_lock_irqsave(l, f)       ((f) = rt_spin_lock_irqsave(l))
#define rthal_spin_unlock_irqrestore(l, f)  rt_spin_unlock_irqrestore(f, l)

// a lock and a structure that need defining

extern xnlock_t nklock;

#define CONFIG_RTAI_OPT_FUSION
#define CONFIG_RTAI_HW_APERIODIC_TIMER
struct xnpod { xnflags_t status; xnticks_t wallclock; };
typedef struct xnpod xnpod_t;
extern xnpod_t *nkpod;

// memory allocation

#define xnmalloc  rt_malloc
#define xnfree    rt_free

// time get/convert

#define xnarch_get_cpu_tsc  rtai_rdtsc
#define xnpod_get_cpu_time  rt_get_cpu_time_ns
#define xnpod_get_time      rt_get_cpu_time_ns
#define xnarch_ns_to_tsc    nano2count
#define xnpod_ticks2ns(t)   (t)
#define xnpod_ns2ticks(t)   (t)

// getting internal real time current task

#define xnpod_current_thread  _rt_whoami

// thread suspend, with some retvals discarded (provisional)

static inline void xnpod_suspend_thread(xnthread_t *thread, unsigned long mask, xnticks_t timeout, void *wchan)
{
	if (timeout == XN_INFINITE) {
		rt_task_suspend(thread);
	} else {
		if (rt_task_suspend_timed(thread, timeout) == SEM_TIMOUT) {
			thread->retval = XNTIMEO;
		}
	}
}

// scheduling function: none at the moment, as used in RTDM it should come by nature with RTAI

#define xnpod_schedule()

#define testbits(var, mask)  ((var) & (mask))
#define setbits(var, mask)   atomic_set_mask(mask, &var)
#define xnthread_test_flags(thread, flags)  ((unsigned long)(thread)->retval & (flags))

// synch services

typedef struct xnsynch {
	SEM sem;
	volatile unsigned long status;
} xnsynch_t;

static inline void xnsynch_init(xnsynch_t *sync, xnflags_t flags)
{
	rt_typed_sem_init(&sync->sem, 1, flags);
	sync->status = 0;
}

static inline void xnsynch_sleep_on(xnsynch_t *sync, xnticks_t timeout)
{
	xnthread_t *thread = rt_whoami();	
	if (timeout == XN_INFINITE) {
		if (rt_sem_wait(&sync->sem) == SEM_ERR) {;
			thread->retval = XNRMID;
		}
	} else {
		int retval;
		if ((retval = rt_sem_wait_timed(&sync->sem, timeout)) >= SEM_TIMOUT) {
			if (retval == SEM_TIMOUT) {
				thread->retval = XNTIMEO;
			} else {
				thread->retval = XNRMID;
			}
		}
	}
}

static inline xnthread_t *xnsynch_wakeup_one_sleeper(xnsynch_t *sync)
{
	SEM *sem;
	if ((sem = &sync->sem)->type > 1) {
		sem->type--;
		return 0;
	} else {
		xnthread_t *awaken = (sem->queue.next)->task;
		rt_sem_signal(sem);
		return awaken;
	}
}

static inline int xnsynch_flush(xnsynch_t *sync, xnflags_t reason)
{
	return rt_sem_broadcast(&sync->sem);
}

// in kernel printing (copied from fusion)

#define XNARCH_PROMPT "RTDM: "

#define xnprintf(fmt, args...)       xnarch_printf(fmt, ##args)
#define xnarch_printf(fmt, args...)  printk(KERN_INFO XNARCH_PROMPT fmt, ##args)
#define xnlogerr(fmt, args...)       xnarch_logerr(fmt, ##args)
#define xnlogwarn                    xnarch_logerr
#define xnarch_logerr(fmt, args...)   printk(KERN_ERR XNARCH_PROMPT fmt, ##args)

// user space access, from Linux

#define __xn_access_ok(task, type, addr, size) \
	(likely(__range_ok(addr, size) == 0))

#define __xn_copy_from_user(task, dstP, srcP, n) \
	({ long err = __copy_from_user_inatomic(dstP, srcP, n); err; })

#define __xn_copy_to_user(task, dstP, srcP, n) \
	({ long err = __copy_to_user_inatomic(dstP, srcP, n); err; })

#define __xn_strncpy_from_user(task, dstP, srcP, n) \
	({ long err = __strncpy_from_user(dstP, srcP, n); err; })

// threads services

#define XN_MIN_PRIO   1
#define XN_MAX_PRIO  99

#define XNPOD_ALL_CPUS  0xF

static inline int convprio(int prio)
{
	if ((prio = XN_MAX_PRIO - prio) < XN_MIN_PRIO) {
		prio = XN_MIN_PRIO;
	} else if (prio > XN_MAX_PRIO) {
		prio = XN_MAX_PRIO;
	}
	return prio;
}

extern int rt_kthread_init_cpuid(RT_TASK *task, void (*rt_thread)(long), long data, int stack_size, int priority, int uses_fpu, void(*signal)(void), unsigned int cpuid);

static inline int xnpod_init_thread(xnthread_t *thread, const char *name, int prio, xnflags_t flags, unsigned stacksize)
{
	if (!rt_kthread_init_cpuid(thread, NULL, 0, stacksize, convprio(prio), 1, 0, 0xF)) {
		if (!rt_register(nam2num(name), thread, IS_TASK, 0)) {
                        rt_task_delete(thread);
			return 1;
                }
		thread->state = ~RT_SCHED_READY;
		return 0;
	}
	return 1;
}

static inline int xnpod_start_thread(xnthread_t *thread, xnflags_t mode, int imask, unsigned long affinity, void (*entry)(void *cookie), void *cookie)
{
	thread->max_msg_size[0] = (long)entry;
	thread->max_msg_size[1] = (long)cookie;
	thread->state = RT_SCHED_READY | RT_SCHED_SUSPENDED;
	if (!(mode & XNSUSP)) {
		rt_task_resume(thread);
	}
	return 0;
}

static inline void xnpod_delete_thread(xnthread_t *thread)
{
	if (!thread) {
		thread = _rt_whoami();
	}
	rt_task_delete(thread);
	thread->retval = 0xFFFFFFFF;
}	

#define xnpod_renice_thread(t, p)  (rt_change_prio(t, convprio(p)))

static inline int xnpod_unblock_thread(xnthread_t *thread)
{
	if (thread->magic != RT_TASK_MAGIC && thread->state && thread->state != RT_SCHED_READY) {
		return 0;
	}
	rt_task_masked_unblock(thread, ~RT_SCHED_READY);
	return 1;
}

static inline int xnpod_set_thread_periodic(xnthread_t *thread, xnticks_t idate, xnticks_t period)
{
	return rt_task_make_periodic(thread, idate == XN_INFINITE ? xnarch_get_cpu_tsc() : xnarch_ns_to_tsc(idate), xnarch_ns_to_tsc(period));
}

#define xnpod_wait_thread_period  rt_task_wait_period

// registration of services (as LXRT extensions)

#define RTDM_INDX  5
#define XENOMAI_SYSCALL2(a, b, c)  RTDM_INDX

typedef struct _xnsysent { void *type; unsigned long fun; } xnsysent_t; 

extern int xnshadow_register_interface(const char *name, unsigned magic, int nrcalls, xnsysent_t *systab, int (*eventcb)(int));
extern int xnshadow_unregister_interface(int muxid);

// interrupt setup/management

#define XN_ISR_ENABLE   (1 << 0)
#define XN_ISR_CHAINED  (1 << 1)

struct xnintr_struct {
	unsigned long irq; void *isr; void *iack; unsigned long hits; void *cookie; 
};

typedef struct xnintr_struct xnintr_t;

extern int xnintr_irq_handler(unsigned long irq, xnintr_t *intr);

static inline int xnintr_init(xnintr_t *intr, unsigned long irq, void *isr, void *iack, xnflags_t flags)
{
	*intr = (xnintr_t){ irq, isr, iack, 0, NULL };
	return 0;
}

static inline int xnintr_attach(xnintr_t *intr, void *cookie)
{
	intr->hits   = 0;
	intr->cookie = cookie;
	return rt_request_irq(intr->irq, (void *)xnintr_irq_handler, intr, 0);
}

static inline int xnintr_detach(xnintr_t *intr)
{
	xnprintf("INFO > IRQ: %lu, INTRCNT: %lu\n", intr->irq, intr->hits);
	return rt_release_irq(intr->irq);
}

static inline int xnintr_enable(xnintr_t *intr)
{
	rt_enable_irq(intr->irq);
	return 0;
}

static inline int xnintr_disable(xnintr_t *intr)
{
	rt_disable_irq(intr->irq);
	return 0;
}

static inline int xnintr_destroy(xnintr_t *intr)
{
	return xnintr_detach(intr);
}

#define xnintr_chain  rt_pend_linux_irq

// some definitions used by rtdm (copied from xenomai)

#define __xn_sys_bind  0

#define __xn_exec_any       (0 << 0)
#define __xn_exec_current   (1 << 0)
#define __xn_exec_adaptive  (1 << 1)

#define __xn_sys_mux  555

#define __xn_reg_mux(regs)    ((regs)->orig_eax)
#define __xn_reg_rval(regs)   ((regs)->eax)
#define __xn_reg_arg1(regs)   ((regs)->ebx)
#define __xn_reg_arg2(regs)   ((regs)->ecx)
#define __xn_reg_arg3(regs)   ((regs)->edx)
#define __xn_reg_arg4(regs)   ((regs)->esi)
#define __xn_reg_arg5(regs)   ((regs)->edi)

#else

#include <sys/syscall.h>
#include <unistd.h>

// xenomai syscalls, one for each num args

#define ENCODE_FUSION_REQ(id, op)  (RTAI_SYSCALL_NR | ENCODE_LXRT_REQ(id, op, 2*sizeof(void *)))

static inline int XENOMAI_SKINCALL0(long id, long op)
{
	return syscall(ENCODE_XN_REQ(id, op));
}

static inline int XENOMAI_SKINCALL1(long id, long op, long a1)
{
	return syscall(ENCODE_XN_REQ(id, op), a1);
}

static inline int XENOMAI_SKINCALL2(long id, long op, long a1, long a2)
{
	return syscall(ENCODE_XN_REQ(id, op), a1, a2);
}

static inline int XENOMAI_SKINCALL3(long id, long op, long a1, long a2, long a3)
{
	return syscall(ENCODE_XN_REQ(id, op), a1, a2, a3);
}

static inline int XENOMAI_SKINCALL4(long id, long op, long a1, long a2, long a3, long a4)
	return syscall(ENCODE_XN_REQ(id, op), a1, a2, a3, a4);
}

static inline int XENOMAI_SKINCALL5(long id, long op, long a1, long a2, long a3, long a4, long a5)
	return syscall(ENCODE_XN_REQ(id, op), a1, a2, a3, a4, a5);
}

#endif

// some more xenomai, dummy stuff for RTAI

#define xnpod_init(a, b, c, d)  (0)
#define xnpod_shutdown(t)       do { } while(0)
#define xnfusion_attach()       (0)
#define xnfusion_detach()       do { } while(0)

#endif /* !_RTAI_XN_H */
