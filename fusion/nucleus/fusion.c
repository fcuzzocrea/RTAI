/*
 * Copyright (C) 2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
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

#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <nucleus/shadow.h>
#include <nucleus/synch.h>
#include <nucleus/fusion.h>

static int __fusion_muxid;

static xnpod_t __fusion_pod;

/* This file implements the basic fusion skin which enables the tight
   coupling between Linux POSIX threads and RTAI. This interface also
   provides the internal calls needed to implement the real-time
   virtual machines in user-space. Unchecked uaccess is used in the
   UVM code since the caller (i.e. include/nucleus/asm-uvm/system.h)
   is always trusted. */

static inline xnthread_t *__pthread_find_by_handle (struct task_struct *curr, void *khandle)

{
    xnthread_t *thread;

    if (!khandle)
	return NULL;

    /* FIXME: we should check if khandle is laid in the system
       heap, or at least in kernel space... */

    thread = (xnthread_t *)khandle;

    if (xnthread_get_magic(thread) != FUSION_SKIN_MAGIC)
	return NULL;

    return thread;
}

static int __pthread_shadow_helper (struct task_struct *curr,
				    struct pt_regs *regs,
				    xncompletion_t __user *u_completion)
{
    char name[XNOBJECT_NAME_LEN];
    xnthread_t *thread;
    spl_t s;
    int err;

    if (__xn_reg_arg2(regs) &&
	!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(thread)))
	return -EFAULT;

    thread = (xnthread_t *)xnmalloc(sizeof(*thread));

    if (!thread)
	return -ENOMEM;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(name)))
	    {
	    xnfree(thread);
	    return -EFAULT;
	    }

	__xn_copy_from_user(curr,name,(const char __user *)__xn_reg_arg1(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	strncpy(curr->comm,name,sizeof(curr->comm));
	curr->comm[sizeof(curr->comm) - 1] = '\0';
	}
    else
	{
	strncpy(name,curr->comm,sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}

    if (xnpod_init_thread(thread,
			  name,
			  curr->policy == SCHED_FIFO ? curr->rt_priority : FUSION_LOW_PRIO,
			  XNFPU|XNSHADOW|XNSHIELD,
			  0) != 0)
	{
	/* Assume this is the only possible failure. */
	xnfree(thread);
	return -ENOMEM;
	}

    xnthread_set_magic(thread,FUSION_SKIN_MAGIC);
    
    /* We don't want some funny guy to rip the new TCB off while two
       user-space threads are being synchronized on it, so enter a
       critical section. Do *not* take the big lock here: this is
       useless since deleting a thread through an inter-CPU request
       requires the target CPU to accept IPIs. */

    splhigh(s);

    if (__xn_reg_arg2(regs))
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&thread,sizeof(thread));

    xnthread_extended_info(thread) = (void *)__xn_reg_arg3(regs);

    err = xnshadow_map(thread,u_completion);

    splexit(s);

    return err;
}

static int __fusion_thread_shadow (struct task_struct *curr, struct pt_regs *regs)
{
    return __pthread_shadow_helper(curr,regs,NULL);
}

static int __fusion_thread_release (struct task_struct *curr, struct pt_regs *regs)

{
    xnthread_t *thread = xnshadow_thread(curr);	/* Can't be NULL. */

    /* We are currently running in secondary mode. */
    xnpod_delete_thread(thread);

    return 0;
}

static int __fusion_thread_create (struct task_struct *curr, struct pt_regs *regs)

{
    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg4(regs),sizeof(xncompletion_t)))
	return -EFAULT;

    return __pthread_shadow_helper(curr,regs,(xncompletion_t __user *)__xn_reg_arg4(regs));
}

static int __fusion_thread_start (struct task_struct *curr, struct pt_regs *regs)

{
    xnthread_t *thread;
    int err;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    thread = __pthread_find_by_handle(curr,(void *)__xn_reg_arg1(regs));

    if (!thread)
	{
	err = -ESRCH;
	goto out;
	}

    err = xnpod_start_thread(thread,0,0,XNPOD_ALL_CPUS,NULL,NULL);

 out:

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

static int __fusion_timer_read (struct task_struct *curr, struct pt_regs *regs)

{
    nanotime_t t;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(t)))
	return -EFAULT;

    t = xnpod_get_time();
    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&t,sizeof(t));

    return 0;
}

static int __fusion_timer_tsc (struct task_struct *curr, struct pt_regs *regs)

{
    nanotime_t t;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(t)))
	return -EFAULT;

    t = xnarch_get_cpu_tsc();
    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&t,sizeof(t));

    return 0;
}

static int __fusion_timer_start (struct task_struct *curr, struct pt_regs *regs)

{
    nanotime_t nstick;

    __xn_copy_from_user(curr,&nstick,(void __user *)__xn_reg_arg1(regs),sizeof(nstick));

    if (testbits(nkpod->status,XNTIMED))
	{
	if ((nstick == FUSION_APERIODIC_TIMER && xnpod_get_tickval() == 1) ||
	    (nstick != FUSION_APERIODIC_TIMER && xnpod_get_tickval() == nstick))
	    return 0;

	xnpod_stop_timer();
	}

    return xnpod_start_timer(nstick,XNPOD_DEFAULT_TICKHANDLER);
}

