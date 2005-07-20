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

#ifndef _RTDM_DRIVER_H
#define _RTDM_DRIVER_H

#ifdef __KERNEL__

#include <asm/atomic.h>
#include <linux/list.h>

#include <nucleus/fusion.h>
#include <nucleus/heap.h>
#include <nucleus/pod.h>
#include <nucleus/synch.h>
#include <rtdm/rtdm.h>


/* device flags */
#define RTDM_EXCLUSIVE              0x0001
#define RTDM_NAMED_DEVICE           0x0010
#define RTDM_PROTOCOL_DEVICE        0x0020
#define RTDM_DEVICE_TYPE            0x00F0

/* context flags (bit numbers) */
#define RTDM_CREATED_IN_NRT         0
#define RTDM_CLOSING                1
#define RTDM_FORCED_CLOSING         2
#define RTDM_USER_CONTEXT_FLAG      8   /* first user-definable flag */

/* version flags */
#define RTDM_SECURE_DEVICE          0x80000000  /* not supported here */

/* structure versions */
#define RTDM_DEVICE_STRUCT_VER      3
#define RTDM_CONTEXT_STRUCT_VER     3

/* API version and compatibility */
#define RTDM_API_VER                3   /* current API version */
#define RTDM_API_MIN_COMPAT_VER     3   /* compatible with API since ... */


struct rtdm_dev_context;


typedef
    int     (*rtdm_open_handler_t)   (struct rtdm_dev_context   *context,
                                      rtdm_user_info_t          *user_info,
                                      int                       oflag);
typedef
    int     (*rtdm_socket_handler_t) (struct rtdm_dev_context   *context,
                                      rtdm_user_info_t          *user_info,
                                      int                       protocol);
typedef
    int     (*rtdm_close_handler_t)  (struct rtdm_dev_context   *context,
                                      rtdm_user_info_t          *user_info);
typedef
    int     (*rtdm_ioctl_handler_t)  (struct rtdm_dev_context   *context,
                                      rtdm_user_info_t          *user_info,
                                      int                       request,
                                      void                      *arg);
typedef
    ssize_t (*rtdm_read_handler_t)   (struct rtdm_dev_context   *context,
                                      rtdm_user_info_t          *user_info,
                                      void                      *buf,
                                      size_t                    nbyte);
typedef
    ssize_t (*rtdm_write_handler_t)  (struct rtdm_dev_context   *context,
                                      rtdm_user_info_t          *user_info,
                                      const void                *buf,
                                      size_t                    nbyte);
typedef
    ssize_t (*rtdm_recvmsg_handler_t)(struct rtdm_dev_context   *context,
                                      rtdm_user_info_t          *user_info,
                                      struct msghdr             *msg,
                                      int                       flags);
typedef
    ssize_t (*rtdm_sendmsg_handler_t)(struct rtdm_dev_context   *context,
                                      rtdm_user_info_t          *user_info,
                                      const struct msghdr       *msg,
                                      int                       flags);

typedef
    int     (*rtdm_rt_handler_t)     (struct rtdm_dev_context   *context,
                                      rtdm_user_info_t          *user_info,
                                      void                      *arg);


struct rtdm_operations {
    /* common operations */
    rtdm_close_handler_t            close_rt;
    rtdm_close_handler_t            close_nrt;
    rtdm_ioctl_handler_t            ioctl_rt;
    rtdm_ioctl_handler_t            ioctl_nrt;

    /* stream-oriented device operations */
    rtdm_read_handler_t             read_rt;
    rtdm_read_handler_t             read_nrt;
    rtdm_write_handler_t            write_rt;
    rtdm_write_handler_t            write_nrt;

    /* message-oriented device operations */
    rtdm_recvmsg_handler_t          recvmsg_rt;
    rtdm_recvmsg_handler_t          recvmsg_nrt;
    rtdm_sendmsg_handler_t          sendmsg_rt;
    rtdm_sendmsg_handler_t          sendmsg_nrt;
};

