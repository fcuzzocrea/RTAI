/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
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

#include <stdarg.h>

#include <nucleus/queue.h>

#include <posix/registry.h>
#include <posix/internal.h>     /* Magics, time conversion */
#include <posix/thread.h>       /* errno. */

/* Temporary definitions. */
struct pse51_mq {
    pse51_node_t nodebase;

#define node2mq(naddr) \
    ((pse51_mq_t *)((char *)(naddr) - (int)(&((pse51_mq_t *)0)->nodebase)))

    unsigned long flags;

    xnpqueue_t queued;
    xnsynch_t synchbase;
    size_t memsize;
    char *mem;
    xnqueue_t avail;

#define synch2mq(saddr) \
    ((pse51_mq_t *)((char *)(saddr) - (int)(&((pse51_mq_t *)0)->synchbase)))

    struct mq_attr attr;

    xnholder_t link;            /* link in mqq */
};

typedef struct pse51_mq pse51_mq_t;

typedef struct pse51_msg {
    xnpholder_t holder;
    size_t len;

#define link2msg(laddr) \
    ((pse51_msg_t *)((char *)(laddr) - (int)(&((pse51_msg_t *)0)->holder)))
    char data[0];
} pse51_msg_t;

typedef struct pse51_direct_msg {
    char *buf;
    size_t *lenp;
    unsigned *priop;
    int used;
} pse51_direct_msg_t;

xnqueue_t pse51_mqq;

pse51_msg_t *pse51_mq_msg_alloc(pse51_mq_t *mq)
{
    xnpholder_t *holder = (xnpholder_t *) getq(&mq->avail);

    if(!holder)
        return NULL;

    initph(holder);
    return link2msg(holder);
}

void pse51_mq_msg_free(pse51_mq_t *mq, pse51_msg_t *msg)
{
    xnholder_t *holder = (xnholder_t *) (&msg->holder);
    inith(holder);
    prependq(&mq->avail, holder); /* For earliest re-use of the block. */
}

int pse51_mq_init(pse51_mq_t *mq, const struct mq_attr *attr)
{
    unsigned i, msgsize, memsize;
    char *mem;

    if(!attr->mq_maxmsg)
        return EINVAL;
    
    msgsize = attr->mq_msgsize + sizeof(pse51_msg_t);

    /* Align msgsize on natural boundary. */
    if ((msgsize % sizeof(unsigned long)))
        msgsize += sizeof(unsigned long) - (msgsize % sizeof(unsigned long));

    memsize = msgsize * attr->mq_maxmsg;
    memsize = PAGE_ALIGN(memsize);

    mem = xnarch_sysalloc(memsize);

    if (!mem)
        return ENOSPC;

    mq->flags = 0;
    mq->memsize = memsize;
    initpq(&mq->queued, xnqueue_down, 0);
    xnsynch_init(&mq->synchbase, XNSYNCH_PRIO | XNSYNCH_NOPIP);
    mq->mem = mem;

    /* Fill the pool. */
    initq(&mq->avail);
    for (i = 0; i < attr->mq_maxmsg; i++)
        {
        pse51_msg_t *msg = (pse51_msg_t *) (mem + i * msgsize);
        pse51_mq_msg_free(mq, msg);
        }

    mq->attr = *attr;

    return 0;
}

static void pse51_mq_destroy(pse51_mq_t *mq)
{
    int need_resched;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);
    need_resched = (xnsynch_destroy(&mq->synchbase) == XNSYNCH_RESCHED);
    removeq(&pse51_mqq, &mq->link);
    xnlock_put_irqrestore(&nklock, s);
    xnarch_sysfree(mq->mem, mq->memsize);

    if (need_resched)
        xnpod_schedule();
}