static int __fusion_timer_stop (struct task_struct *curr, struct pt_regs *regs)

{
    xnpod_stop_timer();
    return 0;
}

static int __fusion_thread_sleep (struct task_struct *curr, struct pt_regs *regs)

{
    nanotime_t delay;
    int err = 0;

    if (!testbits(nkpod->status,XNTIMED))
	return -EWOULDBLOCK;

    __xn_copy_from_user(curr,&delay,(void __user *)__xn_reg_arg1(regs),sizeof(delay));

    xnpod_delay(delay);

    if (xnthread_test_flags(xnpod_current_thread(),XNBREAK))
	err = -EINTR; /* Unblocked.*/

    return err;
}

static int __fusion_thread_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    xnthread_t *thread = xnshadow_thread(curr);	/* Can't be NULL. */
    xninquiry_t buf;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(buf)))
	return -EFAULT;

    strncpy(buf.name,xnthread_name(thread),sizeof(buf.name) - 1);
    buf.name[sizeof(buf.name) - 1] = 0;
    buf.prio = xnthread_current_priority(thread);
    buf.status = xnthread_status_flags(thread);
    buf.khandle = thread;
    buf.uhandle = xnthread_extended_info(thread);

    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&buf,sizeof(buf));

    return 0;
}

static int __fusion_thread_set_periodic (struct task_struct *curr, struct pt_regs *regs)

{
    xnthread_t *thread = xnshadow_thread(curr);	/* Can't be NULL. */
    nanotime_t idate, period;

    __xn_copy_from_user(curr,&idate,(void __user *)__xn_reg_arg1(regs),sizeof(idate));
    __xn_copy_from_user(curr,&period,(void __user *)__xn_reg_arg2(regs),sizeof(period));

    return xnpod_set_thread_periodic(thread,idate,period);
}

static int __fusion_thread_wait_period (struct task_struct *curr, struct pt_regs *regs)

{
    return xnpod_wait_thread_period();
}

/*
 * UVM support -- hidden interface.
 */

static xnsynch_t __fusion_uvm_irqsync;

static int fusion_uvm_hold (struct task_struct *curr, struct pt_regs *regs)

