/**
 * @file
 * Real-Time Driver Model for RTAI
 *
 * @note Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>
 * @note Copyright (C) 2005 Joerg Langenberg <joerg.langenberg@gmx.net>
 * @note Copyright (C) 2005 Paolo Mantegazza <mantegazza@aero.polimi.it>
 *       for the adaption to RTAI only.
 *
 * RTAI is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * RTAI is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RTAI; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*!
 * @defgroup rtdm Real-Time Driver Model
 *
 * The Real-Time Driver Model (RTDM) provides a unified interface to
 * both users and developers of real-time device
 * drivers. Specifically, it addresses the constraints of mixed
 * RT/non-RT systems like RTAI. RTDM conforms to POSIX
 * semantics (IEEE Std 1003.1) where available and applicable.
 */

/*!
 * @ingroup rtdm
 * @defgroup profiles Device Profiles
 *
 * Device profiles define which operation handlers a driver of a certain class
 * has to implement, which name or protocol it has to register, which IOCTLs
 * it has to provide, and further details. Sub-classes can be defined in order
 * to extend a device profile with more hardware-specific functions.
 */

#include <linux/module.h>

#include <rtdm/rtdm.h>
#include <rtdm/core.h>
#include <rtdm/device.h>
#include <rtdm/proc.h>
#include <rtdm/rtdm_driver.h>

MODULE_DESCRIPTION("Real-Time Driver Model");
MODULE_AUTHOR("jan.kiszka@web.de");
MODULE_LICENSE("GPL");

static RTAI_SYSCALL_MODE int _rtdm_fdcount(void)
{
	return RTDM_FD_MAX;
}

#ifdef TRUE_LXRT_WAY

static struct rt_fun_entry rtdm[] = {
	[__rtdm_fdcount] = { 0, _rtdm_fdcount },
	[__rtdm_open]    = { 0, _rtdm_open },
	[__rtdm_socket]  = { 0, _rtdm_socket },
	[__rtdm_close]   = { 0, _rtdm_close },
	[__rtdm_ioctl]   = { 0, _rtdm_ioctl },
	[__rtdm_read]    = { 0, _rtdm_read },
	[__rtdm_write]   = { 0, _rtdm_write },
	[__rtdm_recvmsg] = { 0, _rtdm_recvmsg },
	[__rtdm_sendmsg] = { 0, _rtdm_sendmsg }
};

#else /* !TRUE_LXRT_WAY */

static RTAI_SYSCALL_MODE int sys_rtdm_open(const char *path, long oflag)
{
	char krnl_path[RTDM_MAX_DEVNAME_LEN + 1];
	struct task_struct *curr = current;

	if (unlikely(!__xn_access_ok(curr, VERIFY_READ, path, sizeof(krnl_path)))) {
	        return -EFAULT;
	}
	__xn_copy_from_user(curr, krnl_path, path, sizeof(krnl_path)-1);
	krnl_path[sizeof(krnl_path) - 1] = '\0';
	return _rtdm_open(curr, (const char *)krnl_path, oflag);
}

static RTAI_SYSCALL_MODE int sys_rtdm_socket(long protocol_family, long socket_type, long protocol)
{
	return _rtdm_socket(current, protocol_family, socket_type, protocol);
}

static RTAI_SYSCALL_MODE int sys_rtdm_close(long fd, long forced)
{
	return _rtdm_close(current, fd, forced);
}

static RTAI_SYSCALL_MODE int sys_rtdm_ioctl(long fd, long request, void *arg)
{
	return _rtdm_ioctl(current, fd, request, arg);
}

static RTAI_SYSCALL_MODE int sys_rtdm_read(long fd, void *buf, long nbytes)
{
	return _rtdm_read(current, fd, buf, nbytes);
}

static RTAI_SYSCALL_MODE int sys_rtdm_write(long fd, void *buf, long nbytes)
{
	return _rtdm_write(current, fd, buf, nbytes);
}

