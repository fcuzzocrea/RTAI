/**
 * @file
 * This file is part of the RTAI project.
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org> 
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
 * \ingroup cond
 */

/*!
 * \ingroup native
 * \defgroup cond Condition variable services.
 *
 * Condition variable services.
 *
 *@{*/

#include <nucleus/pod.h>
#include <rtai/task.h>
#include <rtai/mutex.h>
#include <rtai/cond.h>
#include <rtai/registry.h>

int __cond_pkg_init (void)

{
    return 0;
}

void __cond_pkg_cleanup (void)

{
}

int rt_cond_create (RT_COND *cond,
		    const char *name)
{
    int err = 0;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnsynch_init(&cond->synch_base,XNSYNCH_PRIO|XNSYNCH_PIP);
    cond->handle = 0;  /* i.e. (still) unregistered cond. */
    cond->magic = RTAI_COND_MAGIC;
    xnobject_copy_name(cond->name,name);

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    /* <!> Since rt_register_enter() may reschedule, only register
       complete objects, so that the registry cannot return handles to
       half-baked objects... */

    if (name && *name)
        {
        err = rt_registry_enter(cond->name,cond,&cond->handle);

        if (err)
            rt_cond_delete(cond);
        }
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    return err;
}

int rt_cond_delete (RT_COND *cond)

{
    int err = 0, rc;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    splhigh(s);

    cond = rtai_h2obj_validate(cond,RTAI_COND_MAGIC,RT_COND);

    if (!cond)
        {
        err = rtai_handle_error(cond,RTAI_COND_MAGIC,RT_COND);
        goto unlock_and_exit;
        }
    
    rc = xnsynch_destroy(&cond->synch_base);

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    if (cond->handle)
        rt_registry_remove(cond->handle);
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    rtai_mark_deleted(cond);

    if (rc == XNSYNCH_RESCHED)
        /* Some task has been woken up as a result of the deletion:
           reschedule now. */
        xnpod_schedule();

 unlock_and_exit:

    splexit(s);

    return err;
}

int rt_cond_signal (RT_COND *cond)

{
    int err = 0;
    spl_t s;

    splhigh(s);

    cond = rtai_h2obj_validate(cond,RTAI_COND_MAGIC,RT_COND);

    if (!cond)
        {
        err = rtai_handle_error(cond,RTAI_COND_MAGIC,RT_COND);
        goto unlock_and_exit;
        }

    if (thread2rtask(xnsynch_wakeup_one_sleeper(&cond->synch_base)) != NULL)
	{
	xnsynch_set_owner(&cond->synch_base,NULL); /* No ownership to track. */
	xnpod_schedule();
	}

 unlock_and_exit:

    splexit(s);

    return err;
}

int rt_cond_broadcast (RT_COND *cond)

{
    int err = 0;
    spl_t s;

    splhigh(s);

    cond = rtai_h2obj_validate(cond,RTAI_COND_MAGIC,RT_COND);

    if (!cond)
        {
        err = rtai_handle_error(cond,RTAI_COND_MAGIC,RT_COND);
        goto unlock_and_exit;
        }

    if (xnsynch_flush(&cond->synch_base,0) == XNSYNCH_RESCHED)
	xnpod_schedule();

 unlock_and_exit:

    splexit(s);

    return err;
}

int rt_cond_wait (RT_COND *cond,
		  RT_MUTEX *mutex,
		  RTIME timeout)
{
    RT_TASK *task;
    int err;
    spl_t s;

    if (timeout == RT_TIME_NONBLOCK)
	return -EWOULDBLOCK;
    
    splhigh(s);

    cond = rtai_h2obj_validate(cond,RTAI_COND_MAGIC,RT_COND);

    if (!cond)
        {
        err = rtai_handle_error(cond,RTAI_COND_MAGIC,RT_COND);
        goto unlock_and_exit;
        }

    err = rt_mutex_unlock(mutex);

    if (err)
	goto unlock_and_exit;

    task = rtai_current_task();

    xnsynch_sleep_on(&cond->synch_base,timeout);
        
    if (xnthread_test_flags(&task->thread_base,XNRMID))
	err = -EIDRM; /* Condvar deleted while pending. */
    else if (xnthread_test_flags(&task->thread_base,XNTIMEO))
	err = -ETIMEDOUT; /* Timeout.*/
    else if (xnthread_test_flags(&task->thread_base,XNBREAK))
	err = -EINTR; /* Unblocked.*/

    rt_mutex_lock(mutex);

 unlock_and_exit:

    splexit(s);

    return err;
}

int rt_cond_inquire (RT_COND *cond,
		     RT_COND_INFO *info)
{
    int err = 0;
    spl_t s;

    splhigh(s);

    cond = rtai_h2obj_validate(cond,RTAI_COND_MAGIC,RT_COND);

    if (!cond)
        {
        err = rtai_handle_error(cond,RTAI_COND_MAGIC,RT_COND);
        goto unlock_and_exit;
        }
    
    strcpy(info->name,cond->name);
    info->nsleepers = xnsynch_nsleepers(&cond->synch_base);

 unlock_and_exit:

    splexit(s);

    return err;
}

/*@}*/

EXPORT_SYMBOL(rt_cond_create);
EXPORT_SYMBOL(rt_cond_delete);
EXPORT_SYMBOL(rt_cond_signal);
EXPORT_SYMBOL(rt_cond_broadcast);
EXPORT_SYMBOL(rt_cond_wait);
EXPORT_SYMBOL(rt_cond_inquire);
