/*
 * pqueues interface for Real Time Linux.
 *
 * Copyright (©) 1999 Zentropic Computing, All rights reserved
 *  
 * Authors:         Trevor Woolven (trevw@zentropix.com)
 *
 * Original date:   Thu 15 Jul 1999
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

#ifndef _RTAI_MQ_H
#define _RTAI_MQ_H

#include <rtai_sem.h>

#define	MQ_OPEN_MAX	8	/* Maximum number of message queues per process */
#define	MQ_PRIO_MAX	32	/* Maximum number of message priorities */
#define	MQ_BLOCK	0	/* Flag to set queue into blocking mode */
#define	MQ_NONBLOCK	1	/* Flag to set queue into non-blocking mode */
#define MQ_NAME_MAX	80	/* Maximum length of a queue name string */

#define MQ_MIN_MSG_PRIORITY 0		/* Lowest priority message */
#define MQ_MAX_MSG_PRIORITY MQ_PRIO_MAX /* Highest priority message */

#define MAX_PQUEUES     4       /* Maximum number of message queues in module */
#define MAX_MSGSIZE     50      /* Maximum message size per queue (bytes) */
#define MAX_MSGS        10      /* Maximum number of messages per queue */
#define MAX_BLOCKED_TASKS 10    /* Maximum number of tasks blocked on a */
                                /* queue at any one time  */
#define MSG_HDR_SIZE	16	/* Note that this is hard-coded (urgh!) ensure */
				/*  it always matches pqueues sizeof(MSG_HDR) */ 
				/*  or do it a better way! (sic) */
typedef enum {
    FIFO_BASED,
    PRIORITY_BASED
} QUEUEING_POLICY;

typedef enum {
    POSIX,
    VxWORKS
} QUEUE_TYPE;

typedef struct mq_attr {
    long mq_maxmsg;		/* Maximum number of messages in queue */
    long mq_msgsize;		/* Maximum size of a message (in bytes) */
    long mq_flags;		/* Blocking/Non-blocking behaviour specifier */
    long mq_curmsgs;		/* Number of messages currently in queue */
} MQ_ATTR;

typedef unsigned long mqd_t;

#define	INVALID_PQUEUE	0

#ifdef __KERNEL__

#ifdef CONFIG_RTAI_MQ_BUILTIN
#define MQ_INIT_MODULE     mq_init_module
#define MQ_CLEANUP_MODULE  mq_cleanup_module
#else  /* !CONFIG_RTAI_MQ_BUILTIN */
#define MQ_INIT_MODULE     init_module
#define MQ_CLEANUP_MODULE  cleanup_module
#endif /* CONFIG_RTAI_MQ_BUILTIN */

#ifndef __cplusplus

typedef int mq_bool_t;

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

typedef struct msg_hdr {
    mq_bool_t in_use;
    size_t size;		/* Actual message size */
    uint priority;		/* Usage priority (message/task) */
    void *next;			/* Pointer to next message on queue */
} MSG_HDR;

typedef struct queue_control {
    void *base;		/* Pointer to the base of the queue in memory */
    void *head;		/* Pointer to the element at the front of the queue */
    void *tail;		/* Pointer to the element at the back of the queue */
    MQ_ATTR attrs;	/* Queue attributes */
} Q_CTRL;

typedef struct msg {
    MSG_HDR hdr;
    char data;		/* Anchor point for message data */
} MQMSG;

struct notify {
    RT_TASK *task;
    struct sigevent data;
};

typedef struct _pqueue_descr_struct {
    RT_TASK *owner;		/* Task that created the queue */
    int open_count;		/* Count of the number of tasks that have */
				/*  'opened' the queue for access */
    char q_name[MQ_NAME_MAX];	/* Name supplied for queue */
    uint q_id;			/* Queue Id (index into static list of queues) */
    mq_bool_t marked_for_deletion;	/* Queue can be deleted once all tasks have  */
				/*  closed it	*/
    Q_CTRL data;		/* Data queue (real messages) */
    mode_t permissions;		/* Permissions granted by creator (ugo, rwx) */
    struct notify notify;	/* Notification data (empty -> !empty) */
    SEM emp_cond;		/* For blocking on empty queue */
    SEM full_cond;		/* For blocking on full queue */
    SEM mutex;			/* For synchronisation of queue */
} MSG_QUEUE;