static RTAI_SYSCALL_MODE int sys_rtdm_recvmsg(long fd, struct msghdr *msg, long flags)
{
	struct msghdr krnl_msg;
	struct task_struct *curr = current;
	int ret;

	if (unlikely(!__xn_access_ok(curr, VERIFY_WRITE, msg, sizeof(krnl_msg)))) {
		return -EFAULT;
	}
	__xn_copy_from_user(curr, &krnl_msg, msg, sizeof(krnl_msg));
	if ((ret = _rtdm_recvmsg(curr, fd, &krnl_msg, flags)) >= 0) {
		__xn_copy_to_user(curr, msg, &krnl_msg, sizeof(krnl_msg));
	}
	return ret;
}

static RTAI_SYSCALL_MODE int sys_rtdm_sendmsg(long fd, const struct msghdr *msg, long flags)
{
	struct msghdr krnl_msg;
	struct task_struct *curr = current;

	if (unlikely(!__xn_access_ok(curr, VERIFY_READ, msg, sizeof(krnl_msg)))) {
		return -EFAULT;
	}
	__xn_copy_from_user(curr, &krnl_msg, msg, sizeof(krnl_msg));
	return _rtdm_sendmsg(curr, fd, &krnl_msg, flags);
}

static struct rt_fun_entry rtdm[] = {
	[__rtdm_fdcount] = { 0, _rtdm_fdcount },
	[__rtdm_open]    = { 0, sys_rtdm_open },
	[__rtdm_socket]  = { 0, sys_rtdm_socket },
	[__rtdm_close]   = { 0, sys_rtdm_close },
	[__rtdm_ioctl]   = { 0, sys_rtdm_ioctl },
	[__rtdm_read]    = { 0, sys_rtdm_read },
	[__rtdm_write]   = { 0, sys_rtdm_write },
	[__rtdm_recvmsg] = { 0, sys_rtdm_recvmsg },
	[__rtdm_sendmsg] = { 0, sys_rtdm_sendmsg },
};

#endif /* TRUE_LXRT_WAY */

xnlock_t nklock = XNARCH_LOCK_UNLOCKED;

/* This is needed because RTDM interrupt handlers:
 * - do no want immediate in handler rescheduling, RTAI can be configured
 *   to act in the same way but might not have been enabled to do so; 
 * - may not reenable the PIC directly, assuming it will be done here;
 * - may not propagate, assuming it will be done here as well.
 * - might use shared interrupts its own way;
 * REMARK: RTDM irqs management is as generic as its pet system dictates 
 *         and there is no choice but doing the same as closely as possible; 
 *         so this is an as verbatim as possible copy of what is needed from 
 *         the RTDM pet system.
 * REMINDER: the RTAI dispatcher cares mask/ack-ing anyhow, but RTDM will
 *           (must) provide the most suitable one for the shared case. */

#ifndef CONFIG_RTAI_SCHED_ISR_LOCK
extern struct { volatile int locked, rqsted; } rt_scheduling[];
extern void rtai_handle_isched_lock(int);

#define RTAI_SCHED_ISR_LOCK() \
        do { \
		int cpuid = rtai_cpuid(); \
                if (!rt_scheduling[cpuid].locked++) { \
                        rt_scheduling[cpuid].rqsted = 0; \
                }
#define RTAI_SCHED_ISR_UNLOCK() \
                rtai_cli(); \
                if (rt_scheduling[cpuid].locked && !(--rt_scheduling[cpuid].locked)) { \
                        if (rt_scheduling[cpuid].rqsted > 0) { \
                                rtai_handle_isched_lock(cpuid); \
                        } \
                } \
        } while (0)
#else /* !CONFIG_RTAI_SCHED_ISR_LOCK */
#define RTAI_SCHED_ISR_LOCK() \
        do {             } while (0)
#define RTAI_SCHED_ISR_UNLOCK() \
        do { rtai_cli(); } while (0)
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */

#define XNINTR_MAX_UNHANDLED  1000

#ifdef CONFIG_SMP
xnlock_t intrlock;
#endif /* CONFIG_SMP */