{
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    __xn_put_user(curr,1,(int __user *)__xn_reg_arg1(regs)); /* Raise the irqlock flag */

    xnsynch_sleep_on(&__fusion_uvm_irqsync,XN_INFINITE);

    if (xnthread_test_flags(xnpod_current_thread(),XNBREAK))
	err = -EINTR; /* Unblocked.*/
    else if (xnthread_test_flags(xnpod_current_thread(),XNRMID))
	err = -EIDRM; /* Sync deleted.*/

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

static int fusion_uvm_release (struct task_struct *curr, struct pt_regs *regs)

{
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    __xn_put_user(curr,0,(int __user *)__xn_reg_arg1(regs)); /* Clear the irqlock flag */

    if (xnsynch_flush(&__fusion_uvm_irqsync,XNBREAK) == XNSYNCH_RESCHED)
	xnpod_schedule();

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

static int fusion_uvm_idle (struct task_struct *curr, struct pt_regs *regs)

{
    xnthread_t *thread = xnpod_current_thread();
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    /* Emulate sti() for the UVM before returning to idle mode. */

    __xn_put_user(curr,0,(int __user *)__xn_reg_arg1(regs)); /* Clear the irqlock flag */

    if (xnsynch_nsleepers(&__fusion_uvm_irqsync) > 0)
	xnsynch_flush(&__fusion_uvm_irqsync,XNBREAK);

    xnpod_suspend_thread(thread,XNSUSP,XN_INFINITE,NULL);

    if (xnthread_test_flags(thread,XNBREAK))
	err = -EINTR; /* Unblocked.*/

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

static int fusion_uvm_activate (struct task_struct *curr, struct pt_regs *regs)

{
    xnthread_t *prev, *next;
    int err = -ESRCH;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    next = __pthread_find_by_handle(curr,(void *)__xn_reg_arg1(regs));

    if (!next)
	goto out;

    prev = __pthread_find_by_handle(curr,(void *)__xn_reg_arg2(regs));

    if (!prev)
	goto out;

    if (!testbits(next->status,XNSTARTED))
	{
	err = xnpod_start_thread(next,0,0,XNPOD_ALL_CPUS,NULL,NULL);
	goto out;
	}

    xnpod_resume_thread(next,XNSUSP);

    xnpod_suspend_thread(prev,XNSUSP,XN_INFINITE,NULL);

    err = 0;

    if (prev == xnpod_current_thread())
	{
	if (xnthread_test_flags(prev,XNBREAK))
	    err = -EINTR; /* Unblocked.*/
	}
    else
	xnpod_schedule();

 out:

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

static int fusion_uvm_cancel (struct task_struct *curr, struct pt_regs *regs)

{
    xnthread_t *dead, *next;
    int err = -ESRCH;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    if (__xn_reg_arg1(regs))
	{
	dead = __pthread_find_by_handle(curr,(void *)__xn_reg_arg1(regs));

	if (!dead)
	    goto out;
	}
    else
	dead = xnshadow_thread(curr);

    if (__xn_reg_arg2(regs))
	{
	next = __pthread_find_by_handle(curr,(void *)__xn_reg_arg2(regs));

	if (!next)
	    goto out;

	xnpod_resume_thread(next,XNSUSP);
	}

    err = 0;

    xnpod_delete_thread(dead);

out:

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

static void xnfusion_shadow_delete_hook (xnthread_t *thread)

{
    if (xnthread_get_magic(thread) == FUSION_SKIN_MAGIC)
	{
	xnshadow_unmap(thread);
	xnfree(thread);
	}
}

static xnsysent_t __systab[] = {
    /* Exported calls. */
    [__xn_fusion_init] = { &__fusion_thread_shadow, __xn_exec_init },
    [__xn_fusion_exit] = { &__fusion_thread_release, __xn_exec_secondary },
    [__xn_fusion_create] = { &__fusion_thread_create, __xn_exec_init },
    [__xn_fusion_start] = { &__fusion_thread_start, __xn_exec_any },
    [__xn_fusion_time] = { &__fusion_timer_read, __xn_exec_any  },
    [__xn_fusion_cputime] = { &__fusion_timer_tsc, __xn_exec_any  },
    [__xn_fusion_start_timer] = { &__fusion_timer_start, __xn_exec_lostage  },
    [__xn_fusion_stop_timer] = { &__fusion_timer_stop, __xn_exec_lostage  },
    [__xn_fusion_sleep] = { &__fusion_thread_sleep, __xn_exec_primary  },
    [__xn_fusion_inquire] = { &__fusion_thread_inquire, __xn_exec_shadow },
    [__xn_fusion_set_periodic] = { &__fusion_thread_set_periodic, __xn_exec_primary },
    [__xn_fusion_wait_period] = { &__fusion_thread_wait_period, __xn_exec_primary },
    /* Internal UVM calls. */
    [__xn_fusion_idle] = { &fusion_uvm_idle, __xn_exec_primary },
    [__xn_fusion_cancel] = { &fusion_uvm_cancel, __xn_exec_primary },
    [__xn_fusion_activate] = { &fusion_uvm_activate, __xn_exec_primary },
    [__xn_fusion_hold] = { &fusion_uvm_hold, __xn_exec_primary },
    [__xn_fusion_release] = { &fusion_uvm_release, __xn_exec_any },
};

static int xnfusion_unload_hook (void)

{
    int rc = 0;

    /* If nobody is attached to the fusion skin, then clean it up
       now. */

    if (xnarch_atomic_get(&muxtable[__fusion_muxid - 1].refcnt) == -1)
	{
	xnfusion_umount();
	rc = 1;
	}

    return rc;
}

int xnfusion_attach (void)

{
    if (nkpod)
	{
	if (nkpod != &__fusion_pod)
	    return -ENOSYS;

	return 0;
	}

    if (xnpod_init(&__fusion_pod,FUSION_MIN_PRIO,FUSION_MAX_PRIO,0) != 0)
	return -ENOSYS;

    __fusion_pod.svctable.unload = &xnfusion_unload_hook;
    xnpod_add_hook(XNHOOK_THREAD_DELETE,&xnfusion_shadow_delete_hook);
    xnsynch_init(&__fusion_uvm_irqsync,XNSYNCH_FIFO);

    return 0;
}

static int xnfusion_event_cb (int event)

{
    switch (event)
	{
	case XNSHADOW_CLIENT_ATTACH:

	    return xnfusion_attach();

	case XNSHADOW_CLIENT_DETACH:

	    /* Nothing to do upon detach. Let the interface live, and
	       rely on the unload hook for removing it as needed. */
	    break;
	}

    return 0;
}

int xnfusion_mount (void)

{
    int ifid;

    ifid = xnshadow_register_interface("fusion",
				       FUSION_SKIN_MAGIC,
				       sizeof(__systab) / sizeof(__systab[0]),
				       __systab,
				       &xnfusion_event_cb);
    if (ifid < 0)
	return -ENOSYS;

    __fusion_muxid = ifid;

    return 0;
}

int xnfusion_umount (void)

{
    if (nkpod != &__fusion_pod)
	return -ENOSYS;

    xnpod_stop_timer();

    if (xnsynch_destroy(&__fusion_uvm_irqsync) == XNSYNCH_RESCHED)
	xnpod_schedule();

    xnpod_remove_hook(XNHOOK_THREAD_DELETE,&xnfusion_shadow_delete_hook);

    xnpod_shutdown(XNPOD_NORMAL_EXIT);

    xnshadow_unregister_interface(__fusion_muxid);

    return 0;
}

EXPORT_SYMBOL(xnfusion_attach);
