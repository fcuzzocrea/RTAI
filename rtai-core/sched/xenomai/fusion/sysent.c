/*
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
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
 */

#include <xenomai/pod.h>
#include <xenomai/heap.h>
#include <xenomai/shadow.h>
#include <xenomai/mutex.h>
#include <xenomai/fusion.h>

static int __fusion_muxid;

static xnsynch_t __fusion_barrier;

extern xnmutex_t __imutex;

static inline xnthread_t *__pthread_find_by_handle (struct task_struct *curr, void *khandle)

{
    xnthread_t *thread;

    if (!khandle)
	return NULL;

    /* FIXME: we should check if khandle is laid in the system
       heap, or at least in kernel space... */

    thread = (xnthread_t *)khandle;

    if (xnthread_magic(thread) != FUSION_SKIN_MAGIC)
	/* FIXME: We should kill all VM threads at once when a signal
	   is caught for one of them. */
	return NULL;

    return thread;
}

static int __pthread_shadow_helper (struct task_struct *curr,
				    struct pt_regs *regs,
				    pid_t syncpid,
				    int *u_syncp)
{
    char name[XNOBJECT_NAME_LEN];
    xnthread_t *thread;

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

	__xn_copy_from_user(curr,name,(const char *)__xn_reg_arg1(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	strncpy(curr->comm,name,sizeof(curr->comm));
	curr->comm[sizeof(curr->comm) - 1] = '\0';
	}
    else
	{
	strncpy(name,curr->comm,sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}

    if (__xn_reg_arg2(regs))
	__xn_copy_to_user(curr,(void *)__xn_reg_arg2(regs),&thread,sizeof(thread));

    xnthread_extended_info(thread) = (void *)__xn_reg_arg3(regs);

    xnshadow_map(thread,
		 name,
		 curr->policy == SCHED_FIFO ? curr->rt_priority : FUSION_LOW_PRI,
		 syncpid,
		 u_syncp,
		 FUSION_SKIN_MAGIC,
		 NULL);
    return 0;
}

static int __pthread_init_rt (struct task_struct *curr, struct pt_regs *regs) {

    return __pthread_shadow_helper(curr,regs,0,NULL);
}

static int __pthread_create_rt (struct task_struct *curr, struct pt_regs *regs)

{
    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg5(regs),sizeof(int)))
	return -EFAULT;

    return __pthread_shadow_helper(curr,regs,__xn_reg_arg4(regs),(int *)__xn_reg_arg5(regs));
}

static int __pthread_start_rt (struct task_struct *curr, struct pt_regs *regs)

{
    xnthread_t *thread;
    int err = -ESRCH;

    xnmutex_lock(&__imutex);

    thread = __pthread_find_by_handle(curr,(void *)__xn_reg_arg1(regs));

    if (!thread)
	goto out;

    if (!testbits(thread->status,XNSTARTED))
	{
	xnshadow_start(thread,0,NULL,NULL,0);
	xnpod_schedule(&__imutex);
	err = 0;
	}
    else
	err = -EBUSY;

 out:

    xnmutex_unlock(&__imutex);

    return err;
}

static int __pthread_time_rt (struct task_struct *curr, struct pt_regs *regs)

{
    unsigned long long t;

    if (!__xn_access_ok(curr,VERIFY_WRITE,(void *)__xn_reg_arg1(regs),sizeof(t)))
	return -EFAULT;

    t = xnarch_get_cpu_time();
    __xn_copy_to_user(curr,(void *)__xn_reg_arg1(regs),&t,sizeof(t));

    return 0;
}

static int __pthread_inquire_rt (struct task_struct *curr, struct pt_regs *regs)

