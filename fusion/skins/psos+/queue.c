/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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
 *
 * As a special exception, the RTAI project gives permission
 * for additional uses of the text contained in its release of
 * Xenomai.
 *
 * The exception is that, if you link the Xenomai libraries with other
 * files to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the Xenomai libraries code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public
 * License.
 *
 * This exception applies only to the code released by the
 * RTAI project under the name Xenomai.  If you copy code from other
 * RTAI project releases into a copy of Xenomai, as the General Public
 * License permits, the exception does not apply to the code that you
 * add in this way.  To avoid misleading anyone as to the status of
 * such modified files, you must delete this exception notice from
 * them.
 *
 * If you write modifications of your own for Xenomai, it is your
 * choice whether to permit this exception to apply to your
 * modifications. If you do not wish that, delete this exception
 * notice.
 */

#include "psos+/task.h"
#include "psos+/queue.h"

static xnqueue_t psosqueueq;

static xnqueue_t psoschunkq; /* Shared chunks */

static xnqueue_t psosmbufq; /* Shared msg buffers (in chunks) */

static u_long q_destroy_internal(psosqueue_t *queue);

void psosqueue_init (void)

{
    initq(&psosqueueq);
    initq(&psoschunkq);
    initq(&psosmbufq);
}

void psosqueue_cleanup (void)

{
    xnholder_t *holder;

    while ((holder = getheadq(&psosqueueq)) != NULL)
	q_destroy_internal(link2psosqueue(holder));

    while ((holder = getq(&psoschunkq)) != NULL)
	xnfree(holder);
}

static u_long feed_pool (xnqueue_t *chunkq,
			 xnqueue_t *freeq,
			 u_long mbufcount,
			 u_long datalen)
{
    char *bstart, *bend;
    psosmbuf_t *mbuf;
    u_long bufsize;

    if (countq(freeq) >= mbufcount)
	return mbufcount;

    if (mbufcount < PSOS_QUEUE_MIN_ALLOC)
	mbufcount = PSOS_QUEUE_MIN_ALLOC; /* Minimum allocation */

    datalen = ((datalen + 3) & ~0x3);
    bufsize = sizeof(*mbuf) + datalen - sizeof(mbuf->data);

    if (bufsize < sizeof(*mbuf))
	bufsize = sizeof(*mbuf);

    /* A chunk starts with a holder */
    bstart = (char *)xnmalloc(sizeof(xnholder_t) + bufsize * mbufcount);

    if (!bstart)
	return 0;

    inith((xnholder_t *)bstart);
    appendq(chunkq,(xnholder_t *)bstart);
    bstart += sizeof(xnholder_t); /* Skip holder */

    for (bend = bstart + bufsize * mbufcount;
	 bstart < bend; bstart += bufsize)
	{
	mbuf = (psosmbuf_t *)bstart;
	inith(&mbuf->link);
	appendq(freeq,&mbuf->link);
	}

    return mbufcount;
}

static psosmbuf_t *get_mbuf (psosqueue_t *queue,
			     u_long msglen)
{
    psosmbuf_t *mbuf = NULL;

    if (testbits(queue->synchbase.status,Q_NOCACHE))
	{
	mbuf = (psosmbuf_t *)xnmalloc(sizeof(*mbuf) + msglen - sizeof(mbuf->data));

	if (mbuf)
	    inith(&mbuf->link);
	}
    else
	{
	xnholder_t *holder = getq(&queue->freeq);

	if (!holder &&
	    testbits(queue->synchbase.status,Q_INFINITE) &&
	    feed_pool(&queue->chunkq,
		      &queue->freeq,
		      PSOS_QUEUE_MIN_ALLOC,
		      queue->maxlen) != 0)
	    holder = getq(&queue->freeq);

	if (holder)
	    mbuf = link2psosmbuf(holder);
	}

    return mbuf;
}