int mq_getattr(mqd_t fd, struct mq_attr *attr)
{
    pse51_desc_t *desc;
    pse51_mq_t *mq;
    spl_t s;
    int err;

    xnlock_get_irqsave(&nklock, s);

    err = pse51_desc_get(&desc, fd, PSE51_MQ_MAGIC);

    if(err)
        {
        xnlock_put_irqrestore(&nklock, s);
        thread_set_errno(err);
        return -1;
        }
    
    mq = node2mq(pse51_desc_node(desc));
    *attr = mq->attr;
    attr->mq_flags = pse51_desc_getflags(desc);
    attr->mq_curmsgs = countpq(&mq->queued);
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int mq_setattr(mqd_t fd,
               const struct mq_attr *__restrict__ attr,
               struct mq_attr *__restrict__ oattr)
{
    pse51_desc_t *desc;
    pse51_mq_t *mq;
    long flags;
    spl_t s;
    int err;

    xnlock_get_irqsave(&nklock, s);

    err = pse51_desc_get(&desc, fd, PSE51_MQ_MAGIC);

    if(err)
        {
        xnlock_put_irqrestore(&nklock, s);
        thread_set_errno(err);
        return -1;
        }

    mq = node2mq(pse51_desc_node(desc));
    if(oattr)
        oattr->mq_flags = pse51_desc_getflags(desc);
    flags = (pse51_desc_getflags(desc) & PSE51_PERMS_MASK)
        | (attr->mq_flags & ~PSE51_PERMS_MASK);
    pse51_desc_setflags(desc, flags);
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

static int pse51_mq_trysend(pse51_desc_t *desc,
                            const char *buffer,
                            size_t len,
                            unsigned prio)
{
    pthread_t reader;
    pse51_mq_t *mq;
    unsigned flags;

    mq = node2mq(pse51_desc_node(desc));
    flags = pse51_desc_getflags(desc) & PSE51_PERMS_MASK;

    if(flags != O_WRONLY && flags != O_RDWR)
        return EPERM;

    if(len > mq->attr.mq_msgsize)
        return EMSGSIZE;

    /* There may be a reader pending on the queue only if no message is already
       queued. Otherwise, any pending thread is a writer. */
    if(!countpq(&mq->queued))
        reader = thread2pthread(xnsynch_wakeup_one_sleeper(&mq->synchbase));
    else
        reader = NULL;

    if(reader && reader->arg)
        {
        pse51_direct_msg_t *msg = (pse51_direct_msg_t *) reader->arg;

        memcpy(msg->buf, buffer, len);
        *(msg->lenp) = len;
        if(msg->priop)
            *(msg->priop) = prio;
        msg->used = 1;
        }
    else
        {
        pse51_msg_t *msg = pse51_mq_msg_alloc(mq);

        if(!msg)
            return EAGAIN;

        memcpy(&msg->data[0], buffer, len);
        msg->len = len;
        insertpqf(&mq->queued, &msg->holder, prio);
        }

    if(reader)
        xnpod_schedule();

    return 0;
}

static int pse51_mq_tryrcv(pse51_desc_t *desc,
                           char *__restrict__ buffer,
                           size_t *__restrict__ lenp,
                           unsigned *__restrict__ priop)
{
    xnpholder_t *holder;
    pse51_msg_t *msg;
    pse51_mq_t *mq;
    unsigned flags;

    mq = node2mq(pse51_desc_node(desc));
    flags = pse51_desc_getflags(desc) & PSE51_PERMS_MASK;

    if(flags != O_RDONLY && flags != O_RDWR)
        return EPERM;

    if(*lenp < mq->attr.mq_msgsize)
        return EMSGSIZE;

    if(!(holder = getpq(&mq->queued)))
        return EAGAIN;

    msg = link2msg(holder);
    if(priop)
        *priop = holder->prio;
    *lenp = msg->len;
    memcpy(buffer, &msg->data[0], msg->len);

    pse51_mq_msg_free(mq, msg);

    if(xnsynch_wakeup_one_sleeper(&mq->synchbase))
        xnpod_schedule();
    
    return 0;
}


static int pse51_mq_timedsend_inner(mqd_t fd,
                                    const char * buffer,
                                    size_t len,
                                    unsigned prio,
                                    xnticks_t abs_to)
{
    int rc;

    for (;;) {
        xnticks_t to = abs_to;
        pse51_desc_t *desc;
        pse51_mq_t *mq;
        pthread_t cur;
        
        if ((rc = pse51_desc_get(&desc, fd, PSE51_MQ_MAGIC)))
            break;

        if ((rc = pse51_mq_trysend(desc, buffer, len, prio)) != EAGAIN)
            break;

        if ((pse51_desc_getflags(desc) & O_NONBLOCK))
            break;

        if ((rc = clock_adjust_timeout(&to, CLOCK_REALTIME)))
            break;

        mq = node2mq(pse51_desc_node(desc));

        xnsynch_sleep_on(&mq->synchbase, to);

        cur = pse51_current_thread();

        thread_cancellation_point(cur);

        if (xnthread_test_flags(&cur->threadbase, XNBREAK))
            return EINTR;

        if (xnthread_test_flags(&cur->threadbase, XNTIMEO))
            return ETIMEDOUT;

        if (xnthread_test_flags(&cur->threadbase, XNRMID))
            return EBADF;
        }

    return rc;
}

static int pse51_mq_timedrcv_inner(mqd_t fd,
                                   char *__restrict__ buffer,
                                   size_t *__restrict__ lenp,
                                   unsigned *__restrict__ priop,
                                   xnticks_t abs_to)
{
    pthread_t cur = pse51_current_thread();
    int rc;

    for (;;)
        {
        pse51_direct_msg_t msg;
        xnticks_t to = abs_to;
        pse51_desc_t *desc;
        pse51_mq_t *mq;
        int direct = 0;
        
        if ((rc = pse51_desc_get(&desc, fd, PSE51_MQ_MAGIC)))
            break;

        if ((rc = pse51_mq_tryrcv(desc, buffer, lenp, priop)) != EAGAIN)
            break;

        if ((pse51_desc_getflags(desc) & O_NONBLOCK))
            break;

        mq = node2mq(pse51_desc_node(desc));

        if(testbits(pse51_desc_getflags(desc), O_DIRECT))
            {
            msg.buf = buffer;
            msg.lenp = lenp;
            msg.priop = priop;
            msg.used = 0;
            cur->arg = &msg;
            direct = 1;
            }
        else
            cur->arg = NULL;

        if((rc = clock_adjust_timeout(&to, CLOCK_REALTIME)))
            break;

        xnsynch_sleep_on(&mq->synchbase, to);

        thread_cancellation_point(cur);

        if (direct & msg.used)
            return 0;
            
        if (xnthread_test_flags(&cur->threadbase, XNRMID))
            return EBADF;

        if (xnthread_test_flags(&cur->threadbase, XNTIMEO))
            return ETIMEDOUT;

        if (xnthread_test_flags(&cur->threadbase, XNBREAK))
            return EINTR;
        }

    return rc;
}

int mq_timedsend(mqd_t fd,
                 const char * buffer,
                 size_t len,
                 unsigned prio,
                 const struct timespec *abs_timeout)
{
    xnticks_t timeout;
    int err;
    spl_t s;

    if((unsigned) abs_timeout->tv_nsec > ONE_BILLION)
        {
        thread_set_errno(EINVAL);
        return -1;
        }

    timeout = ts2ticks_ceil(abs_timeout) + 1;

    xnlock_get_irqsave(&nklock, s);
    err = pse51_mq_timedsend_inner(fd, buffer, len, prio, timeout);
    xnlock_put_irqrestore(&nklock, s);

    if(err)
        {
        thread_set_errno(err);
        return -1;
        }

    return 0;
}

int mq_send(mqd_t fd, const char *buffer, size_t len, unsigned prio)
{
    int err;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);
    err = pse51_mq_timedsend_inner(fd, buffer, len, prio, XN_INFINITE);
    xnlock_put_irqrestore(&nklock, s);

    if(err)
        {
        thread_set_errno(err);
        return -1;
        }

    return 0;
}

ssize_t mq_timedreceive(mqd_t fd,
                        char *__restrict__ buffer,
                        size_t len,
                        unsigned *__restrict__ priop,
                        const struct timespec *__restrict__ abs_timeout)
{
    xnticks_t timeout;
    int err;
    spl_t s;

    if((unsigned) abs_timeout->tv_nsec > ONE_BILLION)
        {
        thread_set_errno(EINVAL);
        return -1;
        }

    timeout = ts2ticks_ceil(abs_timeout) + 1;

    xnlock_get_irqsave(&nklock, s);
    err = pse51_mq_timedrcv_inner(fd, buffer, &len, priop, timeout);
    xnlock_put_irqrestore(&nklock, s);

    if(err)
        {
        thread_set_errno(err);
        return -1;
        }

    return len;    
}

ssize_t mq_receive(mqd_t fd, char *buffer, size_t len, unsigned *priop)
{
    int err;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);
    err = pse51_mq_timedrcv_inner(fd, buffer, &len, priop, XN_INFINITE);
    xnlock_put_irqrestore(&nklock, s);

    if(err)
        {
        thread_set_errno(err);
        return -1;
        }

    return len;    
}