struct rtdm_dev_context {
    unsigned long                   context_flags;
    int                             fd;
    atomic_t                        close_lock_count;
    struct rtdm_operations          *ops;
    volatile struct rtdm_device     *device;
    char                            dev_private[0];
};

struct rtdm_dev_reserved {
    struct list_head                entry;
    atomic_t                        refcount;
    struct rtdm_dev_context         *exclusive_context;
};

struct rtdm_device {
    int                             struct_version;

    int                             device_flags;
    size_t                          context_size;

    /* named device identification */
    char                            device_name[RTDM_MAX_DEVNAME_LEN+1];

    /* protocol device identification */
    int                             protocol_family;
    int                             socket_type;

    /* device instance creation */
    rtdm_open_handler_t             open_rt;
    rtdm_open_handler_t             open_nrt;

    rtdm_socket_handler_t           socket_rt;
    rtdm_socket_handler_t           socket_nrt;

    struct rtdm_operations          ops;

    int                             device_class;
    int                             device_sub_class;
    const char                      *driver_name;
    const char                      *peripheral_name;
    const char                      *provider_name;

    /* /proc entry */
    const char                      *proc_name;
    struct proc_dir_entry           *proc_entry;

    /* driver-definable id */
    int                             device_id;

    struct rtdm_dev_reserved        reserved;
};


typedef spinlock_t                  rtdm_lock_t;
typedef unsigned long               rtdm_lockctx_t;

typedef unsigned                    rtdm_nrt_signal_t;
typedef void (*rtdm_nrt_sig_handler_t)(rtdm_nrt_signal_t nrt_signal);

typedef xnintr_t                    rtdm_irq_t;
typedef int (*rtdm_irq_handler_t)(rtdm_irq_t *irq_handle);


typedef xnthread_t                  rtdm_task_t;
typedef void (*rtdm_task_proc_t)(void *arg);

typedef struct {
    unsigned long                   pending;
    xnsynch_t                       synch_base;
} rtdm_event_t;

typedef struct {
    unsigned long                   value;
    xnsynch_t                       synch_base;
} rtdm_sem_t;

typedef struct {
    unsigned long                   locked;
    xnsynch_t                       synch_base;
} rtdm_mutex_t;


/* --- device registration --- */

int rtdm_dev_register(struct rtdm_device* device);
int rtdm_dev_unregister(struct rtdm_device* device);


/* --- inter-driver API --- */

#define rtdm_open                   rt_dev_open
#define rtdm_socket                 rt_dev_socket
#define rtdm_close                  rt_dev_close
#define rtdm_ioctl                  rt_dev_ioctl
#define rtdm_read                   rt_dev_read
#define rtdm_write                  rt_dev_write
#define rtdm_rescmsg                rt_dev_recvmsg
#define rtdm_recv                   rt_dev_recv
#define rtdm_recvfrom               rt_dev_recvfrom
#define rtdm_sendmsg                rt_dev_sendmsg
#define rtdm_send                   rt_dev_send
#define rtdm_sendto                 rt_dev_sendto
#define rtdm_bind                   rt_dev_bind
#define rtdm_listen                 rt_dev_listen
#define rtdm_accept                 rt_dev_accept
#define rtdm_getsockopt             rt_dev_getsockopt
#define rtdm_setsockopt             rt_dev_setsockopt
#define rtdm_getsockname            rt_dev_getsockname
#define rtdm_getpeername            rt_dev_getpeername

struct rtdm_dev_context *rtdm_context_get(int fd);

static inline void rtdm_context_lock(struct rtdm_dev_context *context)
{
    atomic_inc(&context->close_lock_count);
}

static inline void rtdm_context_unlock(struct rtdm_dev_context *context)
{
    atomic_dec(&context->close_lock_count);
}


/* --- clock services --- */

__u64 rtdm_clock_read(void);


/* --- spin lock services --- */

#define RTDM_LOCK_UNLOCKED          SPIN_LOCK_UNLOCKED  /* init */
#define rtdm_lock_init(lock)        spin_lock_init(lock)

