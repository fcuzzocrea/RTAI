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

#include <posix/internal.h>     /* Magics, time conversion */
#include <posix/thread.h>       /* errno. */

#define PSE51_MQ_UNLINKED 1
#define PSE51_PERMS_MASK  (O_RDONLY | O_WRONLY | O_RDWR)

/* Temporary definitions. */
struct pse51_mq {
    unsigned magic;
    unsigned long flags;

    xnpqueue_t queued;
    xnsynch_t synchbase;
    size_t memsize;
    char *mem;
    xnqueue_t avail;

#define synch2mq(saddr) \
    ((pse51_mq_t *)((char *)(saddr) - (int)(&((pse51_mq_t *)0)->synchbase)))
    
    unsigned refcount;          /* # Descriptors referencing this queue. */
    struct mq_attr attr;
};

struct pse51_mqd {
    unsigned magic;
    long flags;
    struct pse51_mq *mq;
};
typedef struct pse51_mqd pse51_mqd_t;

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


/* Handling named message queues. */
static struct {
    xnqueue_t buckets [211];
} pse51_nmq_hash;

typedef struct pse51_nmq {
    xnholder_t link;
    const char *name;
    pse51_mq_t mq;
#define mq2nmq(laddr) \
    ((pse51_nmq_t *)((char *)(laddr) - (int)(&((pse51_nmq_t *)0)->mq)))
} pse51_nmq_t;

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
    spl_t s;

    if(!attr->mq_maxmsg)
        return EINVAL;
    
    msgsize = attr->mq_msgsize + sizeof(pse51_msg_t);

    /* Align msgsize on natural boundary. */
    msgsize += sizeof(unsigned long) - (msgsize % sizeof(unsigned long));

    memsize = msgsize * attr->mq_maxmsg;
    memsize = PAGE_ALIGN(memsize);

    mem = xnarch_sysalloc(memsize);

    if(!mem)
        return ENOSPC;

    xnlock_get_irqsave(&nklock, s);
    mq->magic = PSE51_MQ_MAGIC;
    mq->flags = 0;
    mq->memsize = memsize;
    initpq(&mq->queued, xnqueue_down, 0);
    xnsynch_init(&mq->synchbase, XNSYNCH_PRIO | XNSYNCH_NOPIP);
    mq->mem = mem;

    /* Fill the pool. */
    initq(&mq->avail);
    for(i = 0; i < attr->mq_maxmsg; i++)
        {
        pse51_msg_t *msg = (pse51_msg_t *) (mem + i * msgsize);
        pse51_mq_msg_free(mq, msg);
        }

    mq->refcount = 0;
    mq->attr = *attr;
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pse51_mq_destroy(pse51_mq_t *mq)
{
    spl_t s;
    int rc;

    xnlock_get_irqsave(&nklock, s);
    if(mq->refcount)
        {
        setbits(mq->flags, PSE51_MQ_UNLINKED);
        xnlock_put_irqrestore(&nklock, s);
        return -1;
        }

    pse51_mark_deleted(mq);
    rc = (xnsynch_destroy(&mq->synchbase) == XNSYNCH_RESCHED);
    xnlock_put_irqrestore(&nklock, s);
    xnarch_sysfree(mq->mem, mq->memsize);

    return rc;
}