static u_long q_create_internal (char name[4],
				 u_long maxnum,
				 u_long maxlen,
				 u_long flags,
				 u_long *qid)
{
    psosqueue_t *queue;
    int bflags;
    u_long rc;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    bflags = (flags & Q_VARIABLE);

    if (flags & Q_PRIOR)
	bflags |= XNSYNCH_PRIO;

    if (!(flags & Q_LIMIT))
	{
	maxnum = PSOS_QUEUE_MIN_ALLOC;
	bflags |= Q_INFINITE;
	}

    if (!(flags & Q_VARIABLE))
	maxlen = sizeof(u_long[4]);

    /* Force the use of private buffers for variable-size msg
       exceeding sizeof(u_long[4]) that we can't hold in the
       shared mbuf queue, unless the queue is unlimited. In the
       latter case, dynamic allocation on a per-message basis will be
       used. */

    if (maxlen > sizeof(u_long[4]))
	{
	if (bflags & Q_INFINITE)
	    /* Unlimited-variable msg buffers will be obtained
	       directly from the region #0. */
	    bflags |= Q_NOCACHE;
	else
	    bflags |= Q_PRIVCACHE;
	}
    else
	{
	bflags |= Q_PRIVCACHE;

	if (!(flags & Q_PRIBUF))
	    bflags |= Q_SHAREDINIT;
	}

    queue = (psosqueue_t *)xnmalloc(sizeof(*queue));

    if (!queue)
	return ERR_NOQCB;

    queue->maxnum = maxnum;
    queue->maxlen = maxlen;
    inith(&queue->link);
    initq(&queue->inq);
    initq(&queue->freeq);
    initq(&queue->chunkq);

    if (bflags & Q_PRIVCACHE)
	{
	if (bflags & Q_SHAREDINIT)
	    {
	    splhigh(s);
	    rc = feed_pool(&psoschunkq,&psosmbufq,maxnum,maxlen);
	    splexit(s);
	    }
	else
	    rc = feed_pool(&queue->chunkq,&queue->freeq,maxnum,maxlen);
	
	if (!rc)
	    {
	    /* Can't preallocate msg buffers. */
	    xnfree(queue);
	    return ERR_NOMGB;
	    }

	if (bflags & Q_SHAREDINIT)
	    {
	    splhigh(s);

	    while (countq(&queue->freeq) < maxnum)
		appendq(&queue->freeq,getq(&psosmbufq));

	    splexit(s);
	    }
	}

    xnsynch_init(&queue->synchbase,bflags);

    queue->magic = PSOS_QUEUE_MAGIC;
    queue->name[0] = name[0];
    queue->name[1] = name[1];
    queue->name[2] = name[2];
    queue->name[3] = name[3];
    queue->name[4] = '\0';

    splhigh(s);
    appendq(&psosqueueq,&queue->link);
    splexit(s);

    *qid = (u_long)queue;

    xnarch_create_display(&queue->synchbase,queue->name,psosqueue);

    return SUCCESS;
}

static u_long q_destroy_internal (psosqueue_t *queue)

{
    xnholder_t *holder;
    u_long err, flags;
    spl_t s;

    splhigh(s);

    removeq(&psosqueueq,&queue->link);

    if (countpq(xnsynch_wait_queue(&queue->synchbase)) > 0)
	err = ERR_TATQDEL;
    else if (countq(&queue->inq) > 0)
	err = ERR_MATQDEL;
    else
	err = SUCCESS;

    flags = queue->synchbase.status;
    psos_mark_deleted(queue);
    xnsynch_destroy(&queue->synchbase);

    splexit(s);

    if (testbits(flags,Q_NOCACHE))
	{
	/* No cache used -- return the buffers waiting to be received
	   (i.e.linked to the input queue) to the region #0. Received
	   buffers have already been freed on-the-fly in
	   q_receive_internal(). */

	while ((holder = getq(&queue->inq)) != NULL)
	    xnfree(link2psosmbuf(holder));
	}
    else
	{
	if (testbits(flags,Q_SHAREDINIT))
	    {
	    /* Buffers come from the global shared queue. */

	    splhigh(s);

	    while ((holder = getq(&queue->inq)) != NULL)
		appendq(&psosmbufq,holder);

	    while ((holder = getq(&queue->freeq)) != NULL)
		appendq(&psosmbufq,holder);

	    splexit(s);
	    }
	else
	    {
	    /* Private chunks (i.e. containing all the buffers used by
	       the queue) are directly returned to the heap manager
	       where they come from. */

	    while ((holder = getq(&queue->chunkq)) != NULL)
		xnfree(holder);
	    }
	}

    xnarch_delete_display(&queue->synchbase);

    xnfree(queue);

    return err;
}