#define rtdm_lock_get(lock)         rthal_spin_lock(lock)
#define rtdm_lock_put(lock)         rthal_spin_unlock(lock)

#define rtdm_lock_get_irqsave(lock, context)    \
    rthal_spin_lock_irqsave(lock, context)
#define rtdm_lock_put_irqrestore(lock, context) \
    rthal_spin_unlock_irqrestore(lock, context)

#define rtdm_lock_irqsave(context)              \
    rthal_local_irq_save(context)
#define rtdm_lock_irqrestore(context)           \
    rthal_local_irq_restore(context)


/* --- IRQ handler services */

/* return values of the ISR */
#define RTDM_IRQ_PROPAGATE          XN_ISR_CHAINED
#define RTDM_IRQ_ENABLE             XN_ISR_ENABLE

static inline int rtdm_irq_request(rtdm_irq_t *irq_handle,
                                   unsigned int irq_no,
                                   rtdm_irq_handler_t handler,
                                   unsigned long flags,
                                   const char *device_name,
                                   void *arg)
{
    xnintr_init(irq_handle, irq_no, handler, NULL, flags);
    return xnintr_attach(irq_handle, arg);
}

#define rtdm_irq_get_arg(irq_handle, type)  ((type *)irq_handle->cookie)

static inline int rtdm_irq_free(rtdm_irq_t *irq_handle)
{
    return xnintr_detach(irq_handle);
}

static inline int rtdm_irq_enable(rtdm_irq_t *irq_handle)
{
    return xnintr_enable(irq_handle);
}

static inline int rtdm_irq_disable(rtdm_irq_t *irq_handle)
{
    return xnintr_disable(irq_handle);
}


/* --- non-real-time signalling services --- */

static inline int rtdm_nrt_signal_init(rtdm_nrt_signal_t *nrt_sig,
                                       rtdm_nrt_sig_handler_t handler)
{
    *nrt_sig = adeos_alloc_irq();

    if (*nrt_sig > 0)
        adeos_virtualize_irq_from(adp_root, *nrt_sig, handler, NULL,
                                  IPIPE_HANDLE_MASK);
    else
        *nrt_sig = -EBUSY;

    return *nrt_sig;
}

static inline void rtdm_nrt_signal_destroy(rtdm_nrt_signal_t *nrt_sig)
{
    adeos_free_irq(*nrt_sig);
}

static inline void rtdm_pend_nrt_signal(rtdm_nrt_signal_t *nrt_sig)
{
    adeos_trigger_irq(*nrt_sig);
}


/* --- task and timing services --- */

#define RTDM_TASK_LOWEST_PRIORITY   FUSION_LOW_PRIO
#define RTDM_TASK_HIGHEST_PRIORITY  FUSION_HIGH_PRIO

static inline int rtdm_task_init(rtdm_task_t *task, const char *name,
                                 rtdm_task_proc_t task_proc, void *arg,
                                 int priority, __u64 period)
{
    int res;

    res = xnpod_init_thread(task, name, priority, 0, 0);
    if (res)
        goto done;

    if (!__builtin_constant_p(period) || (period != XN_INFINITE)) {
        res = xnpod_set_thread_periodic(task, XN_INFINITE,
                                        xnpod_ns2ticks(period));
        if (res)
            goto done;
    }

    res = xnpod_start_thread(task, 0, 0, XNPOD_ALL_CPUS, task_proc, arg);

  done:
    return res;
}

static inline void rtdm_task_destroy(rtdm_task_t *task)
{
    xnpod_delete_thread(task);
}

void rtdm_task_join_nrt(rtdm_task_t *task, unsigned int poll_delay);

static inline void rtdm_task_set_priority(rtdm_task_t *task, int priority)
{
    xnpod_renice_thread(task, priority);
    xnpod_schedule();
}

static inline int rtdm_task_set_period(rtdm_task_t *task, __u64 period)
{
    return xnpod_set_thread_periodic(task, XN_INFINITE,
                                     xnpod_ns2ticks(period));
}

