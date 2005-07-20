/*
 * Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>.
 * Copyright (C) 2005 Joerg Langenberg <joergel75@gmx.net>.
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

#include <linux/delay.h>

#include <rtdm/rtdm_driver.h>


/* --- clock services --- */

__u64 rtdm_clock_read(void)
{
#ifdef CONFIG_RTAI_HW_APERIODIC_TIMER
    if (!testbits(nkpod->status,XNTMPER))
        return xnpod_get_cpu_time();
    else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
        return xnpod_ticks2ns(nkpod->wallclock);
}


/* --- task and timing services --- */

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


int rtdm_task_sleep(__u64 delay)
{
    xnthread_t  *thread = xnpod_current_thread();


    xnpod_suspend_thread(thread, XNDELAY, xnpod_ns2ticks(delay), NULL);

    return xnthread_test_flags(thread, XNBREAK) ? -EINTR : 0;
}


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


void rtdm_task_busy_sleep(__u64 delay)
{
    xnticks_t wakeup = xnarch_get_cpu_tsc() + xnarch_ns_to_tsc(delay);

    while (xnarch_get_cpu_tsc() < wakeup);
}



/* --- IPC cleanup helper --- */

void _rtdm_synch_flush(xnsynch_t *synch, unsigned long reason)
{
    spl_t s;


    xnlock_get_irqsave(&nklock,s);

    if (likely(xnsynch_flush(synch, reason) == XNSYNCH_RESCHED))
        xnpod_schedule();

    xnlock_put_irqrestore(&nklock, s);
}


/* --- event services --- */

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


void rtdm_event_signal(rtdm_event_t *event)
{
    spl_t s;


    xnlock_get_irqsave(&nklock, s);

    __set_bit(0, &event->pending);
    if (xnsynch_flush(&event->synch_base, 0))
        xnpod_schedule();

    xnlock_put_irqrestore(&nklock, s);
}


/* --- semaphore services --- */

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


/* --- mutex services --- */

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


void rtdm_mutex_unlock(rtdm_mutex_t *mutex)
{
    spl_t s;


    xnlock_get_irqsave(&nklock, s);

    __clear_bit(0, &mutex->locked);
    if (likely(xnsynch_wakeup_one_sleeper(&mutex->synch_base)))
        xnpod_schedule();

    xnlock_put_irqrestore(&nklock, s);
}


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
