/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
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

#include "rtai_config.h"
#include "vxworks/defs.h"




typedef struct wind_msg
{
    xnholder_t link;

#define link2wind_msg(laddr) \
((wind_msg_t *)(((char *)laddr) - (int)(&((wind_msg_t *)0)->link)))

    unsigned int length;
    
    char buffer[0];

} wind_msg_t;


typedef struct wind_msgq
{
    unsigned int magic;
    
    UINT msg_length;

    xnholder_t * free_list;     /* simply linked list of free messages */

    xnqueue_t msgq;             /* queue of messages available for reading */

    xnholder_t link;            /* link in wind_msgq_t */

#define link2wind_msgq(laddr) \
((wind_msgq_t *)(((char *)laddr) - (int)(&((wind_msgq_t *)0)->link)))

    xnsynch_t synchbase;        /* pended readers or writers */

} wind_msgq_t;


static xnqueue_t wind_msgq_q;

static	int msgq_destroy_internal(wind_msgq_t * queue);




void wind_msgq_init (void)
{
    initq(&wind_msgq_q);
}


void wind_msgq_cleanup (void)
{
    xnholder_t * holder;

    while((holder = getheadq(&wind_msgq_q)) != NULL)
        msgq_destroy_internal(link2wind_msgq(holder));
}




/* free_msg: return a message to the free list */
static inline void free_msg(wind_msgq_t * queue, wind_msg_t * msg)
{
    msg->link.next = queue->free_list;
    queue->free_list = &msg->link;
}


/* get a message from the free list */
static inline wind_msg_t * get_free_msg(wind_msgq_t * queue)
{
    wind_msg_t * msg;

    if(queue->free_list == NULL)
        return NULL;
    
    msg = link2wind_msg(queue->free_list);
    queue->free_list = queue->free_list->next;
    inith(&msg->link);

    return msg;
}


/* try to unqueue message for reading */
static inline wind_msg_t * unqueue_msg(wind_msgq_t * queue)
{
    xnholder_t * holder;
    wind_msg_t * msg;

    holder = getheadq(&queue->msgq);
    if(holder == NULL)
        return NULL;
    
    msg = link2wind_msg(holder);
    removeq(&queue->msgq, holder);
    
    return msg;
}




int msgQCreate ( int nb_msgs, int length, int flags )
{
    wind_msgq_t * queue;
    char * msgs_mem;
    int i, msg_size;
    xnflags_t bflags = 0;
    
    check_NOT_ISR_CALLABLE(return 0);

    error_check( nb_msgs<=0 , S_msgQLib_INVALID_QUEUE_TYPE, return 0 );

    error_check( flags & ~WIND_MSG_Q_OPTION_MASK, S_msgQLib_INVALID_QUEUE_TYPE,
                 return 0 );
    
    error_check( length<=0, S_msgQLib_INVALID_MSG_LENGTH, return 0 );
    
    msgs_mem = xnmalloc( sizeof(wind_msgq_t) +
                         nb_msgs*(sizeof(wind_msg_t)+length) );

    error_check( msgs_mem == NULL, S_memLib_NOT_ENOUGH_MEMORY, return 0);

    queue = (wind_msgq_t *) msgs_mem;
    msgs_mem += sizeof(wind_msgq_t);

    queue->magic = WIND_MSGQ_MAGIC;
    queue->msg_length = length;

    queue->free_list = NULL;
    initq(&queue->msgq);

    /* init of the synch object : */
    if( flags & MSG_Q_PRIORITY )
        bflags |= XNSYNCH_PRIO;

    xnsynch_init(&queue->synchbase, bflags);

    
    msg_size = sizeof(wind_msg_t)+length;

    for( i=0 ; i<nb_msgs; ++i, msgs_mem += msg_size )
        free_msg(queue, (wind_msg_t *) msgs_mem);

    inith(&queue->link);

    xnmutex_lock(&__imutex);
    appendq(&wind_msgq_q, &queue->link);
    xnmutex_unlock(&__imutex);
    
    return (int) queue;
}


STATUS msgQDelete (int qid)
{
    wind_msgq_t * queue;
    check_NOT_ISR_CALLABLE(return ERROR);

    xnmutex_lock(&__imutex);

    check_OBJ_ID_ERROR(qid,wind_msgq_t,queue,WIND_MSGQ_MAGIC,goto error);
    if (msgq_destroy_internal(queue) == XNSYNCH_RESCHED)
        xnpod_schedule(&__imutex);
    
    xnmutex_unlock(&__imutex);
    return OK;

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}


int msgQNumMsgs (int qid)
{

    wind_msgq_t * queue;
    int result;

    xnmutex_lock(&__imutex);

    check_OBJ_ID_ERROR(qid,wind_msgq_t,queue,WIND_MSGQ_MAGIC,goto error);
    
    result = queue->msgq.elems;

    xnmutex_unlock(&__imutex);
    return result;

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}