static inline int rtdm_task_unblock(rtdm_task_t *task)
{
    int res = xnpod_unblock_thread(task);

    xnpod_schedule();
    return res;
}

static inline int rtdm_task_wait_period(void)
{
    return xnpod_wait_thread_period();
}

int rtdm_task_sleep(__u64 delay);
int rtdm_task_sleep_until(__u64 wakeup_time);
void rtdm_task_busy_sleep(__u64 delay);


/* --- event services --- */

static inline void rtdm_event_init(rtdm_event_t *event, unsigned long pending)
{
    event->pending = pending;
    xnsynch_init(&event->synch_base, XNSYNCH_PRIO);
}

void _rtdm_synch_flush(xnsynch_t *synch, unsigned long reason);

static inline void rtdm_event_destroy(rtdm_event_t *event)
{
    _rtdm_synch_flush(&event->synch_base, XNRMID);
}

int rtdm_event_wait(rtdm_event_t *event, __s64 timeout);
int rtdm_event_wait_until(rtdm_event_t *event, __u64 timeout);
void rtdm_event_signal(rtdm_event_t *event);

static inline void rtdm_event_broadcast(rtdm_event_t *event)
{
    _rtdm_synch_flush(&event->synch_base, 0);
}


/* --- semaphore services --- */

static inline void rtdm_sem_init(rtdm_sem_t *sem, unsigned long value)
{
    sem->value = value;
    xnsynch_init(&sem->synch_base, XNSYNCH_PRIO);
}

static inline void rtdm_sem_destroy(rtdm_sem_t *sem)
{
    _rtdm_synch_flush(&sem->synch_base, XNRMID);
}

int rtdm_sem_down(rtdm_sem_t *sem, __s64 timeout);
void rtdm_sem_up(rtdm_sem_t *sem);


/* --- mutex services --- */

static inline void rtdm_mutex_init(rtdm_mutex_t *mutex)
{
    mutex->locked = 0;
    xnsynch_init(&mutex->synch_base, XNSYNCH_PRIO|XNSYNCH_PIP);
}

static inline void rtdm_mutex_destroy(rtdm_mutex_t *mutex)
{
    _rtdm_synch_flush(&mutex->synch_base, XNRMID);
}

int rtdm_mutex_lock(rtdm_mutex_t *mutex);
int rtdm_mutex_timedlock(rtdm_mutex_t *mutex, __s64 timeout);
void rtdm_mutex_unlock(rtdm_mutex_t *mutex);


/* --- utility functions --- */

#define rtdm_printk(format, ...)    printk(format, ##__VA_ARGS__)

#define rtdm_malloc(size)           xnmalloc(size)
#define rtdm_free(ptr)              xnfree(ptr)

#define rtdm_read_user_ok(user_info, ptr, size)         \
    __xn_access_ok(user_info, VERIFY_READ, ptr, size)

#define rtdm_rw_user_ok(user_info, ptr, size)           \
    __xn_access_ok(user_info, VERIFY_WRITE, ptr, size)

#define rtdm_copy_from_user(user_info, dst, src, size)  \
({                                                      \
    __xn_copy_from_user(user_info, dst, src, size);     \
    0;                                                  \
})

#define rtdm_copy_to_user(user_info, dst, src, size)    \
({                                                      \
    __xn_copy_to_user(user_info, dst, src, size);       \
    0;                                                  \
})

static inline int rtdm_strncpy_from_user(rtdm_user_info_t *user_info,
                                         char *dst,
                                         const char __user *src,
                                         size_t count)
{
    int res = -EFAULT;

    if (likely(__xn_access_ok(user_info, VERIFY_READ, src, 1))) {
        res = 0;
        __xn_strncpy_from_user(user_info, dst, src, count);
    }
    return res;
}

#define rtdm_in_rt_context()        (adp_current != adp_root)

extern int rtdm_exec_in_rt(struct rtdm_dev_context *context,
                           rtdm_user_info_t *user_info,
                           void *arg,
                           rtdm_rt_handler_t handler);

#endif /* __KERNEL__ */

#endif /* _RTDM_DRIVER_H */