static void xnintr_irq_handler(unsigned irq, void *cookie)
{

	xnintr_t *intr = (xnintr_t *)cookie;


	int s;

	xnarch_memory_barrier();





	RTAI_SCHED_ISR_LOCK();
	s = intr->isr(intr);

	if (unlikely(s == XN_ISR_NONE)) {
		if (++intr->unhandled == XNINTR_MAX_UNHANDLED) {
			xnlogerr("%s: IRQ%d not handled. Disabling IRQ "
				 "line.\n", __FUNCTION__, irq);
			s |= XN_ISR_NOENABLE;
		}
	} else {
		xnstat_counter_inc(&intr->stat[xnsched_cpu(sched)].hits);



		intr->unhandled = 0;
	}

	if (s & XN_ISR_PROPAGATE)
		xnarch_chain_irq(irq);
	else if (!(s & XN_ISR_NOENABLE))
		xnarch_end_irq(irq);

	RTAI_SCHED_ISR_UNLOCK();













}











#if defined(CONFIG_RTAI_RTDM_LEVEL_SHIRQ) ||defined(CONFIG_RTAI_RTDM_EDGE_SHIRQ)

typedef struct xnintr_shirq {

	xnintr_t *handlers;
	int unhandled;
#ifdef CONFIG_SMP
	atomic_counter_t active;
#endif

} xnintr_shirq_t;

static xnintr_shirq_t xnshirqs[RTHAL_NR_IRQS];

static inline void xnintr_shirq_lock(xnintr_shirq_t *shirq)
{
#ifdef CONFIG_SMP
	xnarch_before_atomic_inc();
	xnarch_atomic_inc(&shirq->active);
#endif
}

static inline void xnintr_shirq_unlock(xnintr_shirq_t *shirq)
{
#ifdef CONFIG_SMP
	xnarch_atomic_dec(&shirq->active);
	xnarch_after_atomic_dec();
#endif
}

void xnintr_synchronize(xnintr_t *intr)
{
#ifdef CONFIG_SMP
	xnintr_shirq_t *shirq = &xnshirqs[intr->irq];

	while (xnarch_atomic_get(&shirq->active))
		cpu_relax();
	xnarch_memory_barrier();
#endif
}

#if defined(CONFIG_RTAI_RTDM_LEVEL_SHIRQ)





static void xnintr_shirq_handler(unsigned irq, void *cookie)
{



	xnintr_shirq_t *shirq = &xnshirqs[irq];
	xnintr_t *intr;
	int s = 0;

	xnarch_memory_barrier();





	RTAI_SCHED_ISR_LOCK();

	xnintr_shirq_lock(shirq);
	intr = shirq->handlers;

	while (intr) {
		int ret;

		ret = intr->isr(intr);
		s |= ret;

                if (ret & XN_ISR_HANDLED) {
                        xnstat_counter_inc(
                                &intr->stat[xnsched_cpu(sched)].hits);




                }

                intr = intr->next;
	}

	xnintr_shirq_unlock(shirq);

	if (unlikely(s == XN_ISR_NONE)) {
		if (++shirq->unhandled == XNINTR_MAX_UNHANDLED) {
			xnlogerr("%s: IRQ%d not handled. Disabling IRQ "
				 "line.\n", __FUNCTION__, irq);
			s |= XN_ISR_NOENABLE;
		}
	} else
                shirq->unhandled = 0;

	if (s & XN_ISR_PROPAGATE)
		xnarch_chain_irq(irq);
	else if (!(s & XN_ISR_NOENABLE))
		xnarch_end_irq(irq);

	RTAI_SCHED_ISR_UNLOCK();




}

#endif /* CONFIG_RTAI_RTDM_LEVEL_SHIRQ */

#if defined(CONFIG_RTAI_RTDM_EDGE_SHIRQ)





