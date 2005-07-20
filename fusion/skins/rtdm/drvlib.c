/**
 * @file
 * Real-Time Driver Model for RTAI, driver library
 *
 * @note Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>
 * @note Copyright (C) 2005 Joerg Langenberg <joergel75@gmx.net>
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
 * along with RTAI/fusion; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*!
 * @ingroup rtdm
 * @defgroup driverapi Driver Development API
 *
 * This is the lower interface of RTDM provided to device drivers, currently
 * limited to kernel-space. Real-time drivers should only use functions of
 * this interface in order to remain portable.
 */


#include <linux/delay.h>

#include <rtdm/rtdm_driver.h>


/*!
 * @ingroup driverapi
 * @defgroup clock Clock Services
 * @{
 */

/**
 * @brief Get system time
 *
 * @return The system time in nanoseconds is returned
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 *
 * Rescheduling: never.
 */
__u64 rtdm_clock_read(void)
{
#ifdef CONFIG_RTAI_HW_APERIODIC_TIMER
    if (!testbits(nkpod->status,XNTMPER))
        return xnpod_get_cpu_time();
    else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
        return xnpod_ticks2ns(nkpod->wallclock);
}
/** @} */


/*!
 * @ingroup driverapi
 * @defgroup rtdmtask Task Services
 * @{
 */

#if DOXYGEN_CPP /* Only used for doxygen doc generation */
/**
 * @brief Intialise and start a real-time task
 *
 * @param[in,out] task Task handle
 * @param[in] name Optional task name
 * @param[in] task_proc Procedure to be executed by the task
 * @param[in] arg Custom argument passed to @c task_proc() on entry
 * @param[in] priority Priority of the task, see also
 * @ref taskprio "Task Priority Range"
 * @param[in] period Period in nanosecons of a cyclic task, 0 for non-cyclic
 * mode
 *
 * @return 0 on success, otherwise negative error code
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 *
 * Rescheduling: possible.
 */
int rtdm_task_init(rtdm_task_t *task, const char *name,
                   rtdm_task_proc_t task_proc, void *arg,
                   int priority, __u64 period);

/**
 * @brief Destroy a real-time task
 *
 * @param[in,out] task Task handle
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 *
 * Rescheduling: never.
 */
void rtdm_task_destroy(rtdm_task_t *task);

/**
 * @brief Adjust real-time task priority
 *
 * @param[in,out] task Task handle
 * @param[in] priority New priority of the task, see also
 * @ref taskprio "Task Priority Range"
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 *
 * Rescheduling: possible.
 */
void rtdm_task_set_priority(rtdm_task_t *task, int priority);

/**
 * @brief Adjust real-time task period
 *
 * @param[in,out] task Task handle
 * @param[in] period New period in nanosecons of a cyclic task, 0 for
 * non-cyclic mode
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 *
 * Rescheduling: possible.
 */
int rtdm_task_set_period(rtdm_task_t *task, __u64 period);

/**
 * @brief Wait on next real-time task period
 *
 * @return 0 on success, otherwise:
 *
 * - -EINVAL is returned if calling task is not in periodic mode.
 *
 * - -ETIMEDOUT is returned if a timer overrun occurred, which indicates
 * that a previous release point has been missed by the calling task.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 *
 * Rescheduling: possible.
 */
int rtdm_task_wait_period(void);

/**
 * @brief Activate a blocked real-time task
 *
 * @return 0 on success, otherwise a negative error code
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 *
 * Rescheduling: possible.
 */
int rtdm_task_unblock(rtdm_task_t *task);
#endif /* DOXYGEN_CPP */


/**
 * Wait on real-time task termination
 */
void rtdm_task_join_nrt(rtdm_task_t *task, unsigned int poll_delay)
{
    spl_t s;


    xnlock_get_irqsave(&nklock, s);

    while (!xnthread_test_flags(task, XNZOMBIE)) {
        xnlock_put_irqrestore(&nklock, s);

        msleep(poll_delay);

        xnlock_get_irqsave(&nklock, s);
    }

    xnlock_put_irqrestore(&nklock, s);
}


/**
 * Sleep a specified amount of time
 */