static int pse51_mqd_bind(pse51_mqd_t *qd, pse51_mq_t *mq, long flags)
{
    spl_t s;

    xnlock_get_irqsave(&nklock, s);
    if(!pse51_obj_active(mq, PSE51_MQ_MAGIC, pse51_mq_t))
        {
        xnlock_put_irqrestore(&nklock, s);
        return EINVAL;
        }

    qd->magic = PSE51_MQD_MAGIC;
    qd->mq = mq;
    ++mq->refcount;
    qd->flags = flags;
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int mq_close(mqd_t fd)
{
    pse51_mqd_t *qd = (pse51_mqd_t *) fd;
    pse51_mq_t *mq;
    spl_t s;

    /* In case qd is the result of a call to mq_open which failed. */
    if(qd == (pse51_mqd_t *) -1)
        {
        thread_set_errno(EBADF);
        return -1;
        }

    xnlock_get_irqsave(&nklock, s);
    if(!pse51_obj_active(qd, PSE51_MQD_MAGIC, struct pse51_mqd))
        {
        xnlock_put_irqrestore(&nklock, s);
        thread_set_errno(EBADF);
        return -1;
        }

    mq = qd->mq;
    pse51_mark_deleted(qd);
    if(!--mq->refcount && testbits(mq->flags, PSE51_MQ_UNLINKED))
        {
        int need_resched;
        xnlock_put_irqrestore(&nklock, s);

        need_resched = pse51_mq_destroy(qd->mq);
        xnfree(mq2nmq(mq));
        if(need_resched)
            xnpod_schedule();
        }
    else
        xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int mq_getattr(mqd_t fd, struct mq_attr *attr)
{
    pse51_mqd_t *qd = (pse51_mqd_t *) fd;
    spl_t s;

    /* In case qd is the result of a call to mq_open which failed. */
    if(qd == (pse51_mqd_t *) -1)
        {
        thread_set_errno(EBADF);
        return -1;
        }

    xnlock_get_irqsave(&nklock, s);
    if(!pse51_obj_active(qd, PSE51_MQD_MAGIC, struct pse51_mqd))
        {
        xnlock_put_irqrestore(&nklock, s);
        thread_set_errno(EBADF);
        return -1;
        }

    *attr = qd->mq->attr;
    attr->mq_flags = qd->flags;
    attr->mq_curmsgs = countpq(&qd->mq->queued);
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int mq_setattr(mqd_t fd,
               const struct mq_attr *__restrict__ attr,
               struct mq_attr *__restrict__ oattr)
{
    pse51_mqd_t *qd = (pse51_mqd_t *) fd;
    spl_t s;

    /* In case qd is the result of a call to mq_open which failed. */
    if(qd == (pse51_mqd_t *) -1)
        {
        thread_set_errno(EBADF);
        return -1;
        }

    xnlock_get_irqsave(&nklock, s);
    if(!pse51_obj_active(qd, PSE51_MQD_MAGIC, struct pse51_mqd))
        {
        xnlock_put_irqrestore(&nklock, s);
        thread_set_errno(EBADF);
        return -1;
        }

    if(oattr)
        oattr->mq_flags = qd->flags;
    qd->flags =
        (qd->flags & PSE51_PERMS_MASK) | (attr->mq_flags & ~PSE51_PERMS_MASK);
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

static int pse51_mq_trysend(pse51_mqd_t *qd,
                            const char *buffer,
                            size_t len,
                            unsigned prio)
{
    pthread_t reader;
    pse51_mq_t *mq;
    unsigned perm;

    if(!pse51_obj_active(qd, PSE51_MQD_MAGIC, struct pse51_mqd))
        return EBADF;

    perm = qd->flags & PSE51_PERMS_MASK;
    if(perm != O_WRONLY && perm!= O_RDWR)
        return EPERM;

    mq = qd->mq;
    
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
        pse51_msg_t *msg = pse51_mq_msg_alloc(qd->mq);

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

static int pse51_mq_tryrcv(pse51_mqd_t *qd,
                           char *__restrict__ buffer,
                           size_t *__restrict__ lenp,
                           unsigned *__restrict__ priop)
{
    xnpholder_t *holder;
    pse51_msg_t *msg;
    pse51_mq_t *mq;
    unsigned perm;

    if(!pse51_obj_active(qd, PSE51_MQD_MAGIC, struct pse51_mqd))
        return EBADF;

    perm = qd->flags & PSE51_PERMS_MASK;
    if(perm != O_RDONLY && perm != O_RDWR)
        return EPERM;

    mq = qd->mq;
    
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


static int pse51_mq_timedsend_inner(pse51_mqd_t *qd,
                                    const char * buffer,
                                    size_t len,
                                    unsigned prio,
                                    xnticks_t abs_to)
{
    int rc;
    
    while((rc = pse51_mq_trysend(qd, buffer, len, prio)) == EAGAIN
          && !testbits(qd->flags, O_NONBLOCK))
        {
        xnticks_t to = abs_to;
        pthread_t cur;

        rc = clock_adjust_timeout(&to, CLOCK_REALTIME);

        if (rc)
            return rc;

        xnsynch_sleep_on(&qd->mq->synchbase, to);

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

static int pse51_mq_timedrcv_inner(pse51_mqd_t *qd,
                                   char *__restrict__ buffer,
                                   size_t *__restrict__ lenp,
                                   unsigned *__restrict__ priop,
                                   xnticks_t abs_to)
{
    pthread_t cur;
    int rc;
    
    cur = pse51_current_thread();

    while((rc = pse51_mq_tryrcv(qd, buffer, lenp, priop)) == EAGAIN
          && !testbits(qd->flags, O_NONBLOCK))
        {
        pse51_mq_t *mq = qd->mq;
        pse51_direct_msg_t msg;
        xnticks_t to = abs_to;
        int direct = 0;

        if(testbits(qd->flags, O_DIRECT))
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

        rc = clock_adjust_timeout(&to, CLOCK_REALTIME);

        if (rc)
            return rc;

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
    pse51_mqd_t *qd = (pse51_mqd_t *) fd;
    xnticks_t timeout;
    int err;
    spl_t s;

    if(abs_timeout->tv_nsec < 0 || abs_timeout->tv_nsec > ONE_BILLION)
        {
        thread_set_errno(EINVAL);
        return -1;
        }

    /* In case qd is the result of a call to mq_open which failed. */
    if(qd == (pse51_mqd_t *) -1)
        {
        thread_set_errno(EBADF);
        return -1;
        }

    timeout = ts2ticks_ceil(abs_timeout) + 1;

    xnlock_get_irqsave(&nklock, s);
    err = pse51_mq_timedsend_inner(qd, buffer, len, prio, timeout);
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
    pse51_mqd_t *qd = (pse51_mqd_t *) fd;
    int err;
    spl_t s;

    /* In case qd is the result of a call to mq_open which failed. */
    if(qd == (pse51_mqd_t *) -1)
        {
        thread_set_errno(EBADF);
        return -1;
        }

    xnlock_get_irqsave(&nklock, s);
    err = pse51_mq_timedsend_inner(qd, buffer, len, prio, XN_INFINITE);
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
    pse51_mqd_t *qd = (pse51_mqd_t *) fd;
    xnticks_t timeout;
    int err;
    spl_t s;

    if(abs_timeout->tv_nsec < 0 || abs_timeout->tv_nsec > ONE_BILLION)
        {
        thread_set_errno(EINVAL);
        return -1;
        }

    /* In case qd is the result of a call to mq_open which failed. */
    if(qd == (pse51_mqd_t *) -1)
        {
        thread_set_errno(EBADF);
        return -1;
        }

    timeout = ts2ticks_ceil(abs_timeout) + 1;

    xnlock_get_irqsave(&nklock, s);
    err = pse51_mq_timedrcv_inner(qd, buffer, &len, priop, timeout);
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
    pse51_mqd_t *qd = (pse51_mqd_t *) fd;
    int err;
    spl_t s;

    /* In case qd is the result of a call to mq_open which failed. */
    if(qd == (pse51_mqd_t *) -1)
        {
        thread_set_errno(EBADF);
        return -1;
        }

    xnlock_get_irqsave(&nklock, s);
    err = pse51_mq_timedrcv_inner(qd, buffer, &len, priop, XN_INFINITE);
    xnlock_put_irqrestore(&nklock, s);

    if(err)
        {
        thread_set_errno(err);
        return -1;
        }

    return len;    
}


int pse51_mq_pkg_init(void)
{
    unsigned i;

    for(i = 0; i < sizeof(pse51_nmq_hash.buckets)/sizeof(xnqueue_t); i++)
        initq(&pse51_nmq_hash.buckets[i]);

    return 0;
}

static unsigned pse51_nmq_crunch (const char *key)
{
    unsigned h = 0, g;

#define HQON    24              /* Higher byte position */
#define HBYTE   0xf0000000      /* Higher nibble on */

    while (*key)
        {
        h = (h << 4) + *key++;
        if ((g = (h & HBYTE)) != 0)
            h = (h ^ (g >> HQON)) ^ g;
        }

    return h % (sizeof(pse51_nmq_hash.buckets)/sizeof(xnqueue_t));
}

static xnholder_t *pse51_mq_search_inner(xnqueue_t **bucketp, const char *name)
{
    xnholder_t *holder;
    xnqueue_t *queue;

    queue = &pse51_nmq_hash.buckets[pse51_nmq_crunch(name)];

    for(holder = getheadq(queue); holder; holder = nextq(queue, holder))
        {
        pse51_nmq_t *nmq = (pse51_nmq_t *) holder;
        if(!strcmp(nmq->name, name))
            break;
        }

    *bucketp = queue;
    return holder;
}

mqd_t mq_open(const char *name, int oflags, ...)
{
    pse51_nmq_t *nmq, *useless_nmq = NULL;
    xnholder_t *holder;
    xnqueue_t *bucket;
    pse51_mqd_t *qd;
    int err = 0;
    spl_t s;

    qd = xnmalloc(sizeof(*qd));

    if(!qd)
        {
        err = ENOSPC;
        goto out_err;
        }

    xnlock_get_irqsave(&nklock, s);
    holder = pse51_mq_search_inner(&bucket, name);
    nmq = (pse51_nmq_t *) holder;

    if(holder)
        {
        if((oflags & (O_EXCL | O_CREAT)) == (O_EXCL | O_CREAT))
            {
            err = EEXIST;
            goto out_err_unlock_free_qd;
            }
        else
            goto nmq_found;
        }

    if(!(oflags & O_CREAT))
        {
        err = ENOENT;
out_err_unlock_free_qd:
        xnlock_put_irqrestore(&nklock, s);
        goto out_err_free_qd;
        }

    /* We will have to create the queue, and since it involves allocating system
       memory, release the global lock. */
    xnlock_put_irqrestore(&nklock, s);

    {
    struct mq_attr *attr;
    mode_t mode;
    va_list ap;
    
    nmq = (pse51_nmq_t *) xnmalloc(sizeof(*nmq) + strlen(name)+1);

    if(!nmq)
        {
        err = ENOSPC;
        goto out_err_free_qd;
        }

    nmq->name = (char *) (nmq + 1);
    strcpy((char *) nmq->name, name);
    inith(&nmq->link);

    va_start(ap, oflags);
    mode = va_arg(ap, int); /* unused */
    attr = va_arg(ap, struct mq_attr *);
    va_end(ap);
    
    err = pse51_mq_init(&nmq->mq, attr);

    if(err)
        {
        xnfree(nmq);
        goto out_err_free_qd;
        }
    }

    xnlock_get_irqsave(&nklock, s);
    holder = pse51_mq_search_inner(&bucket, name);

    if(holder)
        {
        useless_nmq = nmq;
        if((oflags & (O_EXCL | O_CREAT)) == (O_EXCL | O_CREAT))
            {
            err = EEXIST;
            goto out_err_free_nmq;
            }
        nmq = (pse51_nmq_t *) holder;
        }

    appendq(bucket, &nmq->link);

  nmq_found:
    pse51_mqd_bind(qd, &nmq->mq, oflags & ~(O_CREAT | O_EXCL));

    xnlock_put_irqrestore(&nklock, s);

    /* rollback allocation of nmq. */
    if(useless_nmq)
        {
        pse51_mq_destroy(&useless_nmq->mq);
        xnfree(useless_nmq);
        }

    return (mqd_t) qd;

  out_err_free_nmq:
    xnlock_put_irqrestore(&nklock, s);
    pse51_mq_destroy(&useless_nmq->mq);
    xnfree(useless_nmq);

  out_err_free_qd:
    xnfree(qd);

  out_err:  
    thread_set_errno(err);
    return (mqd_t) -1;
}

int mq_unlink(const char *name)
{
    xnqueue_t *bucket;
    xnholder_t *holder;
    pse51_nmq_t *nmq;
    int dest_status;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);
    holder = pse51_mq_search_inner(&bucket, name);

    if(holder)
        removeq(bucket, holder);
    xnlock_put_irqrestore(&nklock, s);

    if(!holder)
        {
        thread_set_errno(ENOENT);
        return -1;
        }

    nmq = (pse51_nmq_t *) holder;

    dest_status = pse51_mq_destroy(&nmq->mq);

    if(dest_status != -1)
        {
        xnfree(nmq);
        if(dest_status)
            xnpod_schedule();
        }

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
