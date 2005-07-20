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

#include <linux/module.h>

#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <rtdm/rtdm_driver.h>
#include <rtdm/core.h>
#include <rtdm/device.h>


unsigned int                fd_count = DEF_FILDES_COUNT;
module_param(fd_count, uint, 0400);
MODULE_PARM_DESC(fd_count, "Maximum number of file descriptors");

struct rtdm_fildes          *fildes_table;  /* allocated on init */
static struct rtdm_fildes   *free_fildes;   /* chain of free descriptors */
int                         open_fildes;    /* number of used descriptors */

#ifdef CONFIG_SMP
xnlock_t                    rt_fildes_lock = XNARCH_LOCK_UNLOCKED;
#endif /* !CONFIG_SMP */


static inline int get_fd(struct rtdm_fildes *fildes)
{
    return (fildes - fildes_table);
}


struct rtdm_dev_context *rtdm_context_get(int fd)
{
    struct rtdm_fildes      *fildes;
    struct rtdm_dev_context *context;
    spl_t                   s;


    if (fd >= fd_count)
        return NULL;

    fildes = &fildes_table[fd];

    xnlock_get_irqsave(&rt_fildes_lock, s);

    context = (struct rtdm_dev_context *)fildes->context;
    if (unlikely(!context ||
                 test_bit(RTDM_CLOSING, &context->context_flags))) {
        xnlock_put_irqrestore(&rt_fildes_lock, s);
        return NULL;
    }

    rtdm_context_lock(context);

    xnlock_put_irqrestore(&rt_fildes_lock, s);

    return context;
}


static int create_instance(struct rtdm_device *device,
                           struct rtdm_dev_context **context_ptr,
                           struct rtdm_fildes **fildes_ptr,
                           int nrt_mem)
{
    struct rtdm_fildes      *fildes;
    struct rtdm_dev_context *context;
    spl_t                   s;


    xnlock_get_irqsave(&rt_fildes_lock, s);

    *fildes_ptr = fildes = free_fildes;
    if (!fildes) {
        xnlock_put_irqrestore(&rt_fildes_lock, s);

        *context_ptr = NULL;
        return -ENFILE;
    }
    free_fildes = fildes->next;
    open_fildes++;

    xnlock_put_irqrestore(&rt_fildes_lock, s);

    *context_ptr = context = device->reserved.exclusive_context;
    if (context) {
        xnlock_get_irqsave(&rt_dev_lock, s);

        if (context->device) {
            xnlock_put_irqrestore(&rt_dev_lock, s);
            return -EBUSY;
        }
        context->device = device;

        xnlock_put_irqrestore(&rt_dev_lock, s);
    } else {
        if (nrt_mem)
            context = kmalloc(sizeof(struct rtdm_dev_context) +
                              device->context_size, GFP_KERNEL);
        else
            context = xnmalloc(sizeof(struct rtdm_dev_context) +
                               device->context_size);
        *context_ptr = context;
        if (!context)
            return -ENOMEM;

        context->device = device;
    }

    context->fd  = get_fd(fildes);
    context->ops = &device->ops;
    atomic_set(&context->close_lock_count, 0);

    return 0;
}


/* call with rt_fildes_lock acquired - will release it */
static void cleanup_instance(struct rtdm_device *device,
                             struct rtdm_dev_context *context,
                             struct rtdm_fildes *fildes,
                             int nrt_mem,
                             spl_t *s)
{
    if (fildes) {
        fildes->next = free_fildes;
        free_fildes = fildes;
        open_fildes--;

        fildes->context = NULL;
    }

    xnlock_put_irqrestore(&rt_fildes_lock, *s);

    if (context) {
        if (device->reserved.exclusive_context)
            context->device = NULL;
        else {
            if (nrt_mem)
                kfree(context);
            else
                xnfree(context);
        }
    }

    rtdm_dereference_device(device);
}


int _rtdm_open(rtdm_user_info_t *user_info, const char *path, int oflag)
{
    struct rtdm_device      *device;
    struct rtdm_fildes      *fildes;
    struct rtdm_dev_context *context;
    spl_t                   s;
    int                     ret;
    int                     nrt_mode = !rtdm_in_rt_context();


    device = get_named_device(path);
    ret = -ENODEV;
    if (!device)
        goto err_out;

    ret = create_instance(device, &context, &fildes, nrt_mode);
    if (ret != 0)
        goto cleanup_out;

    if (nrt_mode) {
        context->context_flags = (1 << RTDM_CREATED_IN_NRT);
        ret = device->open_nrt(context, user_info, oflag);
    } else {
        context->context_flags = 0;
        ret = device->open_rt(context, user_info, oflag);
    }

    if (unlikely(ret < 0))
        goto cleanup_out;

    fildes->context = context;

    return context->fd;


 cleanup_out:
    xnlock_get_irqsave(&rt_fildes_lock, s);
    cleanup_instance(device, context, fildes, nrt_mode, &s);

 err_out:
    return ret;
}


int _rtdm_socket(rtdm_user_info_t *user_info, int protocol_family,
                 int socket_type, int protocol)
{
    struct rtdm_device      *device;
    struct rtdm_fildes      *fildes;
    struct rtdm_dev_context *context;
    spl_t                   s;
    int                     ret;
    int                     nrt_mode = !rtdm_in_rt_context();


    device = get_protocol_device(protocol_family, socket_type);
    ret = -EAFNOSUPPORT;
    if (!device)
        goto err_out;

    ret = create_instance(device, &context, &fildes, nrt_mode);
    if (ret != 0)
        goto cleanup_out;

    if (nrt_mode) {
        context->context_flags = (1 << RTDM_CREATED_IN_NRT);
        ret = device->socket_nrt(context, user_info, protocol);
    } else {
        context->context_flags = 0;
        ret = device->socket_rt(context, user_info, protocol);
    }

    if (unlikely(ret < 0))
        goto cleanup_out;

    fildes->context = context;

    return context->fd;


 cleanup_out:
    xnlock_get_irqsave(&rt_fildes_lock, s);
    cleanup_instance(device, context, fildes, nrt_mode, &s);

 err_out:
    return ret;
}