static void xnintr_edge_shirq_handler(unsigned irq, void *cookie)
{
	const int MAX_EDGEIRQ_COUNTER = 128;




	xnintr_shirq_t *shirq = &xnshirqs[irq];
	xnintr_t *intr, *end = NULL;
	int s = 0, counter = 0;

	xnarch_memory_barrier();





	RTAI_SCHED_ISR_LOCK();

	xnintr_shirq_lock(shirq);
	intr = shirq->handlers;

	while (intr != end) {
		int ret, code;




		ret = intr->isr(intr);
		code = ret & ~XN_ISR_BITMASK;
		s |= ret;

		if (code == XN_ISR_HANDLED) {
			end = NULL;
                        xnstat_counter_inc(
                                &intr->stat[xnsched_cpu(sched)].hits);




		} else if (code == XN_ISR_NONE && end == NULL)
			end = intr;

		if (counter++ > MAX_EDGEIRQ_COUNTER)
			break;

		if (!(intr = intr->next))
			intr = shirq->handlers;
	}

	xnintr_shirq_unlock(shirq);

	if (counter > MAX_EDGEIRQ_COUNTER)
		xnlogerr
		    ("xnintr_edge_shirq_handler() : failed to get the IRQ%d line free.\n",
		     irq);

	if (unlikely(s == XN_ISR_NONE)) {
		if (++shirq->unhandled == XNINTR_MAX_UNHANDLED) {
			xnlogerr("%s: IRQ%d not handled. Disabling IRQ "
				 "line.\n", __FUNCTION__, irq);
			s |= XN_ISR_NOENABLE;
		}
	} else
		shirq->unhandled = 0;

	if (s & XN_ISR_PROPAGATE)
		xnarch_chain_irq(irq);
	else if (!(s & XN_ISR_NOENABLE))
		xnarch_end_irq(irq);

	RTAI_SCHED_ISR_UNLOCK();




}

#endif /* CONFIG_RTAI_RTDM_EDGE_SHIRQ */

static inline int xnintr_irq_attach(xnintr_t *intr)
{
	xnintr_shirq_t *shirq = &xnshirqs[intr->irq];
	xnintr_t *prev, **p = &shirq->handlers;
	int err;

	if (intr->irq >= RTHAL_NR_IRQS)
		return -EINVAL;

	if (__testbits(intr->flags, XN_ISR_ATTACHED))
		return -EPERM;

	if ((prev = *p) != NULL) {
		/* Check on whether the shared mode is allowed. */
		if (!(prev->flags & intr->flags & XN_ISR_SHARED) ||
		    (prev->iack != intr->iack)
		    || ((prev->flags & XN_ISR_EDGE) !=
			(intr->flags & XN_ISR_EDGE)))
			return -EBUSY;



		while (prev) {
			p = &prev->next;
			prev = *p;
		}
	} else {
		/* Initialize the corresponding interrupt channel */
		void (*handler) (unsigned, void *) = &xnintr_irq_handler;

		if (intr->flags & XN_ISR_SHARED) {
			if (intr->flags & XN_ISR_EDGE)
#if defined(CONFIG_RTAI_RTDM_EDGE_SHIRQ)
				handler = &xnintr_edge_shirq_handler;
#else /* !CONFIG_RTAI_RTDM_EDGE_SHIRQ */
				return -ENOSYS;
#endif /* CONFIG_RTAI_RTDM_EDGE_SHIRQ */
			else
#if defined(CONFIG_RTAI_RTDM_LEVEL_SHIRQ)
				handler = &xnintr_shirq_handler;
#else /* !CONFIG_RTAI_RTDM_LEVEL_SHIRQ */
				return -ENOSYS;
#endif /* CONFIG_RTAI_RTDM_LEVEL_SHIRQ */

		}
		shirq->unhandled = 0;

		err = xnarch_hook_irq(intr->irq, handler, intr->iack, intr);
		if (err)
			return err;
	}

	__setbits(intr->flags, XN_ISR_ATTACHED);

	/* Add a given interrupt object. */
	intr->next = NULL;
	*p = intr;

	return 0;
}

static inline int xnintr_irq_detach(xnintr_t *intr)
{
	xnintr_shirq_t *shirq = &xnshirqs[intr->irq];
	xnintr_t *e, **p = &shirq->handlers;
	int err = 0;

	if (intr->irq >= RTHAL_NR_IRQS)
		return -EINVAL;

	if (!__testbits(intr->flags, XN_ISR_ATTACHED))
		return -EPERM;

	__clrbits(intr->flags, XN_ISR_ATTACHED);

	while ((e = *p) != NULL) {
		if (e == intr) {
			/* Remove a given interrupt object from the list. */
			*p = e->next;

			/* Release the IRQ line if this was the last user */
			if (shirq->handlers == NULL)
				err = xnarch_release_irq(intr->irq);

			return err;
		}
		p = &e->next;
	}

	xnlogerr("attempted to detach a non previously attached interrupt "
		 "object.\n");
	return err;
}

int xnintr_mount(void)
{
	int i;
	for (i = 0; i < RTHAL_NR_IRQS; ++i) {
		xnshirqs[i].handlers = NULL;
#ifdef CONFIG_SMP
		atomic_set(&xnshirqs[i].active, 0);
#endif
	}
	return 0;
}