int rtdm_task_sleep(__u64 delay)
{
    xnthread_t  *thread = xnpod_current_thread();


    xnpod_suspend_thread(thread, XNDELAY, xnpod_ns2ticks(delay), NULL);

    return xnthread_test_flags(thread, XNBREAK) ? -EINTR : 0;
}


/**
 * Sleep until a specified absolute time
 */
int rtdm_task_sleep_until(__u64 wakeup_time)
{
    xnthread_t  *thread = xnpod_current_thread();
    xnsticks_t  delay;
    spl_t       s;
    int         err = 0;


    xnlock_get_irqsave(&nklock, s);

    delay = xnpod_ns2ticks(wakeup_time) - xnpod_get_time();

    if (likely(delay > 0)) {
        xnpod_suspend_thread(thread, XNDELAY, delay, NULL);

        if (xnthread_test_flags(thread, XNBREAK))
            err = -EINTR;
    }

    xnlock_put_irqrestore(&nklock, s);

    return err;
}


/**
 * Busy-wait a specified amount of time
 */
void rtdm_task_busy_sleep(__u64 delay)
{
    xnticks_t wakeup = xnarch_get_cpu_tsc() + xnarch_ns_to_tsc(delay);

    while (xnarch_get_cpu_tsc() < wakeup);
}
/** @} */



/* --- IPC cleanup helper --- */
void _rtdm_synch_flush(xnsynch_t *synch, unsigned long reason)
{
    spl_t s;


    xnlock_get_irqsave(&nklock,s);

    if (likely(xnsynch_flush(synch, reason) == XNSYNCH_RESCHED))
        xnpod_schedule();

    xnlock_put_irqrestore(&nklock, s);
}



/*!
 * @ingroup driverapi
 * @defgroup rtdmsync Synchronisation Services
 * @{
 */

/*!
 * @name Event Services
 * @{
 */

#if DOXYGEN_CPP /* Only used for doxygen doc generation */
/**
 * Initialise an event
 */
void rtdm_event_init(rtdm_event_t *event, unsigned long pending);

/**
 * Destroy an event
 */
void rtdm_event_destroy(rtdm_event_t *event);

/**
 * Signal an event only to currently listening waiters
 */
void rtdm_event_pulse(rtdm_event_t *event);
#endif /* DOXYGEN_CPP */


/**
 * Wait on event occurrence
 */
int rtdm_event_wait(rtdm_event_t *event, __s64 timeout)
{
    spl_t   s;
    int     err = 0;


    xnlock_get_irqsave(&nklock, s);

    if (!__test_and_clear_bit(0, &event->pending)) {
        xnthread_t  *thread = xnpod_current_thread();

        if (timeout < 0)    /* non-blocking mode */
            err = -EWOULDBLOCK;
        else {
            xnsynch_sleep_on(&event->synch_base, xnpod_ns2ticks(timeout));

            if (!xnthread_test_flags(thread, XNTIMEO|XNRMID|XNBREAK))
                __clear_bit(0, &event->pending);
            else if (xnthread_test_flags(thread, XNTIMEO))
                err = -ETIMEDOUT;
            else if (xnthread_test_flags(thread, XNRMID))
                err = -EIDRM;
            else /* XNBREAK */
                err = -EINTR;
        }
    }

    xnlock_put_irqrestore(&nklock, s);

    return err;
}


/**
 * Wait on event occurrence with absolute timeout
 */
int rtdm_event_wait_until(rtdm_event_t *event, __u64 abstimeout)
{
    spl_t   s;
    int     err = 0;


    xnlock_get_irqsave(&nklock, s);

    if (!__test_and_clear_bit(0, &event->pending)) {
        xnthread_t  *thread = xnpod_current_thread();
        xnsticks_t  delay;

        delay = xnpod_ns2ticks(abstimeout) - xnpod_get_time();

        if (likely(delay > 0)) {
            xnsynch_sleep_on(&event->synch_base, delay);

            if (!xnthread_test_flags(thread, XNTIMEO|XNRMID|XNBREAK))
                __clear_bit(0, &event->pending);
            else if (xnthread_test_flags(thread, XNTIMEO))
                err = -ETIMEDOUT;
            else if (xnthread_test_flags(thread, XNRMID))
                err = -EIDRM;
            else /* XNBREAK */
                err = -EINTR;
        } else
            err = -ETIMEDOUT;
    }

    xnlock_put_irqrestore(&nklock, s);

    return err;
}