struct _pqueue_access_data {
    int q_id;
    int oflags;			/* Queue access permissions & blocking spec */
};

typedef struct _pqueue_access_struct {
    RT_TASK *this_task;
    int n_open_pqueues;
    struct _pqueue_access_data q_access[MQ_OPEN_MAX];
} *QUEUE_CTRL;

typedef enum {
    FOR_READ,
    FOR_WRITE
} Q_ACCESS;

/*
 * a) A single Posix queue ( (MAX_MSGSIZE + sizeof(MSG_HDR) * MAX_MSGS) ) or 
 * b) A blocked tasks queue (MAX_BLOCKED_TASKS * sizeof(MSG_HDR) ) or
 * c) A Zentropix application data staging structure (sizeof(Z_APPS))
 * 
 * It is assumed that the first two are both bigger than a Z_APPS structure
 * and so the choice is made between a) and b).
 *
 * Note that one control mechanism is used to allocate memory 'chunks' for a
 * number of different application uses. This means that if the 'chunk' size
 * becomes large in relation to the amount of memory required by one or other
 * of these applications, memory usage becomes wasteful.
 *
 * Set of pointers to Application-Specific extensions to RTAI
 * such as POSIX Threads, POSIX Queues, VxWorks Compatibility Library, etc
 */

typedef struct z_apps {
    int in_use_count;	// Incremented whenever an application is initialised
    void *pthreads;
    void *pqueues;
    void *vxtasks;
			// anticipate... pclocks, psosTasks,
} Z_APPS;