{
    xnthread_t *thread = xnshadow_thread(curr);	/* Can't be NULL. */
    xninquiry_t buf;

    if (!__xn_access_ok(curr,VERIFY_WRITE,(void *)__xn_reg_arg1(regs),sizeof(buf)))
	return -EFAULT;

    strncpy(buf.name,xnthread_name(thread),sizeof(buf.name) - 1);
    buf.name[sizeof(buf.name) - 1] = 0;
    buf.prio = xnthread_current_priority(thread);
    buf.status = xnthread_status_flags(thread);
    buf.khandle = thread;
    buf.uhandle = xnthread_extended_info(thread);

    __xn_copy_to_user(curr,(void *)__xn_reg_arg1(regs),&buf,sizeof(buf));

    return 0;
}

static int __pthread_migrate_rt (struct task_struct *curr, struct pt_regs *regs)

{
    xnthread_t *thread = xnshadow_thread(curr);	/* Can't be NULL. */

    switch (__xn_reg_arg1(regs))
	{
	case FUSION_RTAI_DOMAIN:

	    if (testbits(thread->status,XNRELAX))
		xnshadow_harden(NULL);

	    setbits(thread->status,XNAUTOSW);
	    break;

	case FUSION_LINUX_DOMAIN:

	    if (!testbits(thread->status,XNRELAX))
		xnshadow_relax();

	    clrbits(thread->status,XNAUTOSW);
	    break;

	default:

	    return -EINVAL;
	}

    return 0;
}

static int __pthread_hold_vm (struct task_struct *curr, struct pt_regs *regs)

{
    if (!__xn_access_ok(curr,VERIFY_WRITE,(void *)__xn_reg_arg1(regs),sizeof(int)))
	return -EFAULT;

    xnmutex_lock(&__imutex);

    __xn_put_user(curr,1,(int *)__xn_reg_arg1(regs)); /* Raise the pend flag */

    xnsynch_sleep_on(&__fusion_barrier,XN_INFINITE,&__imutex);

    xnmutex_unlock(&__imutex);

    return 0;
}

static int __pthread_release_vm (struct task_struct *curr, struct pt_regs *regs)

{
    if (!__xn_access_ok(curr,VERIFY_WRITE,(void *)__xn_reg_arg1(regs),sizeof(int)))
	return -EFAULT;

    xnmutex_lock(&__imutex);

    __xn_put_user(curr,0,(int *)__xn_reg_arg1(regs)); /* Clear the lock flag */

    if (xnsynch_flush(&__fusion_barrier,XNBREAK) == XNSYNCH_RESCHED)
	xnpod_schedule(&__imutex);

    xnmutex_unlock(&__imutex);

    return 0;
}

static int __pthread_idle_vm (struct task_struct *curr, struct pt_regs *regs)

{
    xnthread_t *thread = xnpod_current_thread();

    if (!__xn_access_ok(curr,VERIFY_WRITE,(void *)__xn_reg_arg1(regs),sizeof(int)))
	return -EFAULT;

    xnmutex_lock(&__imutex);

    __xn_put_user(curr,0,(int *)__xn_reg_arg1(regs)); /* Clear the lock flag */

    xnpod_renice_thread(thread,xnthread_initial_priority(thread));

    if (xnsynch_nsleepers(&__fusion_barrier) > 0)
	xnsynch_flush(&__fusion_barrier,XNBREAK);

    xnpod_suspend_thread(thread,XNSUSP,XN_INFINITE,NULL,&__imutex);

    xnmutex_unlock(&__imutex);

    return 0;
}

static int __pthread_activate_vm (struct task_struct *curr, struct pt_regs *regs)

{
    xnthread_t *prev, *next;
    int err = -ESRCH;

    xnmutex_lock(&__imutex);

    next = __pthread_find_by_handle(curr,(void *)__xn_reg_arg1(regs));

    if (!next)
	goto out;

    prev = __pthread_find_by_handle(curr,(void *)__xn_reg_arg2(regs));

    if (!prev)
	goto out;

    xnpod_renice_thread(next,xnthread_initial_priority(next) + 1);

    if (!testbits(next->status,XNSTARTED))
	xnshadow_start(next,0,NULL,NULL,0);
    else if (testbits(next->status,XNSUSP))
	xnpod_resume_thread(next,XNSUSP);

    xnpod_renice_thread(prev,xnthread_initial_priority(prev));

    if (!testbits(prev->status,XNSUSP))
	xnpod_suspend_thread(prev,XNSUSP,XN_INFINITE,NULL,&__imutex);

    xnpod_schedule(&__imutex);

    err = 0;

 out:

    xnmutex_unlock(&__imutex);

    return err;
}