static u_long q_delete_internal (u_long qid,
				 u_long flags)
{
    psosqueue_t *queue;
    u_long err;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    splhigh(s);

    queue = psos_h2obj_active(qid,PSOS_QUEUE_MAGIC,psosqueue_t);

    if (!queue)
	{
	err = psos_handle_error(qid,PSOS_QUEUE_MAGIC,psosqueue_t);
	goto unlock_and_exit;
	}

    if ((flags & Q_VARIABLE) &&
	!testbits(queue->synchbase.status,Q_VARIABLE))
	{
	err = ERR_NOTVARQ;
	goto unlock_and_exit;
	}
    
    if (!(flags & Q_VARIABLE) &&
	testbits(queue->synchbase.status,Q_VARIABLE))
	{
	err = ERR_VARQ;
	goto unlock_and_exit;
	}
    
    err = q_destroy_internal(queue);

    if (err == ERR_TATQDEL)
	/* Some task has been readied. */
	xnpod_schedule();

 unlock_and_exit:

    splexit(s);

    return err;
}

static u_long q_receive_internal (u_long qid,
				  u_long flags,
				  u_long timeout,
				  void *msgbuf,
				  u_long buflen,
				  u_long *msglen)
{
    u_long err = SUCCESS;
    xnholder_t *holder;
    psosqueue_t *queue;
    psosmbuf_t *mbuf;
    spl_t s;

    splhigh(s);

    queue = psos_h2obj_active(qid,PSOS_QUEUE_MAGIC,psosqueue_t);

    if (!queue)
	{
	err = psos_handle_error(qid,PSOS_QUEUE_MAGIC,psosqueue_t);
	goto unlock_and_exit;
	}

    if (!(flags & Q_NOWAIT))
	xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if ((flags & Q_VARIABLE) &&
	!xnsynch_test_flags(&queue->synchbase,Q_VARIABLE))
	{
	err = ERR_NOTVARQ;
	goto unlock_and_exit;
	}
    
    if (!(flags & Q_VARIABLE) &&
	xnsynch_test_flags(&queue->synchbase,Q_VARIABLE))
	{
	err = ERR_VARQ;
	goto unlock_and_exit;
	}

again:
    
    holder = getq(&queue->inq);

    if (!holder)
	{
	if (flags & Q_NOWAIT)
	    {
	    err = ERR_NOMSG;
	    goto unlock_and_exit;
	    }

	xnarch_post_graph_if(&queue->synchbase,
			     1, /* PENDED */
			     xnsynch_nsleepers(&queue->synchbase) == 0);

	xnsynch_sleep_on(&queue->synchbase,timeout);

	if (xnthread_test_flags(&psos_current_task()->threadbase,XNRMID))
	    {
	    err = ERR_QKILLD; /* Queue deleted while pending. */
	    goto unlock_and_exit;
	    }
	
	if (xnthread_test_flags(&psos_current_task()->threadbase,XNTIMEO))
	    {
	    err = ERR_TIMEOUT; /* Timeout.*/
	    goto unlock_and_exit;
	    }

	mbuf = psos_current_task()->waitargs.qmsg;

	if (!mbuf)	/* Rare, but spurious wakeups might */
	    goto again; /* occur during memory contention. */

	psos_current_task()->waitargs.qmsg = NULL;
	}
    else
	{
	mbuf = link2psosmbuf(holder);

	xnarch_post_graph(&queue->synchbase,
			  countq(&queue->inq) > 0 ? 2 : 0); /* POSTED or EMPTY */
	}

    if (mbuf->len > buflen)
	err = ERR_BUFSIZ;

    memcpy(msgbuf,mbuf->data,minval(buflen,mbuf->len));

    if (msglen)
	*msglen = mbuf->len;

    if (testbits(queue->synchbase.status,Q_NOCACHE))
	xnfree(mbuf);
    else
	appendq(&queue->freeq,&mbuf->link);

 unlock_and_exit:

    splexit(s);

    return err;
}