#else /* __cplusplus */
extern "C" {
#endif /* !__cplusplus */

int MQ_INIT_MODULE(void);

void MQ_CLEANUP_MODULE(void);

QUEUEING_POLICY get_task_queueing_policy(void);

QUEUEING_POLICY set_task_queuing_policy(QUEUEING_POLICY policy);

QUEUE_TYPE get_queue_type(void);

QUEUE_TYPE set_queue_type(QUEUE_TYPE type);

void *init_z_apps(void *this_task);

void free_z_apps(void *this_task);

mqd_t mq_open(char *mq_name,
	      int oflags,
	      mode_t permissions,
	      struct mq_attr *mq_attr);

size_t mq_receive(mqd_t mq,
		  char *msg_buffer,
		  size_t buflen,
		  unsigned int *msgprio);

int mq_send(mqd_t mq,
	    const char *msg,
	    size_t msglen,
	    unsigned int msgprio);

int mq_close(mqd_t mq);

int mq_getattr(mqd_t mq,
	       struct mq_attr *attrbuf);

int mq_setattr(mqd_t mq,
	       const struct mq_attr *new_attrs,
	       struct mq_attr *old_attrs);

int mq_notify(mqd_t mq,
	      const struct sigevent *notification);

int mq_unlink(char *mq_name);

size_t mq_timedreceive(mqd_t mq,
		       char *msg_buffer,
		       size_t buflen,
		       unsigned int *msgprio,
		       const struct timespec *abstime);

int mq_timedsend(mqd_t mq,
		 const char *msg,
		 size_t msglen,
		 unsigned int msgprio,
		 const struct timespec *abstime);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#else /* !__KERNEL__ */

#include <signal.h>
#include <rtai_lxrt.h>

#define MQIDX  0

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

RTAI_PROTO(mqd_t, mq_open,(char *mq_name, int oflags, mode_t permissions, struct mq_attr *mq_attr))
{
	struct {char *mq_name; int oflags; mode_t permissions; struct mq_attr *mq_attr; int namesize, attrsize; } arg = { mq_name, oflags, permissions, mq_attr, strlen(mq_name) + 1, sizeof(struct mq_attr) };
	return (mqd_t)rtai_lxrt(MQIDX, SIZARG, MQ_OPEN, &arg).i[LOW];
}

RTAI_PROTO(size_t, mq_receive,(mqd_t mq, char *msg_buffer, size_t buflen, unsigned int *msgprio))
{
	struct { mqd_t mq; char *msg_buffer; size_t buflen; unsigned int *msgprio; } arg = { mq, msg_buffer, buflen, msgprio };
	return (size_t)rtai_lxrt(MQIDX, SIZARG, MQ_RECEIVE, &arg).i[LOW];
}

RTAI_PROTO(int, mq_send,(mqd_t mq, const char *msg, size_t msglen, unsigned int msgprio))
{
	struct { mqd_t mq; const char *msg; size_t msglen; unsigned int msgprio; } arg = { mq, msg, msglen, msgprio };
	return rtai_lxrt(MQIDX, SIZARG, MQ_SEND, &arg).i[LOW];
}

RTAI_PROTO(int, mq_close,(mqd_t mq))
{
	struct { mqd_t mq; } arg = { mq };
	return rtai_lxrt(MQIDX, SIZARG, MQ_CLOSE, &arg).i[LOW];
}

RTAI_PROTO(int, mq_getattr,(mqd_t mq, struct mq_attr *attrbuf))
{
	struct { mqd_t mq; struct mq_attr *attrbuf; int attrsize; } arg = { mq, attrbuf, sizeof(struct mq_attr) };
	return rtai_lxrt(MQIDX, SIZARG, MQ_GETATTR, &arg).i[LOW];
}

RTAI_PROTO(int, mq_setattr,(mqd_t mq, const struct mq_attr *new_attrs, struct mq_attr *old_attrs))
{
	struct { mqd_t mq; const struct mq_attr *new_attrs; struct mq_attr *old_attrs; int attrsize; } arg = { mq, new_attrs, old_attrs, sizeof(struct mq_attr) };
	return rtai_lxrt(MQIDX, SIZARG, MQ_SETATTR, &arg).i[LOW];
}

RTAI_PROTO(int, mq_notify,(mqd_t mq, const struct sigevent *notification))
{
	struct { mqd_t mq; const struct sigevent *notification; int size; } arg = { mq, notification, sizeof(struct sigevent) };
	return rtai_lxrt(MQIDX, SIZARG, MQ_NOTIFY, &arg).i[LOW];
}

RTAI_PROTO(int, mq_unlink,(char *mq_name))
{
	struct { char *mq_name; int size; } arg = { mq_name, strlen(mq_name) + 1};
	return rtai_lxrt(MQIDX, SIZARG, MQ_UNLINK, &arg).i[LOW];
}

RTAI_PROTO(size_t, mq_timedreceive,(mqd_t mq, char *msg_buffer, size_t buflen, unsigned int *msgprio, const struct timespec *abstime))
{
	struct { mqd_t mq; char *msg_buffer; size_t buflen; unsigned int *msgprio; const struct timespec *abstime; int size; } arg = { mq, msg_buffer, buflen, msgprio, abstime, sizeof(struct timespec) };
	return (size_t)rtai_lxrt(MQIDX, SIZARG, MQ_TIMEDRECEIVE, &arg).i[LOW];
}

RTAI_PROTO(int, mq_timedsend,(mqd_t mq, const char *msg, size_t msglen, unsigned int msgprio, const struct timespec *abstime))
{
	struct { mqd_t mq; const char *msg; size_t msglen; unsigned int msgprio; const struct timespec *abstime; int size; } arg = { mq, msg, msglen, msgprio, abstime, sizeof(struct timespec) };
	return rtai_lxrt(MQIDX, SIZARG, MQ_TIMEDSEND, &arg).i[LOW];
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __KERNEL__ */

#endif  /* !_RTAI_MQ_H */