static int __pthread_cancel_vm (struct task_struct *curr, struct pt_regs *regs)

{
    xnthread_t *dead, *next;
    int err = -ESRCH;

    xnmutex_lock(&__imutex);

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

	xnpod_renice_thread(next,xnthread_initial_priority(next) + 1);

	if (testbits(next->status,XNSTARTED))
	    xnshadow_start(next,0,NULL,NULL,0);
	else if (testbits(next->status,XNSUSP))
	    xnpod_resume_thread(next,XNSUSP);
	}

    err = 0;

    xnpod_delete_thread(dead,&__imutex);

out:

    xnmutex_unlock(&__imutex);

    return err;
}

static void fusion_shadow_delete_hook (xnthread_t *thread)

{
    if (xnthread_magic(thread) == FUSION_SKIN_MAGIC)
	{
	xnshadow_unmap(thread);
	xnfree(thread);
	}
}

/* User-space skin services -- The declaration order must be in sync
   with the opcodes defined in xenomai/fusion.h. Services marked by
   the __xn_flag_suspensive bit must be propagated to the caller's
   domain. */

static xnsysent_t fusion_systab[] = {
    { &__pthread_init_rt, __xn_flag_suspensive|__xn_flag_anycontext }, /* 0: __xn_fusion_init */
    { &__pthread_create_rt, __xn_flag_suspensive|__xn_flag_anycontext }, /* 1: __xn_fusion_create */
    { &__pthread_start_rt, __xn_flag_suspensive }, /* 2: __xn_fusion_start */
    { &__pthread_migrate_rt, __xn_flag_suspensive },   /* 3: __xn_fusion_migrate */
    { &__pthread_time_rt, __xn_flag_anycontext  },   /* 4: __xn_fusion_time */
    { &__pthread_inquire_rt, __xn_flag_anycontext },   /* 5: __xn_fusion_inquire */
    { &__pthread_idle_vm, __xn_flag_suspensive }, /* 6: __xn_fusion_idle */
    { &__pthread_cancel_vm, __xn_flag_suspensive }, /* 7: __xn_fusion_cancel */
    { &__pthread_activate_vm, __xn_flag_suspensive },   /* 8: __xn_fusion_activate */
    { &__pthread_hold_vm, __xn_flag_suspensive },   /* 9: __xn_fusion_hold */
    { &__pthread_release_vm, __xn_flag_suspensive },   /* 10: __xn_fusion_release */
};

int fusion_register_skin (void)

{
    __fusion_muxid = xnshadow_register_skin(FUSION_SKIN_MAGIC,
					    sizeof(fusion_systab) / sizeof(fusion_systab[0]),
					    fusion_systab);
    if (__fusion_muxid < 0)
	return __fusion_muxid;

    xnpod_add_hook(XNHOOK_THREAD_DELETE,&fusion_shadow_delete_hook);

    xnsynch_init(&__fusion_barrier,XNSYNCH_FIFO);

    return XN_OK;
}

void fusion_unregister_skin (void)

{
    xnmutex_lock(&__imutex);

    if (xnsynch_destroy(&__fusion_barrier) == XNSYNCH_RESCHED)
	xnpod_schedule(&__imutex);

    xnmutex_unlock(&__imutex);

    xnpod_remove_hook(XNHOOK_THREAD_DELETE,&fusion_shadow_delete_hook);

    xnshadow_unregister_skin(__fusion_muxid);
}