int _rtdm_close(rtdm_user_info_t *user_info, int fd, int forced)
{
    struct rtdm_fildes      *fildes;
    struct rtdm_dev_context *context;
    spl_t                   s;
    int                     ret;


    ret = -EBADF;
    if (unlikely(fd >= fd_count))
        goto err_out;

    fildes = &fildes_table[fd];

    xnlock_get_irqsave(&rt_fildes_lock, s);

    context = (struct rtdm_dev_context *)fildes->context;

    if (unlikely(!context)) {
        xnlock_put_irqrestore(&rt_fildes_lock, s);
        goto err_out;   /* -EBADF */
    }

    set_bit(RTDM_CLOSING, &context->context_flags);
    if (forced)
        set_bit(RTDM_FORCED_CLOSING, &context->context_flags);
    rtdm_context_lock(context);

    xnlock_put_irqrestore(&rt_fildes_lock, s);

    if (rtdm_in_rt_context()) {
        ret = -ENOTSUPP;
        if (unlikely(test_bit(RTDM_CREATED_IN_NRT, &context->context_flags))) {
            xnprintf("RTDM: closing device in real-time mode while creation "
                     "ran in non-real-time - this is not supported!\n");
            goto unlock_out;
        }

        ret = context->ops->close_rt(context, user_info);

    } else
        ret = context->ops->close_nrt(context, user_info);

    if (unlikely(ret < 0))
        goto unlock_out;

    xnlock_get_irqsave(&rt_fildes_lock, s);

    if (unlikely((atomic_read(&context->close_lock_count) > 1) && !forced)) {
        xnlock_put_irqrestore(&rt_fildes_lock, s);
        ret = -EAGAIN;
        goto unlock_out;
    }
    fildes->context = NULL;

    cleanup_instance((struct rtdm_device *)context->device, context,
        fildes, test_bit(RTDM_CREATED_IN_NRT, &context->context_flags), &s);

    return ret;


  unlock_out:
    rtdm_context_unlock(context);

  err_out:
    return ret;
}


#define MAJOR_FUNCTION_WRAPPER(operation, args...)                          \
    struct rtdm_dev_context *context;                                       \
    struct rtdm_operations  *ops;                                           \
    int                     ret;                                            \
                                                                            \
                                                                            \
    context = rtdm_context_get(fd);                                         \
    ret = -EBADF;                                                           \
    if (unlikely(!context))                                                 \
        goto err_out;                                                       \
                                                                            \
    ops = context->ops;                                                     \
                                                                            \
    if (rtdm_in_rt_context())                                               \
        ret = ops->operation##_rt(context, user_info, args);                \
    else                                                                    \
        ret = ops->operation##_nrt(context, user_info, args);               \
                                                                            \
    rtdm_context_unlock(context);                                           \
                                                                            \
 err_out:                                                                   \
    return ret


int _rtdm_ioctl(rtdm_user_info_t *user_info, int fd, int request, ...)
{
    va_list args;
    void    *arg;


    va_start(args, request);
    arg = va_arg(args, void *);
    va_end(args);

    MAJOR_FUNCTION_WRAPPER(ioctl, request, arg);
}


int _rtdm_read(rtdm_user_info_t *user_info, int fd, void *buf, size_t nbyte)
{
    MAJOR_FUNCTION_WRAPPER(read, buf, nbyte);
}


int _rtdm_write(rtdm_user_info_t *user_info, int fd, const void *buf,
                size_t nbyte)
{
    MAJOR_FUNCTION_WRAPPER(write, buf, nbyte);
}


int _rtdm_recvmsg(rtdm_user_info_t *user_info, int fd, struct msghdr *msg,
                  int flags)
{
    MAJOR_FUNCTION_WRAPPER(recvmsg, msg, flags);
}


int _rtdm_sendmsg(rtdm_user_info_t *user_info, int fd,
                  const struct msghdr *msg, int flags)
{
    MAJOR_FUNCTION_WRAPPER(sendmsg, msg, flags);
}


int __init rtdm_core_init(void)
{
    int i;


    fildes_table = (struct rtdm_fildes *)
        kmalloc(fd_count * sizeof(struct rtdm_fildes), GFP_KERNEL);
    if (!fildes_table)
        return -ENOMEM;

    memset(fildes_table, 0, fd_count * sizeof(struct rtdm_fildes));
    for (i = 0; i < fd_count-1; i++)
        fildes_table[i].next = &fildes_table[i+1];
    free_fildes = &fildes_table[0];

    return 0;
}


EXPORT_SYMBOL(rtdm_context_get);
EXPORT_SYMBOL(_rtdm_open);
EXPORT_SYMBOL(_rtdm_socket);
EXPORT_SYMBOL(_rtdm_close);
EXPORT_SYMBOL(_rtdm_ioctl);
EXPORT_SYMBOL(_rtdm_read);
EXPORT_SYMBOL(_rtdm_write);
EXPORT_SYMBOL(_rtdm_recvmsg);
EXPORT_SYMBOL(_rtdm_sendmsg);