/**
 * Signal an event occurrence
 */
void rtdm_event_signal(rtdm_event_t *event)
{
    spl_t s;


    xnlock_get_irqsave(&nklock, s);

    __set_bit(0, &event->pending);
    if (xnsynch_flush(&event->synch_base, 0))
        xnpod_schedule();

    xnlock_put_irqrestore(&nklock, s);
}
/** @} */



/*!
 * @name Semaphore Services
 * @{
 */

#if DOXYGEN_CPP /* Only used for doxygen doc generation */
/**
 * Initialise a semaphore
 */
void rtdm_sem_init(rtdm_sem_t *sem, unsigned long value);

/**
 * Destroy a semaphore
 */
void rtdm_sem_destroy(rtdm_sem_t *sem);
#endif /* DOXYGEN_CPP */

/**
 * Decrement a semaphore
 */
int rtdm_sem_down(rtdm_sem_t *sem, __s64 timeout)
{
    spl_t   s;
    int     err = 0;


    xnlock_get_irqsave(&nklock, s);

    if (sem->value > 0)
        sem->value--;
    else if (timeout < 0)   /* non-blocking mode */
        err = -EWOULDBLOCK;
    else {
        xnthread_t  *thread = xnpod_current_thread();


        xnsynch_sleep_on(&sem->synch_base, xnpod_ns2ticks(timeout));

        if (xnthread_test_flags(thread, XNTIMEO|XNRMID|XNBREAK)) {
            if (xnthread_test_flags(thread, XNTIMEO))
                err = -ETIMEDOUT;
            else if (xnthread_test_flags(thread, XNRMID))
                err = -EIDRM;
            else /*  XNBREAK */
                err = -EINTR;
        }
    }

    xnlock_put_irqrestore(&nklock, s);

    return err;
}


/**
 * Increment a semaphore
 */
void rtdm_sem_up(rtdm_sem_t *sem)
{
    spl_t s;


    xnlock_get_irqsave(&nklock, s);

    if (xnsynch_wakeup_one_sleeper(&sem->synch_base))
        xnpod_schedule();
    else
        sem->value++;

    xnlock_put_irqrestore(&nklock, s);
}
/** @} */



/*!
 * @name Mutex Services
 * @{
 */

#if DOXYGEN_CPP /* Only used for doxygen doc generation */
/**
 * Initialise a mutex
 */
void rtdm_mutex_init(rtdm_mutex_t *mutex);

/**
 * Destroy a mutex
 */
void rtdm_mutex_destroy(rtdm_mutex_t *mutex);
#endif /* DOXYGEN_CPP */


/**
 * Request a mutex
 */
int rtdm_mutex_lock(rtdm_mutex_t *mutex)
{
    spl_t   s;
    int     err = 0;


    xnlock_get_irqsave(&nklock, s);

    while (__test_and_set_bit(0, &mutex->locked)) {
        xnsynch_sleep_on(&mutex->synch_base, XN_INFINITE);

        if (xnthread_test_flags(xnpod_current_thread(), XNRMID))
            err = -EIDRM;
    }

    xnlock_put_irqrestore(&nklock, s);

    return err;
}


/**
 * Request a mutex with timeout
 */
int rtdm_mutex_timedlock(rtdm_mutex_t *mutex, __s64 timeout)
{
    spl_t   s;
    int     err = 0;


    xnlock_get_irqsave(&nklock, s);

    while (__test_and_set_bit(0, &mutex->locked)) {
        if (timeout < 0) {  /* non-blocking mode */
            err = -EWOULDBLOCK;
            break;
        } else {
            xnthread_t  *thread = xnpod_current_thread();

            xnsynch_sleep_on(&mutex->synch_base, xnpod_ns2ticks(timeout));

            if (xnthread_test_flags(thread, XNTIMEO|XNRMID)) {
                if (xnthread_test_flags(thread, XNTIMEO))
                    err = -ETIMEDOUT;
                else /*  XNRMID */
                    err = -EIDRM;
                break;
            }
        }
    }

    xnlock_put_irqrestore(&nklock, s);

    return err;
}


/**
 * Release a mutex
 */