static u_long q_send_internal (u_long qid,
			       u_long flags,
			       void *msgbuf,
			       u_long msglen)
{
    u_long err = SUCCESS;
    xnthread_t *sleeper;
    psosqueue_t *queue;
    psosmbuf_t *mbuf;
    spl_t s;

    splhigh(s);

    queue = psos_h2obj_active(qid,PSOS_QUEUE_MAGIC,psosqueue_t);

    if (!queue)
	{
	err = psos_handle_error(qid,PSOS_QUEUE_MAGIC,psosqueue_t);
	goto unlock_and_exit;
	}

    if ((flags & Q_VARIABLE) &&
	!xnsynch_test_flags(&queue->synchbase,Q_VARIABLE))
	{
	err = ERR_NOTVARQ;
	goto unlock_and_exit;
	}
    
    if (!(flags & Q_VARIABLE) &&
	xnsynch_test_flags(&queue->synchbase,Q_VARIABLE))
	{
	err = ERR_VARQ;
	goto unlock_and_exit;
	}

    if (msglen > queue->maxlen)
	{
	err = ERR_MSGSIZ;
	goto unlock_and_exit;
	}

    mbuf = get_mbuf(queue,msglen);

    if (!mbuf)
	{
	err = ERR_NOMGB;
	goto unlock_and_exit;
	}

    sleeper = xnsynch_wakeup_one_sleeper(&queue->synchbase);
    mbuf->len = msglen;
    
    if (sleeper)
	{
	memcpy(mbuf->data,msgbuf,msglen);
	thread2psostask(sleeper)->waitargs.qmsg = mbuf;
	xnpod_schedule();
	goto unlock_and_exit;
	}

    if (!xnsynch_test_flags(&queue->synchbase,Q_INFINITE) &&
	countq(&queue->inq) >= queue->maxnum)
	{
	err = ERR_QFULL;
	goto unlock_and_exit;
	}

    if (flags & Q_JAMMED)
	{
	prependq(&queue->inq,&mbuf->link);
	xnarch_post_graph(&queue->synchbase,4); /* JAMMED */
	}
    else
	appendq(&queue->inq,&mbuf->link);

    memcpy(mbuf->data,msgbuf,msglen);

    xnarch_post_graph_if(&queue->synchbase,
			 countq(&queue->inq) >= queue->maxnum ? 3 : 2,
			 !xnsynch_test_flags(&queue->synchbase,Q_INFINITE)); /* FULL or POSTED */

 unlock_and_exit:

    splexit(s);

    return err;
}

static u_long q_broadcast_internal (u_long qid,
				    u_long flags,
				    void *msgbuf,
				    u_long msglen,
				    u_long *count)
{
    u_long err = SUCCESS;
    xnthread_t *sleeper;
    psosqueue_t *queue;
    spl_t s;

    splhigh(s);

    queue = psos_h2obj_active(qid,PSOS_QUEUE_MAGIC,psosqueue_t);

    if (!queue)
	{
	err = psos_handle_error(qid,PSOS_QUEUE_MAGIC,psosqueue_t);
	goto unlock_and_exit;
	}

    if ((flags & Q_VARIABLE) &&
	!xnsynch_test_flags(&queue->synchbase,Q_VARIABLE))
	{
	err = ERR_NOTVARQ;
	goto unlock_and_exit;
	}
    
    if (!(flags & Q_VARIABLE) &&
	xnsynch_test_flags(&queue->synchbase,Q_VARIABLE))
	{
	err = ERR_VARQ;
	goto unlock_and_exit;
	}

    if (msglen > queue->maxlen)
	{
	err = ERR_MSGSIZ;
	goto unlock_and_exit;
	}

    *count = 0;

    while ((sleeper = xnsynch_wakeup_one_sleeper(&queue->synchbase)) != NULL)
	{
	psosmbuf_t *mbuf = get_mbuf(queue,msglen);

	if (!mbuf)
	    {
	    /* Will beget a spurious wakeup. */
	    thread2psostask(sleeper)->waitargs.qmsg = NULL;
	    break;
	    }
    
	mbuf->len = msglen;
	memcpy(mbuf->data,msgbuf,msglen);
	thread2psostask(sleeper)->waitargs.qmsg = mbuf;
	(*count)++;
	}

 unlock_and_exit:

    splexit(s);

    return err;
}