int msgQReceive ( int qid, char *buf,UINT bytes,int to )
{
    xnticks_t timeout;
    wind_msgq_t *queue;
    wind_msg_t * msg;
    xnthread_t * thread;
    wind_task_t * task;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    error_check( buf == NULL, 0, return ERROR );

    xnmutex_lock(&__imutex);

    check_OBJ_ID_ERROR(qid,wind_msgq_t,queue,WIND_MSGQ_MAGIC,goto error);

    error_check( bytes <= 0 || bytes > queue->msg_length,
                 S_msgQLib_INVALID_MSG_LENGTH, goto error);
    
    /* here, we are finished with error checking, the real work can begin */
    if( (msg = unqueue_msg(queue)) == NULL )
    {
        /* message queue is empty */
        
        error_check( to == NO_WAIT, S_objLib_OBJ_UNAVAILABLE, goto error);
        
        if(to == WAIT_FOREVER)
            timeout = XN_INFINITE;
        else
            timeout = to;
        
        task = wind_current_task();
        thread = &task->threadbase;
        task->rcv_buf = buf;
        task->rcv_bytes = bytes;
        
        xnsynch_sleep_on(&queue->synchbase,timeout,&__imutex);
        
        error_check(xnthread_test_flags(thread,XNRMID),
                    S_objLib_OBJ_DELETED, goto error);
        error_check(xnthread_test_flags(thread,XNTIMEO),
                    S_objLib_OBJ_TIMEOUT, goto error);
        
        bytes = task->rcv_bytes;
    } else {
        if(msg->length < bytes)
            bytes = msg->length;
        memcpy(buf, msg->buffer, bytes);
        free_msg(queue, msg);
        
        /* check if some sender is pending */
        if (xnsynch_wakeup_one_sleeper(&queue->synchbase))
            xnpod_schedule(&__imutex);
    }

    xnmutex_unlock(&__imutex);
    return bytes;

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}


STATUS msgQSend (int qid ,char * buf, UINT bytes,int to, int prio)
{
    wind_msgq_t * queue;
    xnticks_t timeout;
    wind_msg_t * msg;
    xnthread_t * thread;
    wind_task_t * task;
    
    if (xnpod_asynch_p() && to != NO_WAIT)
    {
        wind_errnoset(S_msgQLib_NON_ZERO_TIMEOUT_AT_INT_LEVEL);
        return ERROR;
    }

    error_check( prio != MSG_PRI_NORMAL && prio != MSG_PRI_URGENT,
                 0, return ERROR ); /* FIXME: find another status than 0 */

    error_check( buf == NULL, 0, return ERROR );

    xnmutex_lock(&__imutex);

    check_OBJ_ID_ERROR(qid,wind_msgq_t,queue,WIND_MSGQ_MAGIC,goto error);
    
    error_check( bytes <= 0 || bytes > queue->msg_length,
                 S_msgQLib_INVALID_MSG_LENGTH, goto error);
    
    /* here, we are finished with error checking, the real work can begin */
    
    if( queue->msgq.elems == 0 &&
        (thread =
         xnsynch_wakeup_one_sleeper(&queue->synchbase))!=NULL )
    {
        /* the message queue is empty and we have found a pending receiver */
        task = thread2wind_task(thread);
        if( bytes < task->rcv_bytes)
            task->rcv_bytes = bytes;
        
        memcpy(task->rcv_buf, buf, bytes);
        xnpod_schedule(&__imutex);
        
    } else {
        msg = get_free_msg(queue);
        if (msg == NULL)
        {
            /* the message queue is full, we need to wait */
            error_check(to==NO_WAIT, S_objLib_OBJ_UNAVAILABLE, goto error);
            
            thread = &wind_current_task()->threadbase;
            
            if(to == WAIT_FOREVER)
                timeout = XN_INFINITE;
            else
                timeout = to;
            
            xnsynch_sleep_on(&queue->synchbase,timeout,&__imutex);
            
            error_check(xnthread_test_flags(thread,XNRMID),
                        S_objLib_OBJ_DELETED, goto error);
            error_check(xnthread_test_flags(thread,XNTIMEO),
                        S_objLib_OBJ_TIMEOUT, goto error);
            
            /* a receiver unblocked us, so we are sure to obtain a message
               buffer */
            msg = get_free_msg(queue);
        }
        
        msg->length = bytes;
        memcpy(msg->buffer, buf, bytes);
        if( prio == MSG_PRI_URGENT )
            prependq(&queue->msgq, &msg->link);
        else
            appendq(&queue->msgq, &msg->link);
    }

    xnmutex_unlock(&__imutex);
    return OK;

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;

}




static int msgq_destroy_internal(wind_msgq_t * queue)
{
    int s = xnsynch_destroy(&queue->synchbase);
    wind_mark_deleted(queue);
    removeq(&wind_msgq_q, &queue->link);
    xnfree(queue);
    return s;
}