mqd_t mq_open(const char *name, int oflags, ...)
{
    struct mq_attr *attr;
    xnsynch_t done_synch;
    pse51_node_t *node;
    pse51_desc_t *desc;
    pse51_mq_t *mq;
    mode_t mode;
    va_list ap;
    int err;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    err = pse51_node_get(&node, name, PSE51_MQ_MAGIC, oflags);

    if(err)
        goto error;

    if(node)
        {
        mq = node2mq(node);
        goto got_mq;
        }

    /* Here, we know that we must create a message queue. */
    mq = xnmalloc(sizeof(*mq));
    
    if(!mq)
        {
        err = ENOMEM;
        goto error;
        }

    err = pse51_node_add_start(&mq->nodebase, name, PSE51_MQ_MAGIC, &done_synch);
    if(err)
        goto error;
    xnlock_put_irqrestore(&nklock, s);

    /* Release the global lock while creating the message queue. */
    va_start(ap, oflags);
    mode = va_arg(ap, int); /* unused */
    attr = va_arg(ap, struct mq_attr *);
    va_end(ap);    

    err = pse51_mq_init(mq, attr);

    xnlock_get_irqsave(&nklock, s);
    pse51_node_add_finished(&mq->nodebase, err);
    if(err)
        {
        xnlock_put_irqrestore(&nklock, s);
        goto err_free_mq;
        }

    inith(&mq->link);
    appendq(&pse51_mqq, &mq->link);
    
    /* Whether found or created, here we have a valid message queue. */
  got_mq:
    err = pse51_desc_create(&desc, &mq->nodebase);

    if(err)
        goto err_put_mq;

    pse51_desc_setflags(desc, oflags & (O_NONBLOCK | PSE51_PERMS_MASK));

    xnlock_put_irqrestore(&nklock, s);

    /* FIXME: need a cancellation point in the case we blocked and were resumed
     * with XNBREAK set. */
    
    return (mqd_t) pse51_desc_fd(desc);


  err_put_mq:
    pse51_node_put(&mq->nodebase);

    if(pse51_node_removed_p(&mq->nodebase))
        {
        /* mq is no longer referenced, we may destroy it. */
        xnlock_put_irqrestore(&nklock, s);

        pse51_mq_destroy(mq);
  err_free_mq:
        xnfree(mq);
        }
    else
  error:
        xnlock_put_irqrestore(&nklock, s);

    thread_set_errno(err);

    return (mqd_t) -1;
}

