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
 * \ingroup alarm
 */

/*!
 * \ingroup native
 * \defgroup alarm Alarm services.
 *
 * 
 *
 *@{*/

#include <nucleus/pod.h>
#include <rtai/task.h>
#include <rtai/alarm.h>
#include <rtai/registry.h>

int __alarm_pkg_init (void)

{
    return 0;
}

void __alarm_pkg_cleanup (void)

{
}

static void __alarm_trampoline (void *cookie)

{
    RT_ALARM *alarm = (RT_ALARM *)cookie;
    alarm->handler(alarm,alarm->cookie);
}

/**
 * @fn int rt_alarm_create(RT_ALARM *alarm,
                           rt_alarm_t handler,
                           void *cookie,
                           const char *name)
 * @brief Create an alarm object.
 *
 * Create an object calling an alarm routine at specified
 * times. Alarms can be made periodic or aperiodic, depending on the
 * reload interval value passed to rt_alarm_start() for them.
 *
 * @param alarm The address of an alarm descriptor RTAI will use to
 * store the alarm-related data.  This descriptor must always be valid
 * while the alarm is active therefore it must be allocated in
 * permanent memory.
 *
 * @param handler The address of the routine to call when the alarm
 * triggers. This routine will be passed the address of the current
 * alarm descriptor, and the opaque @a cookie.
 *
 * @param cookie A user-defined opaque cookie the real-time kernel
 * will pass to the alarm handler as its second argument.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * alarm. When non-NULL and non-empty, this string is copied to a safe
 * place into the descriptor, and passed to the registry package if
 * enabled for indexing the created alarm.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EEXIST is returned if the @a name is already in use by some
 * registered object.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int rt_alarm_create (RT_ALARM *alarm,
		     rt_alarm_t handler,
		     void *cookie,
		     const char *name)
{
    int err = 0;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xntimer_init(&alarm->timer_base,&__alarm_trampoline,alarm);
    alarm->handle = 0;  /* i.e. (still) unregistered alarm. */
    alarm->magic = RTAI_ALARM_MAGIC;
    alarm->handler = handler;
    alarm->cookie = cookie;
    xnobject_copy_name(alarm->name,name);

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    /* <!> Since rt_register_enter() may reschedule, only register
       complete objects, so that the registry cannot return handles to
       half-baked objects... */

    if (name && *name)
        {
        err = rt_registry_enter(alarm->name,alarm,&alarm->handle);

        if (err)
            rt_alarm_delete(alarm);
        }
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    return err;
}

/**
 * @fn int rt_alarm_delete(RT_ALARM *alarm)
 * @brief Delete an alarm.
 *
 * Destroy an alarm. An alarm exists in the system since
 * rt_alarm_create() has been called to create it, so this service
 * must be called in order to destroy it afterwards.
 *
 * @param alarm The descriptor address of the affected alarm.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a alarm is not a alarm descriptor.
 *
 * - -EIDRM is returned if @a alarm is a deleted alarm descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int rt_alarm_delete (RT_ALARM *alarm)

{
    int err = 0;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock,s);

    alarm = rtai_h2obj_validate(alarm,RTAI_ALARM_MAGIC,RT_ALARM);

    if (!alarm)
        {
        err = rtai_handle_error(alarm,RTAI_ALARM_MAGIC,RT_ALARM);
        goto unlock_and_exit;
        }
    
    xntimer_destroy(&alarm->timer_base);

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    if (alarm->handle)
        rt_registry_remove(alarm->handle);
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    rtai_mark_deleted(alarm);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_alarm_start(RT_ALARM *alarm,
                          RTIME value,
                          RTIME interval)
 * @brief Start an alarm.
 *
 * Program the trigger date of an alarm object. An alarm can be either
 * periodic or single-shot, depending on the reload value passed to
 * this routine. The given alarm must have been previously created by
 * a call to rt_alarm_create().
 *
 * Alarm handlers are always called on behalf of RTAI's internal timer
 * tick handler, so the RTAI services which can be called from such
 * handlers are restricted to the set of services available on behalf
 * of any ISR.
 *
 * @param alarm The descriptor address of the affected alarm.
 *
 * @param value The relative date of the initial alarm shot, expressed
 * in clock ticks (see note).
 *
 * @param interval The reload value of the alarm. It is a periodic
 * interval value to be used for reprogramming the next alarm shot,
 * expressed in clock ticks (see note). If @a interval is equal to
 * TM_INFINITE, the alarm will not be reloaded after it has expired.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a alarm is not a alarm descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are expressed as periodic jiffies. In
 * oneshot mode, clock ticks are expressed as CPU ticks (e.g. TSC
 * value).
 */

int rt_alarm_start (RT_ALARM *alarm,
		    RTIME value,
		    RTIME interval)
{
    int err = 0;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock,s);

    alarm = rtai_h2obj_validate(alarm,RTAI_ALARM_MAGIC,RT_ALARM);

    if (!alarm)
        {
        err = rtai_handle_error(alarm,RTAI_ALARM_MAGIC,RT_ALARM);
        goto unlock_and_exit;
        }

    xntimer_start(&alarm->timer_base,value,interval);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_alarm_stop(RT_ALARM *alarm)
 * @brief Stop an alarm.
 *
 * Disarm an alarm object previously armed using rt_alarm_start() so
 * that it will not trigger until is is re-armed.
 *
 * @param alarm The descriptor address of the released alarm.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a alarm is not a alarm descriptor.
 *
 * - -EIDRM is returned if @a alarm is a deleted alarm descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int rt_alarm_stop (RT_ALARM *alarm)

{
    int err = 0;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock,s);

    alarm = rtai_h2obj_validate(alarm,RTAI_ALARM_MAGIC,RT_ALARM);

    if (!alarm)
        {
        err = rtai_handle_error(alarm,RTAI_ALARM_MAGIC,RT_ALARM);
        goto unlock_and_exit;
        }

    xntimer_stop(&alarm->timer_base);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_alarm_inquire(RT_ALARM *alarm, RT_ALARM_INFO *info)
 * @brief Inquire about an alarm.
 *
 * Return various information about the status of a given alarm.
 *
 * @param alarm The descriptor address of the inquired alarm.
 *
 * @param info The address of a structure the alarm information will
 * be written to.
 *
 * The expiration date returned in the information block is converted
 * to the current time unit. The special value TM_INFINITE is returned
 * if @a alarm is currently inactive/stopped. In single-shot mode, it
 * might happen that the alarm has already expired when this service
 * is run (even if the associated handler has not been fired yet); in
 * such a case, 1 is returned.
 *
 * @return 0 is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a alarm is not a alarm descriptor.
 *
 * - -EIDRM is returned if @a alarm is a deleted alarm descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int rt_alarm_inquire (RT_ALARM *alarm,
                      RT_ALARM_INFO *info)
{
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    alarm = rtai_h2obj_validate(alarm,RTAI_ALARM_MAGIC,RT_ALARM);

    if (!alarm)
        {
        err = rtai_handle_error(alarm,RTAI_ALARM_MAGIC,RT_ALARM);
        goto unlock_and_exit;
        }
    
    strcpy(info->name,alarm->name);
    info->expiration = xntimer_get_timeout(&alarm->timer_base);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*@}*/

EXPORT_SYMBOL(rt_alarm_create);
EXPORT_SYMBOL(rt_alarm_delete);
EXPORT_SYMBOL(rt_alarm_start);
EXPORT_SYMBOL(rt_alarm_stop);
EXPORT_SYMBOL(rt_alarm_inquire);