static u_long q_ident_internal (char name[4],
				u_long flags,
				u_long node,
				u_long *qid)
{
    u_long err = SUCCESS;
    xnholder_t *holder;
    psosqueue_t *queue;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (node > 1)
	return ERR_NODENO;

    if (!name)
	return ERR_OBJNF;

    splhigh(s);

    for (holder = getheadq(&psosqueueq);
	 holder; holder = nextq(&psosqueueq,holder))
	{
	queue = link2psosqueue(holder);

	if (queue->name[0] == name[0] &&
	    queue->name[1] == name[1] &&
	    queue->name[2] == name[2] &&
	    queue->name[3] == name[3])
	    {
	    if (((flags & Q_VARIABLE) &&
		 testbits(queue->synchbase.status,Q_VARIABLE)) ||
		(!(flags & Q_VARIABLE) &&
		 !testbits(queue->synchbase.status,Q_VARIABLE)))
		{
		*qid = (u_long)queue;
		goto unlock_and_exit;
		}
	    }
	}

    err = ERR_OBJNF;

 unlock_and_exit:

    splexit(s);

    return err;
}

u_long q_create (char name[4],
		 u_long maxnum,
		 u_long flags,
		 u_long *qid) {

    return q_create_internal(name,
			     maxnum,
			     sizeof(u_long[4]),
			     flags & ~Q_VARIABLE,
			     qid);
}

u_long q_vcreate (char name[4],
		  u_long flags,
		  u_long maxnum,
		  u_long maxlen,
		  u_long *qid) {

    return q_create_internal(name,
			     maxnum,
			     maxlen,
			     flags|Q_VARIABLE,
			     qid);
}

u_long q_delete (u_long qid) {
    return q_delete_internal(qid,0);
}

u_long q_vdelete (u_long qid) {
    return q_delete_internal(qid,Q_VARIABLE);
}

u_long q_ident (char name[4],
		u_long node,
		u_long *qid) {

    return q_ident_internal(name,
			    0,
			    node,
			    qid);
}

u_long q_vident (char name[4],
		 u_long node,
		 u_long *qid) {

    return q_ident_internal(name,
			    Q_VARIABLE,
			    node,
			    qid);
}

u_long q_receive (u_long qid,
		  u_long flags,
		  u_long timeout,
		  u_long msgbuf[4]) {

    return q_receive_internal(qid,
			      flags & ~Q_VARIABLE,
			      timeout,
			      msgbuf,
			      sizeof(msgbuf),
			      NULL);
}

u_long q_vreceive(u_long qid,
		  u_long flags,
		  u_long timeout,
		  void *msgbuf,
		  u_long buflen,
		  u_long *msglen) {

    return q_receive_internal(qid,
			      flags|Q_VARIABLE,
			      timeout,
			      msgbuf,
			      buflen,
			      msglen);
}

u_long q_send (u_long qid,
	       u_long msgbuf[4]) {

    return q_send_internal(qid,
			   0,
			   msgbuf,
			   sizeof(msgbuf));
}

u_long q_vsend (u_long qid,
		void *msgbuf,
		u_long msglen) {

    return q_send_internal(qid,
			   Q_VARIABLE,
			   msgbuf,
			   msglen);
}

u_long q_broadcast (u_long qid,
		    u_long msgbuf[4],
		    u_long *count) {

    return q_broadcast_internal(qid,
				0,
				msgbuf,
				sizeof(msgbuf),
				count);
}

u_long q_vbroadcast (u_long qid,
		     void *msgbuf,
		     u_long msglen,
		     u_long *count) {

    return q_broadcast_internal(qid,
				Q_VARIABLE,
				msgbuf,
				msglen,
				count);
}

u_long q_urgent (u_long qid,
		 u_long msgbuf[4]) {

    return q_send_internal(qid,
			   Q_JAMMED,
			   msgbuf,
			   sizeof(msgbuf));
}

u_long q_vurgent (u_long qid,
		  void *msgbuf,
		  u_long msglen) {

    return q_send_internal(qid,
			   Q_VARIABLE|Q_JAMMED,
			   msgbuf,
			   msglen);
}