#else /* !CONFIG_RTAI_RTDM_LEVEL_SHIRQ && !CONFIG_RTAI_RTDM_EDGE_SHIRQ */

static inline int xnintr_irq_attach(xnintr_t *intr)
{
	return xnarch_hook_irq(intr->irq, &xnintr_irq_handler, intr->iack, intr);
}

static inline int xnintr_irq_detach(xnintr_t *intr)
{
	return xnarch_release_irq(intr->irq);
}

void xnintr_synchronize(xnintr_t *intr) {}
int xnintr_mount(void) { return 0; }

#endif /* CONFIG_RTAI_RTDM_LEVEL_SHIRQ || CONFIG_RTAI_RTDM_EDGE_SHIRQ */

int xnintr_init(xnintr_t *intr,
		const char *name,
		unsigned irq, xnisr_t isr, xniack_t iack, xnflags_t flags)
{
	intr->irq = irq;
	intr->isr = isr;
	intr->iack = iack;
	intr->cookie = NULL;
	intr->name = name ? : "<unknown>";
	intr->flags = flags;
	intr->unhandled = 0;
	memset(&intr->stat, 0, sizeof(intr->stat));
#if defined(CONFIG_RTAI_RTDM_LEVEL_SHIRQ) ||defined(CONFIG_RTAI_RTDM_EDGE_SHIRQ)
	intr->next = NULL;
#endif /* CONFIG_RTAI_RTDM_LEVEL_SHIRQ) || CONFIG_RTAI_RTDM_EDGE_SHIRQ */

	return 0;
}

int xnintr_destroy(xnintr_t *intr)
{
	xnintr_detach(intr);
	return 0;
}

int xnintr_attach(xnintr_t *intr, void *cookie)
{
	int err;
	spl_t s;

	intr->cookie = cookie;

	xnlock_get_irqsave(&intrlock, s);

	err = xnintr_irq_attach(intr);

	xnlock_put_irqrestore(&intrlock, s);









	return err;
}

int xnintr_detach(xnintr_t *intr)
{
	int err;
	spl_t s;

	xnlock_get_irqsave(&intrlock, s);

	err = xnintr_irq_detach(intr);

	xnlock_put_irqrestore(&intrlock, s);













	xnintr_synchronize(intr);

	return err;
}

int xnintr_enable (xnintr_t *intr)
{
        rt_enable_irq(intr->irq);
        return 0;
}

int xnintr_disable (xnintr_t *intr)
{
        rt_disable_irq(intr->irq);
        return 0;
}

int __init rtdm_skin_init(void)
{
	int err;

        if(set_rt_fun_ext_index(rtdm, RTDM_INDX)) {
                printk("LXRT extension %d already in use. Recompile RTDM with a different extension index\n", RTDM_INDX);
                return -EACCES;
        }
	if ((err = rtdm_dev_init())) {
	        goto fail;
	}

	xnintr_mount();

#ifdef CONFIG_PROC_FS
	if ((err = rtdm_proc_init())) {
	        goto cleanup_core;
	}
#endif /* CONFIG_PROC_FS */

	printk("RTDM started.\n");
	return 0;

#ifdef CONFIG_PROC_FS
	rtdm_proc_cleanup();
#endif /* CONFIG_PROC_FS */
cleanup_core:
	rtdm_dev_cleanup();
fail:
	return err;
}

void __exit rtdm_skin_exit(void)
{
	rtdm_dev_cleanup();
        reset_rt_fun_ext_index(rtdm, RTDM_INDX);
#ifdef CONFIG_PROC_FS
	rtdm_proc_cleanup();
#endif /* CONFIG_PROC_FS */
	printk("RTDM stopped.\n");
}

module_init(rtdm_skin_init);
module_exit(rtdm_skin_exit);

EXPORT_SYMBOL(xnintr_init);
EXPORT_SYMBOL(xnintr_destroy);
EXPORT_SYMBOL(xnintr_attach);
EXPORT_SYMBOL(xnintr_detach);
EXPORT_SYMBOL(xnintr_disable);
EXPORT_SYMBOL(xnintr_enable);