void rtdm_mutex_unlock(rtdm_mutex_t *mutex)
{
    spl_t s;


    xnlock_get_irqsave(&nklock, s);

    __clear_bit(0, &mutex->locked);
    if (likely(xnsynch_wakeup_one_sleeper(&mutex->synch_base)))
        xnpod_schedule();

    xnlock_put_irqrestore(&nklock, s);
}
/** @} */

/** @} Synchronisation services */


#if DOXYGEN_CPP /* Only used for doxygen doc generation */

/*!
 * @ingroup driverapi
 * @defgroup rtdmirq Interrupt Management Services
 * @{
 */

/**
 * Register an interrupt handler
 */
int rtdm_irq_request(rtdm_irq_t *irq_handle, unsigned int irq_no,
                     rtdm_irq_handler_t handler, unsigned long flags,
                     const char *device_name, void *arg);

/**
 * Release an interrupt handler
 */
int rtdm_irq_free(rtdm_irq_t *irq_handle);

/**
 * Enable interrupt line
 */
int rtdm_irq_enable(rtdm_irq_t *irq_handle);

/**
 * Disable interrupt line
 */
int rtdm_irq_disable(rtdm_irq_t *irq_handle);
/** @} */


/*!
 * @ingroup driverapi
 * @defgroup nrtsignal Non-Real-Time Signalling Services
 * @{
 */

/**
 * Register a non-real-time signal handler
 */
int rtdm_nrt_signal_init(rtdm_nrt_signal_t *nrt_sig,
                         rtdm_nrt_sig_handler_t handler);

/**
 * Release a non-realtime signal handler
 */
void rtdm_nrt_signal_destroy(rtdm_nrt_signal_t *nrt_sig);

/**
 * Trigger non-real-time signal
 */
void rtdm_nrt_pend_signal(rtdm_nrt_signal_t *nrt_sig);
/** @} */


/*!
 * @ingroup driverapi
 * @defgroup util Utility Services
 * @{
 */

/**
 * Real-time safe message printing on kernel console
 */
void rtdm_printk(const char *format, ...);

/**
 * Allocate memory block in real-time context
 */
void *rtdm_malloc(size_t size);

/**
 * Release real-time memory block
 */
void rtdm_free(void *ptr);

/**
 * Check if read access to user-space memory block is safe
 */
int rtdm_read_user_ok(rtdm_user_info_t *user_info, const void __user *ptr,
                      size_t size);

/**
 * Check if read/write access to user-space memory block is safe
 */
int rtdm_rw_user_ok(rtdm_user_info_t *user_info, const void __user *ptr,
                    size_t size);

/**
 * Copy user-space memory block to specified buffer
 */
int rtdm_copy_from_user(rtdm_user_info_t *user_info, void *dst,
                        const void __user *src, size_t size);

/**
 * Copy specified buffer to user-space memory block
 */
int rtdm_copy_to_user(rtdm_user_info_t *user_info, void __user *dst,
                      const void *src, size_t size);

/**
 * Copy user-space string to specified buffer
 */
int rtdm_strncpy_from_user(rtdm_user_info_t *user_info, char *dst,
                           const char __user *src, size_t count);

/**
 * Test if running in a real-time task
 */
int rtdm_in_rt_context(void);

/** @} */

#endif /* DOXYGEN_CPP */


EXPORT_SYMBOL(rtdm_clock_read);
EXPORT_SYMBOL(rtdm_task_join_nrt);
EXPORT_SYMBOL(rtdm_task_sleep);
EXPORT_SYMBOL(rtdm_task_sleep_until);
EXPORT_SYMBOL(rtdm_task_busy_sleep);
EXPORT_SYMBOL(_rtdm_synch_flush);
EXPORT_SYMBOL(rtdm_event_wait);
EXPORT_SYMBOL(rtdm_event_wait_until);
EXPORT_SYMBOL(rtdm_event_signal);
EXPORT_SYMBOL(rtdm_sem_down);
EXPORT_SYMBOL(rtdm_sem_up);
EXPORT_SYMBOL(rtdm_mutex_lock);
EXPORT_SYMBOL(rtdm_mutex_timedlock);
EXPORT_SYMBOL(rtdm_mutex_unlock);
