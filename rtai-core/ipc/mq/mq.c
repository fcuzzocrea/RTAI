/*
 * pqueues interface for Real Time Linux.
 *
 * Timed services extension and user space integration for RTAI by
 * Paolo Mantegazza <mantegazza@aero.polimi.it>.
 *
 * Copyright (©) 1999 Zentropic Computing, All rights reserved
 *  
 * Authors:             Trevor Woolven (trevw@zentropix.com)
 *
 * Original date:       Thu 15 Jul 1999
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <asm/uaccess.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
extern struct proc_dir_entry *rtai_proc_root;
#endif
#include <rtai_schedcore.h>
#include <rtai_proc_fs.h>

MODULE_LICENSE("GPL");

#define pthread_cond_t                   SEM
#define pthread_mutex_t                  SEM
#define pthread_mutex_init(mutex, attr)  rt_mutex_init(mutex)
#define pthread_mutex_unlock             rt_mutex_unlock
#define pthread_mutex_lock               rt_mutex_lock
#define pthread_mutex_destroy            rt_mutex_destroy
#define pthread_cond_init(cond, attr)    rt_cond_init(cond)
#define pthread_cond_wait                rt_cond_wait
#define pthread_cond_signal              rt_cond_signal
#define pthread_cond_destroy             rt_cond_destroy

#define REPORT(fmt, args...)  rt_printk("<Rep> " fmt ,##args)

#undef DBG
#ifdef ZDEBUG
#define DBG(fmt, args...)  rt_printk("<%s %d> " fmt, __FILE__, __LINE__ ,##args)
#else
#define DBG(fmt, args...)
#endif

#undef nDGB
#define nDBG(fmt, args...)

#ifndef OK
#define OK	0
#endif
#ifndef ERROR
#define ERROR	-1
#endif

///////////////////////////////////////////////////////////////////////////////
//      LOCAL DEFINITIONS
///////////////////////////////////////////////////////////////////////////////

#define MAX_RT_TASKS 128

///////////////////////////////////////////////////////////////////////////////
//      PACKAGE GLOBAL DATA
///////////////////////////////////////////////////////////////////////////////
#define rtai_pqueue_version "0.6"

static uint num_pqueues = 0;
static struct _pqueue_descr_struct rt_pqueue_descr[MAX_PQUEUES] = {{0}};
static struct _pqueue_access_struct task_pqueue_access[MAX_RT_TASKS] = {{0}};

static QUEUEING_POLICY task_queueing_policy = FIFO_BASED;
static QUEUE_TYPE queue_type = POSIX;

static pthread_mutex_t pqueue_mutex;

///////////////////////////////////////////////////////////////////////////////
//      LOCAL FUNCTIONS
///////////////////////////////////////////////////////////////////////////////
void *init_z_apps(void *this_task)
{
  Z_APPS *zapps;
  RT_TASK *task = (RT_TASK*)this_task;

    if( task->system_data_ptr == NULL) {
	// This task has not yet created a Z_APPS structure
    	task->system_data_ptr = rt_malloc(sizeof(Z_APPS));
    	zapps = (Z_APPS*)task->system_data_ptr;

	// Now initialise it
	zapps->in_use_count = 0;
	zapps->pthreads = NULL;
	zapps->pqueues  = NULL;
	zapps->vxtasks  = NULL;

    }
    return task->system_data_ptr;

} // End function - init_z_apps

void free_z_apps(void *this_task)
{
  RT_TASK *task = (RT_TASK*)this_task;

    if(task->system_data_ptr != NULL) {
	rt_free(task->system_data_ptr);
	task->system_data_ptr = NULL;
    }

} // End function - free_z_apps

static int name_to_id(char *name)
{
//This function takes a message queue name and tries to find it in the list
//of queues. Once found the associated message queue list index is returned.
//Note that this lies in the range 0..MAX_PQUEUES
//If the name cannot be found among the existing queues, an error is reported.
//
int ind;

    DBG("Looking for queue named %s\n", name);

    for (ind = 0; ind < MAX_PQUEUES; ind++)
    {
	//if (rt_pqueue_descr[ind].q_name != NULL && 
	if( (strcmp(rt_pqueue_descr[ind].q_name, "")   != 0) && 
	    (strcmp(rt_pqueue_descr[ind].q_name, name) == 0) ) {
	    DBG("%s found at %d\n", name, ind);
	    return ind;
	}
    } 
    DBG("cannot find queue named %s\n", name); 
    return ERROR;

}  // End function - name_to_id

// ----------------------------------------------------------------------------
static inline mq_bool_t is_empty(struct queue_control *q)
{
//This function tests a queue to see if it is empty
//
    return (q->attrs.mq_curmsgs == 0) ? TRUE : FALSE;

}  // End function - is_empty

// ----------------------------------------------------------------------------
static inline mq_bool_t is_full(struct queue_control *q)
{
//This function tests a queue to see if it is full
//
    return (q->attrs.mq_curmsgs == q->attrs.mq_maxmsg) ? TRUE : FALSE;

}  // End function - is_full

// ----------------------------------------------------------------------------
static MSG_HDR* getnode(Q_CTRL *queue)
{
//This function searches for an unused (spare) message slot within the
//specified queue. Each message slot in the queue has a used/unused flag which
//can be tested. If a spare slot is found, it is marked as used (here) and 
//the address of the slot in the queue is returned.
//
//A null pointer is returned if there are no spare slots left. This would 
//indicate that something drastic has gone wrong as the 'is_full' function 
//should be used first to check for space on the queue. 
//
uint ind;
unsigned char msg_size;
MSG_HDR *msg;

    DBG("\n");

    msg_size = queue->attrs.mq_msgsize + sizeof(MSG_HDR);
    msg = queue->base;

    DBG("starting at %x, looking for messages of size %d bytes\n", 
				(int)msg, msg_size);
    for(ind = 0; ind < queue->attrs.mq_maxmsg; ind++)
    {
	DBG("looking at %x, found %d\n", (int)msg, msg->in_use);

	if(msg->in_use == FALSE)
        {
	    DBG("found spare slot for message at ofs %d, addr %x\n", 
							ind, (int)msg);
	    msg->in_use = TRUE;
 	    return msg;
        }
	DBG("incrementing Message ptr by %x\n", msg_size);
	msg = (MSG_HDR *)((char *)msg + msg_size);
    }
    DBG("cannot find unused message slot in queue\n");
    return NULL;

}  // End function - getnode

// ----------------------------------------------------------------------------
static void insert_message(Q_CTRL *q, MSG_HDR *this_msg)
{
//This function finds the appropriate point in a priority queue to
//insert the supplied message. It preserves FIFO order within each
//priority levela and can therefore be used for FIFO queuing policies
//simply by making the priority equal to the supplied message priority
//
MSG_HDR *prev, *insertpt;

    //Do a quick check in case the message at the back of the queue has
    //a higher priority than this one, in which case this one can just
    //go at the back of the queue. 
    //Remember that Posix priorities increase from 0 to (at least) 32
    //
    if( ((MSG_HDR*)q->tail)->priority >= this_msg->priority) {
	    DBG("adding to the back of the queue\n");
    	    ((MSG_HDR*)q->tail)->next = this_msg;
	    q->tail = this_msg;
	    DBG("head = %x, tail = %x\n", (int)q->head, 
					  (int)q->tail);
    } else {
	//Find the priority-based insertion point
	prev = insertpt = q->head;
  	if(queue_type == POSIX) {
	    //POSIX queues preserve FIFO ordering of messages within
	    //a particular priority level
	    while( insertpt->priority >= this_msg->priority)
	    {
	        prev = insertpt;
	        insertpt = insertpt->next; 
	    }
	}
	else {
	    //Urgent VxWorks messages go to the head of the queue
	    //Urgh! a) I don't like embedding VxWorks stuff here
	    //      b) It's a crap implementation anyway
	    //
	    insertpt = q->head;
	}

	//We've now found a message (or messages) of equal or lower
	//priority than the one we're trying to put onto the queue
	//
 	if( insertpt == q->head ) {
	    DBG("inserting at the head of the queue\n");
	    this_msg->next = q->head;
	    q->head = this_msg;
            DBG("head = %x, next = %x\n", (int)q->head, 
					  (int)this_msg->next);
	} else {
	    DBG("inserting after p = %d\n", prev->priority);
	    this_msg->next = prev->next;
	    prev->next = this_msg;
	    DBG("prev = %x, next = %x\n", (int)prev, 
					  (int)this_msg->next);
	}   
    }

} // End function - insert_message

// ----------------------------------------------------------------------------
static mq_bool_t is_blocking(MSG_QUEUE *q)
{
//This function checks whether or not the calling task opened this queue
//in blocking mode 
//
//Returns TRUE if O_NONBLOCK = FALSE
//
RT_TASK *this_task = _rt_whoami();
struct _pqueue_access_struct *task_queue_data_ptr;
Z_APPS *zapps;
int q_ind;

    DBG("\n");

    //Find this queue in the task's list of open ones
    zapps = (Z_APPS*)this_task->system_data_ptr;
    task_queue_data_ptr = (QUEUE_CTRL)zapps->pqueues;
    for(q_ind = 0; q_ind < MQ_OPEN_MAX; q_ind++) {
	if(task_queue_data_ptr->q_access[q_ind].q_id == q->q_id) {
    	    return (task_queue_data_ptr->q_access[q_ind].oflags & O_NONBLOCK) ?
		FALSE : TRUE;
	}
    }
    REPORT("cannot find queue %s in task's open list\n", q->q_name);
    return FALSE;

} // End function - is_blocking

// ----------------------------------------------------------------------------
static mq_bool_t can_access(MSG_QUEUE *q, Q_ACCESS access)
{
//This function checks the user and group permissions granted by the queue's
//creator and the mode used by the calling task when it opened the queue to
//determine if the calling task is allowed the specified access to the queue
//
mq_bool_t permissions_ok = FALSE;
mq_bool_t mode_ok = FALSE;
RT_TASK *caller_pid = _rt_whoami();
int q_ind = 0;
int q_access_flags = 0;
struct _pqueue_access_struct *task_queue_data_ptr = NULL;
Z_APPS *zapps;

    DBG("\n");

    if( q->owner == caller_pid ) {
	// I am the owner of this task
	if( ( (access == FOR_READ)  && (q->permissions & S_IRUSR) ) ||
	    ( (access == FOR_WRITE) && (q->permissions & S_IWUSR) ) ) {
	    permissions_ok = TRUE;
	    DBG("Owner has permission to %s queue %s\n",
			((access == FOR_READ) ? "read from" : "write to"),
			q->q_name);

	} else {
	    permissions_ok = FALSE;
	    DBG("Owner does not have permission to %s queue %s\n",
			((access == FOR_READ) ? "read from" : "write to"),
			q->q_name);

	}
    } else {
	// I am not the owner of this task
	if( ( (access == FOR_READ)  && (q->permissions & S_IRGRP) ) ||
	    ( (access == FOR_WRITE) && (q->permissions & S_IWGRP) ) ) {
	    permissions_ok = TRUE;
	    DBG("Group member has permission to %s queue %s\n",
			((access == FOR_READ) ? "read from" : "write to"),
			q->q_name);

	} else {
	    permissions_ok = FALSE;
	    DBG("Group member does not have permission to %s queue %s\n",
			((access == FOR_READ) ? "read from" : "write to"),
			q->q_name);
	}
    }
    if (permissions_ok == TRUE) {

	//Find the access mode with which this task opened this queue
	zapps = (Z_APPS*)caller_pid->system_data_ptr;
	task_queue_data_ptr = (QUEUE_CTRL)zapps->pqueues;
    	//DBG("task q ptr = %p\n", task_queue_data_ptr);

  	if (task_queue_data_ptr == NULL) {
	    REPORT("task has not opened any queues!\n");
	    return FALSE;
	}
	for (q_ind = 0; q_ind < MQ_OPEN_MAX; q_ind++) {
	    if (task_queue_data_ptr->q_access[q_ind].q_id == q->q_id) {
		q_access_flags = 
			task_queue_data_ptr->q_access[q_ind].oflags;
		break;
	    }
	}
	if(q_ind == MQ_OPEN_MAX) {
	    REPORT("cannot find queue %s in task's open list\n", q->q_name);
	    return FALSE;
	}

	DBG("qId = %d, q_ind = %d, q_access_flags = %d\n", q->q_id, 
						q_ind, q_access_flags);

	//Check the mode in which this queue was opened by this task
	if (access == FOR_WRITE) {
	    if( (q_access_flags & O_WRONLY) || (q_access_flags & O_RDWR) ) {
	        mode_ok = TRUE;
	        DBG("Have correctly opened queue for writing\n");
	    } else {
	        DBG("Have not correctly opened queue for writing\n");
	    }
	} else {
	    //Default is queue opened for reading
    	    mode_ok = TRUE;
	    DBG("Have opened queue for reading\n");
	}
    }
    if(mode_ok) {
	DBG("Task has full permission to %s queue %s\n",
			((access == FOR_READ) ? "read from" : "write to"),
			q->q_name);
    }
    return mode_ok;

}  // End function - can_access

// ----------------------------------------------------------------------------
static void initialise_queue(Q_CTRL *q)
{
void *msg_ptr;
unsigned long msg_size;
unsigned long msg_ind;

    DBG("\n");

    //Initialise all message 'slots' in the queue
    msg_size = q->attrs.mq_msgsize + sizeof(MSG_HDR);
    msg_ptr = q->base;
    if (msg_ptr == NULL) {
	REPORT("Error: Invalid queue pointer\n");
	return;
    }
    for(msg_ind = 0; msg_ind < q->attrs.mq_maxmsg; msg_ind++) {
        DBG("Clearing message %ld at %p\n", msg_ind, msg_ptr);
	((MSG_HDR*)msg_ptr)->in_use = FALSE;
	((MSG_HDR*)msg_ptr)->size = 0;
	((MSG_HDR*)msg_ptr)->priority = MQ_MIN_MSG_PRIORITY;
	((MSG_HDR*)msg_ptr)->next = NULL;
	msg_ptr += (char)msg_size;
    }

} // End function - initialise_queue

// ----------------------------------------------------------------------------
static void delete_queue(int q_index)
{
MQMSG *msg;
int i, msg_size;
MQ_ATTR empty_pqueue_attrs = {0,0,0,0};


    //Clear out the queue
    msg = rt_pqueue_descr[q_index].data.base;    
    msg_size = rt_pqueue_descr[q_index].data.attrs.mq_msgsize + 
						sizeof(MSG_HDR);
    msg_size /= sizeof(int);
    for(i = 0; i < rt_pqueue_descr[q_index].data.attrs.mq_maxmsg; i++) {
    	msg->hdr.in_use = FALSE;
	msg = (MQMSG *)((char *)msg + msg_size);
    }

    //De-allocate the memory reserved for the queue
    rt_free(rt_pqueue_descr[q_index].data.base);

    //Reset the queue descriptor
    rt_pqueue_descr[q_index].owner = NULL;
    rt_pqueue_descr[q_index].open_count = 0;
    strcpy(rt_pqueue_descr[q_index].q_name, "\0");
    rt_pqueue_descr[q_index].q_id = INVALID_PQUEUE;
    rt_pqueue_descr[q_index].data.base = NULL;
    rt_pqueue_descr[q_index].data.head = NULL;
    rt_pqueue_descr[q_index].data.tail = NULL;
    rt_pqueue_descr[q_index].data.attrs = empty_pqueue_attrs;
    rt_pqueue_descr[q_index].permissions = 0;

    //Free conditional variables and mutex (mutex always locked)
    pthread_mutex_unlock(&rt_pqueue_descr[q_index].mutex);
    pthread_mutex_destroy(&rt_pqueue_descr[q_index].mutex);
    pthread_cond_destroy(&rt_pqueue_descr[q_index].emp_cond);
    pthread_cond_destroy(&rt_pqueue_descr[q_index].full_cond);

    num_pqueues--;	
    DBG("%d pQueues left in the system\n", num_pqueues);

} // End function - delete_queue

///////////////////////////////////////////////////////////////////////////////
//      ACCESS FUNCTIONS
///////////////////////////////////////////////////////////////////////////////
QUEUEING_POLICY get_task_queueing_policy(void)
{

    DBG("\n");
    return task_queueing_policy;

} // End function - get_task_queueing_policy

// ----------------------------------------------------------------------------
QUEUEING_POLICY set_task_queueing_policy(QUEUEING_POLICY policy)
{

    DBG("\n");
    task_queueing_policy = policy;
    return task_queueing_policy;

} // End function - set_task_queueing_policy

// ----------------------------------------------------------------------------
QUEUE_TYPE get_queue_type(void) 
{

    DBG("\n");
    return queue_type;

} // End function - get_queue_type

// ----------------------------------------------------------------------------
QUEUE_TYPE set_queue_type(QUEUE_TYPE type) 
{

    DBG("\n");
    queue_type = type;
    return queue_type;

} // End function - set_queue_type

///////////////////////////////////////////////////////////////////////////////
//      POSIX MESSAGE QUEUES API
///////////////////////////////////////////////////////////////////////////////
mqd_t mq_open(char *mq_name, int oflags, mode_t permissions, 
							struct mq_attr *mq_attr)
{
int queue_size = 0;
MQ_ATTR default_data_queue_attrs = {MAX_MSGS, MAX_MSGSIZE, MQ_NONBLOCK, 0};
int q_index = 0, t_index = 0;
char msg_size;
int q_ind = 0;
mq_bool_t q_found = FALSE;
int spare_count = 0;
int first_spare = 0;
void *mem_ptr = NULL;

RT_TASK *this_task = _rt_whoami();
struct _pqueue_access_struct *task_data_ptr = NULL;
Z_APPS *zapps_ptr = NULL; 

    DBG("\n");

    if(this_task->system_data_ptr == NULL) {
	// Need to allocate a Z_APPS structure for this task
	this_task->system_data_ptr = init_z_apps(this_task);
	DBG("Creating Z_APPS structure at %p\n", this_task->system_data_ptr);
    }
    zapps_ptr = (Z_APPS*)this_task->system_data_ptr;	
    task_data_ptr = (QUEUE_CTRL)zapps_ptr->pqueues;
    if (task_data_ptr != NULL) {
    	DBG("task data ptr = %p\n", task_data_ptr);
    }
    //===========================
    // OPEN AN EXISTING QUEUE?
    //===========================
    pthread_mutex_lock(&pqueue_mutex);
    if( (q_index = name_to_id(mq_name) ) >= 0 ) {

	DBG( "queue %s, id %d, index %d already exists\n", 
	     mq_name, rt_pqueue_descr[q_index].q_id, q_index
	     );

	//Report an error for a pre-existing queue?
        if ( (oflags & O_CREAT) && (oflags & O_EXCL) ) {
	    DBG("queue already exists, O_CREAT and O_EXCL specified\n");
	    pthread_mutex_unlock(&pqueue_mutex);
	    return -EEXIST;
        }

	//Has this task already opened a queue and got itself a
	//queue access structure?
	if ( task_data_ptr == NULL ) {

	    DBG("task needs queue access data array\n");

	    //Find a spare task pqueue access slot for this task
	    for (t_index = 0; t_index < MAX_RT_TASKS; t_index++) {
		if (task_pqueue_access[t_index].this_task == NULL) {
		    DBG("spare task queue data slot found at %d\n", t_index);
	    	    task_data_ptr = &(task_pqueue_access[t_index]);
		    DBG("task data ptr = %x\n", (int)task_data_ptr);
		    task_data_ptr->this_task = this_task;
		    zapps_ptr->pqueues = task_data_ptr;
		    DBG("stored as..%x\n", (int)zapps_ptr->pqueues);
		    //this_task->pqueue_data_ptr = task_data_ptr; 
		    //DBG("stored as..%x\n", (int)this_task->pqueue_data_ptr);
		    break;
		}
	    }
	    if (t_index == MAX_RT_TASKS) {
		REPORT("cannot find spare task pqueue access struct\n");
		pthread_mutex_unlock(&pqueue_mutex);
		return -ENOMEM;
	    }
	}

	//Now record that this task has opened this queue and
	//the access permissions required
	//
	//Check first to see if this task has already opened this queue
	//and while doing so, record the number of spare 'slots' for this
	//task to have further opened queues
	//
	pthread_mutex_lock(&rt_pqueue_descr[q_index].mutex);
	for (q_ind = 0; q_ind < MQ_OPEN_MAX; q_ind++) {
	    if (task_data_ptr->q_access[q_ind].q_id == 
					rt_pqueue_descr[q_index].q_id) { 
		q_found = TRUE;
		break;
	    }
	    else if(task_data_ptr->q_access[q_ind].q_id == INVALID_PQUEUE) {
		if (spare_count == 0 ) {
		    first_spare = q_ind;
		}
		spare_count++;
	    }
	}

	//If the task has not already opened this queue and there are no
	//more available slots, can't do anymore...
	if (!q_found && spare_count == 0) {
	    REPORT("task has maximum number of open queues already\n");
	    pthread_mutex_unlock(&rt_pqueue_descr[q_index].mutex);
	    pthread_mutex_unlock(&pqueue_mutex);
	    return -EINVAL;
	}
	//Either the queue has already been opened and so we can re-use
	//it's slot, or a new one is being opened in an unused slot
	if(!q_found) {
	    //Open a new one, using the first free slot
	    task_data_ptr->n_open_pqueues++;
	    q_ind = first_spare;
	}
	task_data_ptr->q_access[q_ind].q_id = rt_pqueue_descr[q_index].q_id;
	task_data_ptr->q_access[q_ind].oflags = oflags;
	DBG("task ind = %d, oflags = %d\n", q_ind, oflags);
	pthread_mutex_unlock(&rt_pqueue_descr[q_index].mutex);

    }  // End if - queue already exists
     
    //===================
    // CREATE A QUEUE? 
    //===================
    else if( oflags & O_CREAT ) {
	if(num_pqueues >= MAX_PQUEUES) {
	    REPORT("Cannot create a new queue, limit exhausted\n");
	    pthread_mutex_unlock(&pqueue_mutex);
	    return -ENOMEM;
	}
	DBG("Creating new queue %s\n", mq_name);

	//Check the size of the name
	if( strlen(mq_name) >= MQ_NAME_MAX) {
	    REPORT("name too long for queue %s\n", mq_name);
	    pthread_mutex_unlock(&pqueue_mutex);
	    return -ENAMETOOLONG;
	}
 
	//Allocate a task pqueue access structure to this task, if necessary.
	//Otherwise, check that this task has not already opened too many 
	//queues
	//
	if ( task_data_ptr == NULL ) {

	    DBG("task needs queue access data array\n");

	    //Find a spare task pqueue access slot for this task
	    for (t_index = 0; t_index < MAX_RT_TASKS; t_index++) {
		if (task_pqueue_access[t_index].this_task == NULL) {
		    DBG("Owner is %p\n", this_task);
		    DBG("spare task queue data slot found at %d\n", t_index);
	    	    task_data_ptr = &task_pqueue_access[t_index];
		    DBG("task data ptr = %x\n", (int)task_data_ptr);
		    task_data_ptr->this_task = this_task;
		    zapps_ptr->pqueues = task_data_ptr;
		    DBG("stored as..%x\n", (int)zapps_ptr->pqueues);
		    //this_task->pqueue_data_ptr = 
		    //			(void *)&task_pqueue_access[t_index]; 
		    //DBG("stored as..%x\n", (int)this_task->pqueue_data_ptr);
		    break;
		}
	    }
	    if (t_index == MAX_RT_TASKS) {
		REPORT("cannot find spare task pqueue access struct\n");
		pthread_mutex_unlock(&pqueue_mutex);
		return -ENOMEM;
	    }
	}
	else if	(task_data_ptr->n_open_pqueues >= MQ_OPEN_MAX) {
	    REPORT("task has too many queues already open\n");
	    pthread_mutex_unlock(&pqueue_mutex);
	    return -EINVAL;
 	}

	//Look for default queue attributes
	if(mq_attr == NULL) {
	    DBG("using default queue attributes\n");
	    mq_attr = &default_data_queue_attrs;
	}

	//Find a spare descriptor for this queue
	for (q_index = 0; q_index < MAX_PQUEUES; q_index++) {
	    if(rt_pqueue_descr[q_index].q_id == INVALID_PQUEUE) {
		DBG("found spare queue descriptor = %d\n", q_index);

		//Get memory for the data queue
		msg_size = mq_attr->mq_msgsize + sizeof(MSG_HDR);
		queue_size = msg_size * mq_attr->mq_maxmsg;
		mem_ptr = rt_malloc(queue_size);
		if(mem_ptr == NULL) {
		    REPORT("Cannot get data memory for queue %s\n", mq_name);
		    pthread_mutex_unlock(&pqueue_mutex);
		    return -ENOMEM;
		}
		rt_pqueue_descr[q_index].data.base = mem_ptr; 

		//Initialise the Message Queue descriptor
    		rt_pqueue_descr[q_index].owner = _rt_whoami();
    		rt_pqueue_descr[q_index].open_count = 0;
		strcpy(rt_pqueue_descr[q_index].q_name, mq_name);
		rt_pqueue_descr[q_index].q_id = q_index + 1;
		rt_pqueue_descr[q_index].marked_for_deletion = FALSE;

		rt_pqueue_descr[q_index].data.head = 
		    rt_pqueue_descr[q_index].data.tail = 
					rt_pqueue_descr[q_index].data.base;
		rt_pqueue_descr[q_index].data.attrs = *(mq_attr);
		rt_pqueue_descr[q_index].data.attrs.mq_curmsgs = 0;
		rt_pqueue_descr[q_index].permissions = permissions;

		//Initialise conditional variables used for blocking
		pthread_cond_init(&rt_pqueue_descr[q_index].emp_cond, NULL);
		pthread_cond_init(&rt_pqueue_descr[q_index].full_cond, NULL);
		pthread_mutex_init(&rt_pqueue_descr[q_index].mutex, NULL);

		//Clear the queue contents
		initialise_queue(&rt_pqueue_descr[q_index].data);

		//Initialise the Task Queue access descriptor
		q_ind = task_data_ptr->n_open_pqueues++;
		task_data_ptr->q_access[q_ind].q_id = q_index + 1;
		task_data_ptr->q_access[q_ind].oflags = oflags;
	  	DBG("queue is no. %x of %d allowed for this task\n",
			task_data_ptr->n_open_pqueues, MQ_OPEN_MAX);

		break;

	    }  // End if - spare message queue id found

        }  // End for - searching for spare queue id

	if(q_index >= MAX_PQUEUES) {
            REPORT("cannot find spare queue descriptor, aborting...\n");
	    pthread_mutex_unlock(&pqueue_mutex);
  	    return -EMFILE;
	}
	num_pqueues++;

	//Report on the queue creation
	DBG("queue %s of %d created with q_id %d at base %p\n", 
		rt_pqueue_descr[q_index].q_name,
		num_pqueues,
		rt_pqueue_descr[q_index].q_id,
		rt_pqueue_descr[q_index].data.base);
	DBG(" accomodating %ld, %ld byte messages\n", 
		rt_pqueue_descr[q_index].data.attrs.mq_maxmsg,
		(rt_pqueue_descr[q_index].data.attrs.mq_msgsize + 
							sizeof(MSG_HDR)));
    }
    //=================================
    // OPENING A NON-EXISTANT QUEUE?
    //=================================
    else {
	REPORT("need to create queue %s first\n", mq_name);
	pthread_mutex_unlock(&pqueue_mutex);
	return -ENOENT;

    }  // End if, else if, else - creating or opening    

    TRACE_RTAI_POSIX(TRACE_RTAI_EV_POSIX_MQ_OPEN, rt_pqueue_descr[q_index].q_id, 0, 0);

    //Return the message queue's id and mark it as open
    DBG("queue %s, id %d opened\n", rt_pqueue_descr[q_index].q_name,
    				    rt_pqueue_descr[q_index].q_id);
    rt_pqueue_descr[q_index].open_count++;
    pthread_mutex_unlock(&pqueue_mutex);
    return (mqd_t)rt_pqueue_descr[q_index].q_id;

}  // End function - mq_open

// ----------------------------------------------------------------------------
size_t _mq_receive(mqd_t mq, char *msg_buffer, size_t buflen, unsigned int *msgprio, int space)
{
int q_index = mq - 1;
MQMSG *msg_ptr;
uint nBytes;
char *msg_data;
MSG_QUEUE *q;

    DBG("\n");

    TRACE_RTAI_POSIX(TRACE_RTAI_EV_POSIX_MQ_RECV, mq, buflen, 0);

    //Check that the supplied queue id is valid
    if ( 0 <= q_index && q_index < MAX_PQUEUES)
    { 
    	q = &rt_pqueue_descr[q_index];
	DBG("1:head = %x, tail = %x\n", (int)q->data.head, 
					(int)q->data.tail);
	
	//Check that we have read permissions for this queue
	if( can_access(q, FOR_READ) == FALSE) {
	    REPORT("do not have read permissions for queue %s\n", q->q_name);	
	    return -EINVAL;
	}

	//Check if the queue is currently empty
	pthread_mutex_lock(&q->mutex);
    	while (is_empty(&q->data)) {

	    //Do I wanna block?
	    if( is_blocking(q) ) {
	    	DBG("Empty, blocking queue, waiting for a message to arrive\n");
		pthread_cond_wait(&q->emp_cond, &q->mutex);
	    }
	    else {
		DBG("Empty, non-blocking queue, returning\n");
		pthread_mutex_unlock(&q->mutex);
	        return -EAGAIN;
	    }
        }

    	// Point to the front of the queue
    	msg_ptr = q->data.head;
        
	// Check that the reception buffer is large enough to hold a 
	// max size message
	if (buflen < q->data.attrs.mq_msgsize) {
	    pthread_mutex_unlock(&q->mutex);
	    REPORT("allocated buffer is too small %d for the largest allowed message %ld on %s\n", buflen, q->data.attrs.mq_msgsize, q->q_name);
	    return -EMSGSIZE;
	}

	//Get the message off the queue if the allocated buffer is big 
	//enough to hold it 
        if (msg_ptr->hdr.size <= buflen) {
	    nBytes = msg_ptr->hdr.size;

            //Copy data out of the queue into the supplied buffer
	    msg_data = &msg_ptr->data;
	    DBG("getting %d bytes from %x\n", nBytes, (int)msg_data);

		if (space) {
			memcpy(msg_buffer, msg_data, nBytes);
            		// Record the message's priority
			*msgprio = msg_ptr->hdr.priority;
		} else {
			copy_to_user(msg_buffer, msg_data, nBytes);
            		// Record the message's priority
			copy_to_user(msgprio, &msg_ptr->hdr.priority, sizeof(msgprio));
		}

	} else {
	    REPORT("buffer size too small for message on queue\n");
	    nBytes =  ERROR;
	}

 	//Tidy-up the queue
	q->data.head = msg_ptr->hdr.next;
	DBG("2:head = %x, tail = %x\n", (int)q->data.head, 
					(int)q->data.tail);
	if(q->data.head == NULL) {
	    DBG("Queue %s empty, resetting head 'n' tail pointers\n",q->q_name);
	    q->data.head = q->data.tail = q->data.base;
	}

   	//Release this message back into the pool
    	msg_ptr->hdr.in_use = FALSE;
    	msg_ptr->hdr.size   = 0;
    	msg_ptr->hdr.next   = NULL;
	rt_pqueue_descr[q_index].data.attrs.mq_curmsgs--;

	//Unblock any task waiting for space on the queue
	DBG("Waking up blocked task(s)\n");
	pthread_cond_signal(&q->full_cond);
	pthread_mutex_unlock(&q->mutex);
	
	return nBytes;

    }  // End if - queue id is valid
	
    REPORT("invalid queue specifier %d\n", mq);	
    return -EBADF;

}  // End function - mq_receive

// ----------------------------------------------------------------------------
size_t _mq_timedreceive(mqd_t mq, char *msg_buffer, size_t buflen, unsigned int *msgprio, const struct timespec *abstime, int space)
{
	int q_index = mq - 1;
	MQMSG *msg_ptr;
	uint nBytes;
	char *msg_data;
	MSG_QUEUE *q;

	DBG("\n");

	TRACE_RTAI_POSIX(TRACE_RTAI_EV_POSIX_MQ_RECV, mq, buflen, 0);

	//Check that the supplied queue id is valid
	if (q_index < 0 || q_index >= MAX_PQUEUES) { 
		REPORT("invalid queue specifier %d\n", mq);	
		return -EBADF;
	}  // End if - queue id is valid

	q = &rt_pqueue_descr[q_index];
	DBG("1:head = %x, tail = %x\n", (int)q->data.head, (int)q->data.tail);

	//Check that we have read permissions for this queue
	if (can_access(q, FOR_READ) == FALSE) {
		REPORT("do not have read permissions for queue %s\n", q->q_name);	
		return -EINVAL;
	}

	//Check if the queue is currently empty
	pthread_mutex_lock(&q->mutex);
	while (is_empty(&q->data)) {

		//Do I wanna block?
		if (is_blocking(q)) {
			struct timespec time;
			DBG("Empty, blocking queue, waiting for a message to arrive\n");
			if (!space) {
				copy_from_user(&time, abstime, sizeof(struct timespec));
				abstime = &time;
			}
			if (rt_cond_wait_until(&q->emp_cond, &q->mutex, timespec2count(abstime)) > 1) {
				pthread_mutex_unlock(&q->mutex);
				return -ETIMEDOUT;
			}
		} else {
			DBG("Empty, non-blocking queue, returning\n");
			pthread_mutex_unlock(&q->mutex);
			return -EAGAIN;
		}
	}

	// Point to the front of the queue
	msg_ptr = q->data.head;
       
	// Check that the reception buffer is large enough to hold a 
	// max size message
	if (buflen < q->data.attrs.mq_msgsize) {
		pthread_mutex_unlock(&q->mutex);
		REPORT("allocated buffer is too small %d for the largest allowed message %ld on %s\n", buflen, q->data.attrs.mq_msgsize, q->q_name);
		return -EMSGSIZE;
	}

	//Get the message off the queue if the allocated buffer is big 
	//enough to hold it 
	if (msg_ptr->hdr.size <= buflen) {
		nBytes = msg_ptr->hdr.size;

		//Copy data out of the queue into the supplied buffer
		msg_data = &msg_ptr->data;
		DBG("getting %d bytes from %x\n", nBytes, (int)msg_data);

		if (space) {
			memcpy(msg_buffer, msg_data, nBytes);
			// Record the message's priority
			*msgprio = msg_ptr->hdr.priority;
		} else {
			copy_to_user(msg_buffer, msg_data, nBytes);
			// Record the message's priority
			copy_to_user(msgprio, &msg_ptr->hdr.priority, sizeof(msgprio));
		}

	} else {
		REPORT("buffer size too small for message on queue\n");
		nBytes =  ERROR;
	}

	//Tidy-up the queue
	q->data.head = msg_ptr->hdr.next;
	DBG("2:head = %x, tail = %x\n", (int)q->data.head, (int)q->data.tail);
	if(q->data.head == NULL) {
		DBG("Queue %s empty, resetting head 'n' tail pointers\n",q->q_name);
		q->data.head = q->data.tail = q->data.base;
	}

	//Release this message back into the pool
	msg_ptr->hdr.in_use = FALSE;
	msg_ptr->hdr.size   = 0;
	msg_ptr->hdr.next   = NULL;
	rt_pqueue_descr[q_index].data.attrs.mq_curmsgs--;

	//Unblock any task waiting for space on the queue
	DBG("Waking up blocked task(s)\n");
	pthread_cond_signal(&q->full_cond);
	pthread_mutex_unlock(&q->mutex);

	return nBytes;


}  // End function - mq_timedreceive

// ----------------------------------------------------------------------------
int _mq_send(mqd_t mq, const char *msg, size_t msglen, unsigned int msgprio, int space)
{

MSG_QUEUE *q;
int q_index = mq - 1;
MSG_HDR *this_msg;
char *msg_data; 
mq_bool_t q_was_empty = FALSE;

    DBG("\n");

    TRACE_RTAI_POSIX(TRACE_RTAI_EV_POSIX_MQ_SEND, mq, msglen, msgprio);

    //Check that the supplied queue id is valid
    if ( 0 <= q_index && q_index < MAX_PQUEUES)
    { 
    	q = &rt_pqueue_descr[q_index];
	DBG("1:head = %x, tail = %x\n", (int)q->data.head, 
					(int)q->data.tail);

	//Check that we have write permissions for this queue
	if( can_access(q, FOR_WRITE) == FALSE) {
	    REPORT("do not have write permissions for queue %s\n", q->q_name);	
	    return -EINVAL;
	}

	//Check that the requested message priority is valid
	if(msgprio > MQ_PRIO_MAX) {
	    DBG("invalid message priority %d, aborting send\n", msgprio);
	    return -EINVAL;
	}

	//Check if the queue is currently full
	pthread_mutex_lock(&q->mutex);
    	while (is_full(&q->data)) {

	    //Do I wanna block or what?
	    if( is_blocking(q) ) {
		DBG("Full, blocking queue %s waiting for space\n", q->q_name);
		pthread_cond_wait(&q->full_cond, &q->mutex);
	    } 
	    else {
		DBG("Full, non-blocking queue %s returning\n", q->q_name);
		pthread_mutex_unlock(&q->mutex);
	        return -EAGAIN;
	    }
        }

	//Find a spare storage 'slot' on the queue for this message
	if( (this_msg = getnode(&q->data)) == NULL) {
	    pthread_mutex_unlock(&q->mutex);
	    REPORT("cannot put message on queue %s, aborting...\n", q->q_name);
	    return -ENOBUFS;
	}

	//Record whether or not the queue was empty in case we need
	//to notify a task later
	q_was_empty = is_empty(&q->data);

	//Now increment the number of messages on the queue, this is done
	//here in case pre-emption causes overflow
	q->data.attrs.mq_curmsgs++;

	//Check that the message will fit on the queue. If 'msglen' is bigger 
	//than the 'mq_msgsize' attribute of the queue then reject the 
	//request to send it and return an error
	//
	if ( msglen > q->data.attrs.mq_msgsize) {
	    pthread_mutex_unlock(&q->mutex);
	    REPORT("message is too big for queue %s at %d bytes, max size = %ld bytes\n", q->q_name, msglen, q->data.attrs.mq_msgsize);
	    return -EMSGSIZE;
	}

	//Add the message header data to the queued message
	//and then copy the message body into the queue
	this_msg->size = msglen;
	this_msg->priority = msgprio;
	msg_data = &((MQMSG*)this_msg)->data;
	DBG("msg of size %d, prio %d, goes to %x\n", 
		this_msg->size, 
		this_msg->priority,
		(int)msg_data);

	if (space) {
		memcpy(msg_data, msg, msglen);
	} else {
		copy_from_user(msg_data, msg, msglen);
	}

	//Insert this message in the queue according to it's priority
	insert_message(&q->data, this_msg);

	//Unblock any task waiting for data
	DBG("Waking up any blocked tasks\n");
	pthread_cond_signal(&q->emp_cond);

	//Does any task require notification?
	if(q_was_empty && rt_pqueue_descr[q_index].notify.task != NULL) {
	    DBG("notifying about queue %d\n", mq);

	    //TODO: The bit that actually goes here!...........
	    //Need to think about SIGNALS, and the content of struct sigevent
	    //and how these are/not supported under RTAI
	    //...then do some rt_schedule() McHackery...
	    REPORT("notification not implemented yet\n");

	    //Finally, remove the notification
	    rt_pqueue_descr[q_index].notify.task = NULL;

	}
	pthread_mutex_unlock(&q->mutex);
	DBG("sent %d bytes successfully\n", msglen);
	return msglen;

    }  // End if - queue id is valid

    REPORT("invalid queue specifier %d\n", mq);	
    return -EBADF;

}  // End function - mq_send

// ----------------------------------------------------------------------------
int _mq_timedsend(mqd_t mq, const char *msg, size_t msglen, unsigned int msgprio, const struct timespec *abstime, int space)
{
	MSG_QUEUE *q;
	int q_index = mq - 1;
	MSG_HDR *this_msg;
	char *msg_data; 
	mq_bool_t q_was_empty = FALSE;

	DBG("\n");

	TRACE_RTAI_POSIX(TRACE_RTAI_EV_POSIX_MQ_SEND, mq, msglen, msgprio);

	//Check that the supplied queue id is valid
	if (q_index < 0 || q_index >= MAX_PQUEUES) { 
		REPORT("invalid queue specifier %d\n", mq);	
		return -EBADF;
	}  // End if - queue id is valid
	q = &rt_pqueue_descr[q_index];
	DBG("1:head = %x, tail = %x\n", (int)q->data.head, (int)q->data.tail);

	//Check that we have write permissions for this queue
	if (can_access(q, FOR_WRITE) == FALSE) {
		REPORT("do not have write permissions for queue %s\n", q->q_name);	
		return -EINVAL;
	}

	//Check that the requested message priority is valid
	if (msgprio > MQ_PRIO_MAX) {
		DBG("invalid message priority %d, aborting send\n", msgprio);
		return -EINVAL;
	}

	//Check if the queue is currently full
	pthread_mutex_lock(&q->mutex);
	while (is_full(&q->data)) {

		//Do I wanna block or what?
		if (is_blocking(q)) {
			struct timespec time;
			DBG("Full, blocking queue %s waiting for space\n", q->q_name);
			if (!space) {
				copy_from_user(&time, abstime, sizeof(struct timespec));
				abstime = &time;
			}
			if (rt_cond_wait_until(&q->full_cond, &q->mutex, timespec2count(abstime)) > 1) {
				pthread_mutex_unlock(&q->mutex);
				return -ETIMEDOUT;
			}
		} else {
			DBG("Full, non-blocking queue %s returning\n", q->q_name);
			pthread_mutex_unlock(&q->mutex);
			return -EAGAIN;
		}
	}

	//Find a spare storage 'slot' on the queue for this message
	if ((this_msg = getnode(&q->data)) == NULL) {
		pthread_mutex_unlock(&q->mutex);
		REPORT("cannot put message on queue %s, aborting...\n", q->q_name);
		return -ENOBUFS;
	}

	//Record whether or not the queue was empty in case we need
	//to notify a task later
	q_was_empty = is_empty(&q->data);

	//Now increment the number of messages on the queue, this is done
	//here in case pre-emption causes overflow
	q->data.attrs.mq_curmsgs++;

	//Check that the message will fit on the queue. If 'msglen' is bigger 
	//than the 'mq_msgsize' attribute of the queue then reject the 
	//request to send it and return an error
	//
	if (msglen > q->data.attrs.mq_msgsize) {
		pthread_mutex_unlock(&q->mutex);
		REPORT("message is too big for queue %s at %d bytes, max size = %ld bytes\n", q->q_name, msglen, q->data.attrs.mq_msgsize);
		return -EMSGSIZE;
	}

	//Add the message header data to the queued message
	//and then copy the message body into the queue
	this_msg->size = msglen;
	this_msg->priority = msgprio;
	msg_data = &((MQMSG*)this_msg)->data;
	DBG("msg of size %d, prio %d, goes to %x\n", 
	this_msg->size, 
	this_msg->priority,
	(int)msg_data);

	if (space) {
		memcpy(msg_data, msg, msglen);
	} else {
		copy_from_user(msg_data, msg, msglen);
	}

	//Insert this message in the queue according to it's priority
	insert_message(&q->data, this_msg);

	//Unblock any task waiting for data
	DBG("Waking up any blocked tasks\n");
	pthread_cond_signal(&q->emp_cond);

	//Does any task require notification?
	if (q_was_empty && rt_pqueue_descr[q_index].notify.task != NULL) {
		DBG("notifying about queue %d\n", mq);

		//TODO: The bit that actually goes here!...........
		//Need to think about SIGNALS and the content of struct sigevent
		//and how these are/not supported under RTAI
		//...then do some rt_schedule() McHackery...
		REPORT("notification not implemented yet\n");

		//Finally, remove the notification
		rt_pqueue_descr[q_index].notify.task = NULL;

	}
	pthread_mutex_unlock(&q->mutex);
	DBG("sent %d bytes successfully\n", msglen);
	return msglen;

}  // End function - mq_send

// ----------------------------------------------------------------------------
int mq_close(mqd_t mq)
{
int q_index = mq - 1;
int q_ind = 0;
RT_TASK *this_task = _rt_whoami();
struct _pqueue_access_struct *task_queue_data_ptr = NULL;
Z_APPS *zapps;

    DBG("\n");

    TRACE_RTAI_POSIX(TRACE_RTAI_EV_POSIX_MQ_CLOSE, mq, 0, 0);

    //Check that the supplied queue id is valid
    if ( 0 <= q_index && q_index < MAX_PQUEUES)
    { 
	DBG("closing queue %d\n", rt_pqueue_descr[q_index].q_id);
	zapps = (Z_APPS*)this_task->system_data_ptr;
	task_queue_data_ptr = (QUEUE_CTRL)zapps->pqueues;
	if (task_queue_data_ptr == NULL ) {
	    DBG("task has not opened any queues, close request refused\n");
	    return -EINVAL;
	}
	//Find this queue in the task's list of open ones and close it
	pthread_mutex_lock(&pqueue_mutex);
	for (q_ind = 0; q_ind < MQ_OPEN_MAX; q_ind++) {
	    if (task_queue_data_ptr->q_access[q_ind].q_id == mq) {
		task_queue_data_ptr->q_access[q_ind].q_id = INVALID_PQUEUE;
		task_queue_data_ptr->n_open_pqueues--;

		break;
	    }
	}
	if (q_ind == MQ_OPEN_MAX) {
	    DBG("task has not got this queue open, cannot close it\n");
	    pthread_mutex_unlock(&pqueue_mutex);
	    return -EINVAL;
	}

	//Remove notification request, if any, attached to this queue
	//by this task...
	pthread_mutex_lock(&rt_pqueue_descr[q_index].mutex);
	if (rt_pqueue_descr[q_index].notify.task == _rt_whoami()) {
	    rt_pqueue_descr[q_index].notify.task = NULL;
	}

	//Is it time to delete this queue?
        if (--rt_pqueue_descr[q_index].open_count <= 0 &&
	      rt_pqueue_descr[q_index].marked_for_deletion == TRUE ) {
	    DBG("removing queue %d on last close\n", q_index);
	    delete_queue(q_index);	// unlocks and destroys mutex
	}
	else {
            DBG( "queue %d has %d users left\n", 
		 rt_pqueue_descr[q_index].q_id,
		 rt_pqueue_descr[q_index].open_count
		 );
	    pthread_mutex_unlock(&rt_pqueue_descr[q_index].mutex);
	}
	pthread_mutex_unlock(&pqueue_mutex);
        return OK;
    }
    REPORT("invalid queue Id %d\n", mq);	
    return -EINVAL;

}  // End function - mq_close

// ----------------------------------------------------------------------------
int mq_getattr(mqd_t mq, struct mq_attr *attrbuf)
{
//Get the full set of queue attributes...
//
int q_index = mq - 1;

    DBG("\n");

    TRACE_RTAI_POSIX(TRACE_RTAI_EV_POSIX_MQ_GET_ATTR, mq, 0, 0);

    //Check that the supplied queue id is valid
    if ( 0 <= q_index && q_index < MAX_PQUEUES)
    { 
        *attrbuf = rt_pqueue_descr[q_index].data.attrs;
	return OK;
    }
    REPORT("invalid queue specifier %d\n", mq);	
    return -EBADF;

}  // End function - mq_getattr

// ----------------------------------------------------------------------------
int mq_setattr(mqd_t mq, const struct mq_attr *new_attrs,
			 struct mq_attr *old_attrs)
{
//Note that according to the POSIX spec, this function is only allowed
//to change the BLOCKING characteristics of a particular queue. These
//are particular to the task(s) themselves.
//
int q_index = mq - 1;
int q_ind = 0;
RT_TASK *this_task = _rt_whoami();
struct _pqueue_access_struct *task_queue_data_ptr = NULL;
Z_APPS *zapps;

    DBG("\n");

    TRACE_RTAI_POSIX(TRACE_RTAI_EV_POSIX_MQ_SET_ATTR, mq, 0, 0);

    //Check that the supplied queue id is valid
    if ( 0 <= q_index && q_index < MAX_PQUEUES)
    {
	if (old_attrs != NULL) { 
            *old_attrs = rt_pqueue_descr[q_index].data.attrs;
	}

	//Find this queue in the task's list of open ones
	zapps = (Z_APPS*)this_task->system_data_ptr;
	task_queue_data_ptr = (QUEUE_CTRL)zapps->pqueues;
	if (task_queue_data_ptr == NULL) {
	    REPORT("task has not opened any queues!\n");
	    return -EINVAL;
	}

	for (q_ind = 0; q_ind < MQ_OPEN_MAX; q_ind++) {
	    if (task_queue_data_ptr->q_access[q_ind].q_id == mq) {

		if(new_attrs->mq_flags == MQ_NONBLOCK) {
	    	    task_queue_data_ptr->q_access[q_ind].oflags |= O_NONBLOCK;
		}
		else if (new_attrs->mq_flags == MQ_BLOCK) {
	    	    task_queue_data_ptr->q_access[q_ind].oflags &= ~O_NONBLOCK;
		}
		else {
	    	    DBG("invalid new attribute for queue %s\n", 
					rt_pqueue_descr[q_index].q_name);
	    	    return -EINVAL;
		}
                break;
	    }
	}
	if (q_ind == MQ_OPEN_MAX) {
	    REPORT("cannot find queue in task's list of open queues\n");
	    return -EINVAL;
	}
	pthread_mutex_lock(&rt_pqueue_descr[q_index].mutex);
	rt_pqueue_descr[q_index].data.attrs.mq_flags = new_attrs->mq_flags;
	pthread_mutex_unlock(&rt_pqueue_descr[q_index].mutex);
	return OK;
    }
    REPORT("invalid queue specifier %d\n", mq);	
    return -EBADF;

}  // End function - mq_setattr

// ----------------------------------------------------------------------------
int mq_notify(mqd_t mq, const struct sigevent *notification)
{
int q_index = mq - 1;
int rtn;

    DBG("\n");

    TRACE_RTAI_POSIX(TRACE_RTAI_EV_POSIX_MQ_NOTIFY, mq, 0, 0);

    //Check that the supplied queue id is valid
    if ( 0 <= q_index && q_index < MAX_PQUEUES)
    {
	pthread_mutex_lock(&rt_pqueue_descr[q_index].mutex);
	if (notification != NULL) {
	    //Set up a notification request for this task
	    if (rt_pqueue_descr[q_index].notify.task != NULL) {
	        rt_pqueue_descr[q_index].notify.task = _rt_whoami();
	        rt_pqueue_descr[q_index].notify.data = *notification;
	        rtn = OK;
	    }
	    else {
	        REPORT("cannot service notify request, one already pending\n");
	        rtn = ERROR;
	    }
	}
	else {
	    //Clear this task's notification request
	    if (rt_pqueue_descr[q_index].notify.task == _rt_whoami()) {
		rt_pqueue_descr[q_index].notify.task = NULL;
		rtn = OK;
	    }
	    else {
		REPORT("cannot clear notification, owned by another task\n");
		rtn = ERROR;
	    }
	}
	pthread_mutex_unlock(&rt_pqueue_descr[q_index].mutex);
	return rtn;
    }
    REPORT("invalid queue specifier %d\n", mq);	
    return -EBADF;

}  // End function - mq_notify

// ----------------------------------------------------------------------------
int mq_unlink(char *mq_name)
{
int q_index, rtn;

    DBG("\n");

    // Check the queue exists 
    pthread_mutex_lock(&pqueue_mutex);
    q_index = name_to_id(mq_name);
    TRACE_RTAI_POSIX(TRACE_RTAI_EV_POSIX_MQ_UNLINK, q_index, 0, 0);
    if( q_index < 0 ) {

	REPORT("no such queue %s\n", mq_name);
	pthread_mutex_unlock(&pqueue_mutex);
	return -ENOENT;
    }

    // Check if anybody has the queue open
    pthread_mutex_lock(&rt_pqueue_descr[q_index].mutex);
    if(rt_pqueue_descr[q_index].open_count > 0) {

	// At least one task still has the queue open. Therefore we cannot
	// remove it yet, but we can remove the name so that nobody else
	// can open or unlink it. We'll also mark it for deletion so that
	// when the last task closes the queue we can remove it then.
	
	REPORT("cannot unlink queue %s, other tasks have it open\n", mq_name);
	strcpy(rt_pqueue_descr[q_index].q_name, "\0");
	rt_pqueue_descr[q_index].marked_for_deletion = TRUE;
	pthread_mutex_unlock(&rt_pqueue_descr[q_index].mutex);
	rtn = rt_pqueue_descr[q_index].open_count;

    } else {

	DBG("removing queue %s now!\n", mq_name);
	delete_queue(q_index);	// unlocks and destroys mutex
	rtn = OK;
    }

    pthread_mutex_unlock(&pqueue_mutex);
    return rtn;

}  // End function - mq_unlink

///////////////////////////////////////////////////////////////////////////////
//      PROC FILESYSTEM SECTION
///////////////////////////////////////////////////////////////////////////////
#ifdef CONFIG_PROC_FS

// ----------------------------------------------------------------------------
static int pqueue_read_proc(char *page, char **start, off_t off, int count,
			    int *eof, void *data)
{
PROC_PRINT_VARS;
int ind;

    PROC_PRINT("\nRTAI Posix Queue Status\n");
    PROC_PRINT("-----------------------\n\n");
    PROC_PRINT("MAX_PQUEUES = %2d (system wide)\n", MAX_PQUEUES);
    PROC_PRINT("MQ_OPEN_MAX = %2d (per RT task)\n", MQ_OPEN_MAX);
    PROC_PRINT("MQ_NAME_MAX = %d\n", MQ_NAME_MAX);

    PROC_PRINT("\nID  NOpen  NMsgs  MaxMsgs  MaxSz  Perms  Del  Name\n");
    PROC_PRINT("--------------------------------------------------------------------------------\n");
    for (ind = 0; ind < MAX_PQUEUES; ind++) {
	if (rt_pqueue_descr[ind].q_name[0] || rt_pqueue_descr[ind].open_count) {
	    PROC_PRINT( "%-3d %-6d ",
			rt_pqueue_descr[ind].q_id,
			rt_pqueue_descr[ind].open_count
			);
	    PROC_PRINT( "%-6ld %-6ld   %-5ld  ",
		        rt_pqueue_descr[ind].data.attrs.mq_curmsgs,
			rt_pqueue_descr[ind].data.attrs.mq_maxmsg,
			rt_pqueue_descr[ind].data.attrs.mq_msgsize
			);
	    PROC_PRINT( "%-4o   %c    %s\n",
			rt_pqueue_descr[ind].permissions,
			rt_pqueue_descr[ind].marked_for_deletion ? '*' : ' ',
			rt_pqueue_descr[ind].q_name
			);
	}
    }
    PROC_PRINT_DONE;

} // End function - pqueue_read_proc

// ----------------------------------------------------------------------------
static struct proc_dir_entry *proc_rtai_pqueue;

// ----------------------------------------------------------------------------
static int pqueue_proc_register(void)
{
    proc_rtai_pqueue = create_proc_entry("pqueue", 0, rtai_proc_root);
    proc_rtai_pqueue->read_proc = pqueue_read_proc;
    return 0;
} // End function - pqueue_proc_register

// ----------------------------------------------------------------------------
static int pqueue_proc_unregister(void)
{
    remove_proc_entry("pqueue", rtai_proc_root);
    return 0;
} // End function - pqueue_proc_unregister
#endif

///////////////////////////////////////////////////////////////////////////////
//      MODULE CONTROL
///////////////////////////////////////////////////////////////////////////////

//#include <rtai_lxrt.h>

struct rt_native_fun_entry rt_pqueue_entries[] = {
	{ { UR1(1, 5) | UR2(4, 6), mq_open },  	        MQ_OPEN },
        { { 1, _mq_receive },  		                MQ_RECEIVE },
        { { 1, _mq_send },    		                MQ_SEND },
        { { 1, mq_close },                              MQ_CLOSE },
        { { UW1(2, 3), mq_getattr },                    MQ_GETATTR },
        { { UR1(2, 4) | UW1(3, 4), mq_setattr },	MQ_SETATTR },
        { { UR1(2, 3), mq_notify },                     MQ_NOTIFY },
        { { UR1(1, 2), mq_unlink },                     MQ_UNLINK },      
        { { 1, _mq_timedreceive },		  	MQ_TIMEDRECEIVE },
        { { 1, _mq_timedsend }, 	       		MQ_TIMEDSEND },
	{ { 0, 0 },  		      	       		000 }
};

extern int set_rt_fun_entries(struct rt_native_fun_entry *entry);
extern void reset_rt_fun_entries(struct rt_native_fun_entry *entry);

/* +++++++++++++++++++++++++++ END MAIL BOXES +++++++++++++++++++++++++++++++ */
int __rtai_mq_init(void) 
{
	num_pqueues = 0;
	pthread_mutex_init(&pqueue_mutex, NULL);
#ifdef CONFIG_PROC_FS
	pqueue_proc_register();
#endif
	printk(KERN_INFO "RTAI[mq]: loaded.\n");
	return set_rt_fun_entries(rt_pqueue_entries);
	return OK;
}  // End function - init_module