int mq_close(mqd_t fd)
{
    pse51_desc_t *desc;
    pse51_mq_t *mq;
    spl_t s;
    int err;

    xnlock_get_irqsave(&nklock, s);

    err = pse51_desc_get(&desc, fd, PSE51_MQ_MAGIC);

    if(err)
        goto error;

    mq = node2mq(pse51_desc_node(desc));
    
    err = pse51_desc_destroy(desc);

    if(err)
        goto error;

    err = pse51_node_put(&mq->nodebase);

    if(err)
        goto error;
    
    if(pse51_node_removed_p(&mq->nodebase))
        {
        xnlock_put_irqrestore(&nklock, s);

        pse51_mq_destroy(mq);
        xnfree(mq);
        }
    else
        xnlock_put_irqrestore(&nklock, s);

    return 0;

  error:
    xnlock_put_irqrestore(&nklock, s);
    thread_set_errno(err);
    return -1;
}

int mq_unlink(const char *name)
{
    pse51_node_t *node;
    int err, removed;
    pse51_mq_t *mq;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    err = pse51_node_remove(&node, name, PSE51_MQ_MAGIC);

    if(!err && pse51_node_removed_p(&mq->nodebase))
        {
        xnlock_put_irqrestore(&nklock, s);

        pse51_mq_destroy(mq);
        xnfree(mq);
        }
    else
        xnlock_put_irqrestore(&nklock, s);

    if(err)
        {
        thread_set_errno(err);
        return -1;
        }

    mq = node2mq(node);
        
    return 0;
}

int pse51_mq_pkg_init(void)
{
    initq(&pse51_mqq);
    return 0;
}

EXPORT_SYMBOL(mq_open);
EXPORT_SYMBOL(mq_getattr);
EXPORT_SYMBOL(mq_setattr);
EXPORT_SYMBOL(mq_send);
EXPORT_SYMBOL(mq_timedsend);
EXPORT_SYMBOL(mq_receive);
EXPORT_SYMBOL(mq_timedreceive);
EXPORT_SYMBOL(mq_close);
EXPORT_SYMBOL(mq_unlink);