// ----------------------------------------------------------------------------
void __rtai_mq_exit(void) 
{
	pthread_mutex_destroy(&pqueue_mutex);
	reset_rt_fun_entries(rt_pqueue_entries);
#ifdef CONFIG_PROC_FS
	pqueue_proc_unregister();
#endif
	printk(KERN_INFO "RTAI[mq]: unloaded.\n");
}  // End function - cleanup_module
// ---------------------------------< eof >------------------------------------

#ifndef CONFIG_RTAI_MQ_BUILTIN
module_init(__rtai_mq_init);
module_exit(__rtai_mq_exit);
#endif /* !CONFIG_RTAI_MQ_BUILTIN */

#ifdef CONFIG_KBUILD
EXPORT_SYMBOL(mq_open);
EXPORT_SYMBOL(_mq_receive);
EXPORT_SYMBOL(_mq_timedreceive);
EXPORT_SYMBOL(_mq_send);
EXPORT_SYMBOL(_mq_timedsend);
EXPORT_SYMBOL(mq_close);
EXPORT_SYMBOL(mq_getattr);
EXPORT_SYMBOL(mq_setattr);
EXPORT_SYMBOL(mq_notify);
EXPORT_SYMBOL(mq_unlink);
EXPORT_SYMBOL(init_z_apps);
EXPORT_SYMBOL(free_z_apps);
#endif /* CONFIG_KBUILD */
