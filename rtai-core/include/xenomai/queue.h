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

#ifndef _xenomai_queue_h
#define _xenomai_queue_h

#include "xenomai/types.h"

/* Basic element holder */

typedef struct xnholder {

    struct xnholder *next;
    struct xnholder *last;

} xnholder_t;

static inline void inith (xnholder_t *holder) {
    /* Holding queues are doubly-linked and circular */
    holder->last = holder;
    holder->next = holder;
}

static inline void ath (xnholder_t *head,
			xnholder_t *holder) {
    /* Inserts the new element right after the heading one  */
    holder->last = head;
    holder->next = head->next;
    holder->next->last = holder;
    head->next = holder;
}

static inline void dth (xnholder_t *holder) {
    holder->last->next = holder->next;
    holder->next->last = holder->last;
}

/* Basic element queue */

typedef struct xnqueue {

    xnholder_t head;
    int elems;

} xnqueue_t;

static inline void initq (xnqueue_t *qslot) {
    inith(&qslot->head);
    qslot->elems = 0;
}

#ifdef CONFIG_RTAI_XENOMAI_DEBUG

#define XENO_DEBUG_CHECK_QUEUE() \
do { \
    xnholder_t *curr; \
    spl_t s; \
    int nelems = 0; \
    splhigh(s); \
    curr = qslot->head.last; \
    while (curr != &qslot->head && nelems < qslot->elems) \
	curr = curr->last, nelems++; \
    if (curr != &qslot->head || nelems != qslot->elems) \
        xnpod_fatal("corrupted queue, qslot->elems=%d, qslot=%p", \
                    qslot->elems, \
		    qslot); \
    splexit(s); \
} while(0)

#define XENO_DEBUG_INSERT_QUEUE() \
do { \
    xnholder_t *curr; \
    spl_t s; \
    splhigh(s); \
    curr = qslot->head.last; \
    while (curr != &qslot->head && holder != curr) \
	curr = curr->last; \
    if (curr == holder) \
        xnpod_fatal("inserting element twice, holder=%p, qslot=%p", \
		    holder, \
		    qslot); \
    if (holder->last == NULL)	/* Just a guess. */ \
	xnpod_fatal("holder=%p not initialized, qslot=%p", \
		    holder, \
		    qslot); \
    splexit(s); \
} while(0)

#define XENO_DEBUG_REMOVE_QUEUE() \
do { \
    xnholder_t *curr; \
    spl_t s; \
    splhigh(s); \
    curr = qslot->head.last; \
    while (curr != &qslot->head && holder != curr) \
	curr = curr->last; \
    if (curr == &qslot->head) \
	xnpod_fatal("removing non-linked element, holder=%p, qslot=%p", \
		    holder, \
		    qslot); \
    splexit(s); \
} while(0)
#endif /* CONFIG_RTAI_XENOMAI_DEBUG */

static inline int insertq (xnqueue_t *qslot,
			   xnholder_t *head,
			   xnholder_t *holder) {
    /* Insert the <holder> element before <head> */
#ifdef CONFIG_RTAI_XENOMAI_DEBUG
    XENO_DEBUG_CHECK_QUEUE();
    XENO_DEBUG_INSERT_QUEUE();
#endif /*CONFIG_RTAI_XENOMAI_DEBUG */
    ath(head->last,holder);
    return ++qslot->elems;
}

static inline int prependq (xnqueue_t *qslot,
			    xnholder_t *holder) {
    /* Prepend the element to the queue */
#ifdef CONFIG_RTAI_XENOMAI_DEBUG
    XENO_DEBUG_CHECK_QUEUE();
    XENO_DEBUG_INSERT_QUEUE();
#endif /* CONFIG_RTAI_XENOMAI_DEBUG */
    ath(&qslot->head,holder);
    return ++qslot->elems;
}

static inline int appendq (xnqueue_t *qslot,
			   xnholder_t *holder) {
    /* Append the element to the queue */
#ifdef CONFIG_RTAI_XENOMAI_DEBUG
    XENO_DEBUG_CHECK_QUEUE();
    XENO_DEBUG_INSERT_QUEUE();
#endif /* CONFIG_RTAI_XENOMAI_DEBUG */
    ath(qslot->head.last,holder);
    return ++qslot->elems;
}

static inline int removeq (xnqueue_t *qslot,
			   xnholder_t *holder) {
#ifdef CONFIG_RTAI_XENOMAI_DEBUG
    XENO_DEBUG_CHECK_QUEUE();
    XENO_DEBUG_REMOVE_QUEUE();
#endif /* CONFIG_RTAI_XENOMAI_DEBUG */
    dth(holder);
    return --qslot->elems;
}

static inline xnholder_t *getheadq (xnqueue_t *qslot) {
    xnholder_t *holder = qslot->head.next;
    if (holder == &qslot->head) return NULL;
    return holder;
}

static inline xnholder_t *getq (xnqueue_t *qslot) {
    xnholder_t *holder = getheadq(qslot);
    if (holder) removeq(qslot,holder);
    return holder;
}

static inline xnholder_t *nextq (xnqueue_t *qslot,
				 xnholder_t *holder) {
    xnholder_t *nextholder = holder->next;
    return nextholder == &qslot->head ? NULL : nextholder;
}

static inline xnholder_t *popq (xnqueue_t *qslot,
				xnholder_t *holder) {
    xnholder_t *nextholder = nextq(qslot,holder);
    removeq(qslot,holder);
    return nextholder;
}

static inline int countq (xnqueue_t *qslot) {
    return qslot->elems;
}

static inline int moveq (xnqueue_t *dstq, xnqueue_t *srcq) {
    
    xnholder_t *headsrc = srcq->head.next;
    xnholder_t *tailsrc = srcq->head.last->last;
    xnholder_t *headdst = &dstq->head;

    headsrc->last->next = tailsrc->next;
    tailsrc->next->last = headsrc->last;
    headsrc->last = headdst;
    tailsrc->next = headdst->next;
    headdst->next->last = tailsrc;
    headdst->next = headsrc;
    dstq->elems += srcq->elems;
    srcq->elems = 0;

    return dstq->elems;
}

/* Prioritized element holder */

typedef struct xnpholder {

    xnholder_t plink;
    int prio;

} xnpholder_t;

static inline void initph (xnpholder_t *holder) {
    inith(&holder->plink);
    /* Priority is set upon queue insertion */
}

/* Prioritized element queue */

#define xnqueue_up   (-1)
#define xnqueue_down   1

typedef struct xnpqueue {

    xnqueue_t pqueue;
    int qdir;

} xnpqueue_t;

static inline void initpq (xnpqueue_t *pqslot,
			   int qdir) {
    initq(&pqslot->pqueue);
    pqslot->qdir = qdir;
}

static inline int insertpq (xnpqueue_t *pqslot,
			    xnpholder_t *head,
			    xnpholder_t *holder) {
    /* Insert the <holder> element before <head> */
    return insertq(&pqslot->pqueue,&head->plink,&holder->plink);
}

static inline int insertpqf (xnpqueue_t *pqslot,
			     xnpholder_t *holder,
			     int prio) {

    /* Insert the element at the end of its priority group (FIFO) */

    xnholder_t *curr;

    if (pqslot->qdir == xnqueue_down) {
    	for (curr = pqslot->pqueue.head.last;
	     curr != &pqslot->pqueue.head; curr = curr->last) {
		if (prio <= ((xnpholder_t *)curr)->prio)
		    break;
	}
    }
    else {
    	for (curr = pqslot->pqueue.head.last;
	     curr != &pqslot->pqueue.head; curr = curr->last) {
		if (prio >= ((xnpholder_t *)curr)->prio)
		    break;
	}
    }

    holder->prio = prio;

    return insertq(&pqslot->pqueue,curr->next,&holder->plink);
}

static inline int insertpql (xnpqueue_t *pqslot,
			     xnpholder_t *holder,
			     int prio) {

    /* Insert the element at the front of its priority group (LIFO) */

    xnholder_t *curr;

    if (pqslot->qdir == xnqueue_down) {
    	for (curr = pqslot->pqueue.head.next;
	     curr != &pqslot->pqueue.head; curr = curr->next) {
		if (prio >= ((xnpholder_t *)curr)->prio)
		    break;
	}
    }
    else {
    	for (curr = pqslot->pqueue.head.next;
	     curr != &pqslot->pqueue.head; curr = curr->next) {
		if (prio <= ((xnpholder_t *)curr)->prio)
		    break;
	}
    }

    holder->prio = prio;

    return insertq(&pqslot->pqueue,curr,&holder->plink);
}

static inline xnpholder_t *findpqh (xnpqueue_t *pqslot,
				    int prio) {

    /* Find the element heading a given priority group */

    xnholder_t *curr;

    if (pqslot->qdir == xnqueue_down) {
    	for (curr = pqslot->pqueue.head.next;
	     curr != &pqslot->pqueue.head; curr = curr->next) {
		if (prio >= ((xnpholder_t *)curr)->prio)
		    break;
	}
    }
    else {
    	for (curr = pqslot->pqueue.head.next;
	     curr != &pqslot->pqueue.head; curr = curr->next) {
		if (prio <= ((xnpholder_t *)curr)->prio)
		    break;
	}
    }

    if (curr && ((xnpholder_t *)curr)->prio == prio)
	return (xnpholder_t *)curr;

    return NULL;
}

static inline int appendpq (xnpqueue_t *pqslot,
			    xnpholder_t *holder) {
    holder->prio = 0;
    return appendq(&pqslot->pqueue,&holder->plink);
}

static inline int prependpq (xnpqueue_t *pqslot,
			     xnpholder_t *holder) {
    holder->prio = 0;
    return prependq(&pqslot->pqueue,&holder->plink);
}

static inline int removepq (xnpqueue_t *pqslot,
			    xnpholder_t *holder) {
    return removeq(&pqslot->pqueue,&holder->plink);
}

static inline xnpholder_t *getheadpq (xnpqueue_t *pqslot) {
    return (xnpholder_t *)getheadq(&pqslot->pqueue);
}

static inline xnpholder_t *nextpq (xnpqueue_t *pqslot,
				   xnpholder_t *holder) {
    return (xnpholder_t *)nextq(&pqslot->pqueue,&holder->plink);
}

static inline xnpholder_t *getpq (xnpqueue_t *pqslot) {
    return (xnpholder_t *)getq(&pqslot->pqueue);
}

static inline xnpholder_t *poppq (xnpqueue_t *pqslot,
				  xnpholder_t *holder) {
    return (xnpholder_t *)popq(&pqslot->pqueue,&holder->plink);
}

static inline int countpq (xnpqueue_t *pqslot) {
    return countq(&pqslot->pqueue);
}

/* Generic prioritized element holder */

typedef struct xngholder {

    xnpholder_t glink;
    void *data;

} xngholder_t;

static inline void initgh (xngholder_t *holder, void *data) {
    inith(&holder->glink.plink);
    holder->data = data;
}

/* Generic element queue */

typedef struct xngqueue {

    xnpqueue_t gqueue;
    xnqueue_t *freehq;
    void (*starvation)(xnqueue_t *);
    int threshold;

} xngqueue_t;

static inline void initgq (xngqueue_t *gqslot,
			   xnqueue_t *freehq,
			   void (*starvation)(xnqueue_t *),
			   int threshold,
			   int qdir) {
    initpq(&gqslot->gqueue,qdir);
    gqslot->freehq = freehq;
    gqslot->starvation = starvation;
    gqslot->threshold = threshold;
}

static inline xngholder_t *allocgh (xngqueue_t *gqslot) {

    if (countq(gqslot->freehq) < gqslot->threshold)
	gqslot->starvation(gqslot->freehq);

    return (xngholder_t *)getq(gqslot->freehq);
}

static inline void *removegh (xngqueue_t *gqslot,
			      xngholder_t *holder) {
    removepq(&gqslot->gqueue,&holder->glink);
    appendq(gqslot->freehq,&holder->glink.plink);
    return holder->data;
}

static inline int insertgqf (xngqueue_t *gqslot,
			     void *data,
			     int prio) {
    xngholder_t *holder = allocgh(gqslot);
    holder->data = data;
    return insertpqf(&gqslot->gqueue,&holder->glink,prio);
}

static inline int insertgql (xngqueue_t *gqslot,
			     void *data,
			     int prio) {
    xngholder_t *holder = allocgh(gqslot);
    holder->data = data;
    return insertpql(&gqslot->gqueue,&holder->glink,prio);
}

static inline int appendgq (xngqueue_t *gqslot,
			    void *data) {
    xngholder_t *holder = allocgh(gqslot);
    holder->data = data;
    return appendpq(&gqslot->gqueue,&holder->glink);
}

static inline int prependgq (xngqueue_t *gqslot,
			     void *data) {
    xngholder_t *holder = allocgh(gqslot);
    holder->data = data;
    return prependpq(&gqslot->gqueue,&holder->glink);
}

static inline xngholder_t *getheadgq (xngqueue_t *gqslot) {
    return (xngholder_t *)getheadpq(&gqslot->gqueue);
}

static inline xngholder_t *nextgq (xngqueue_t *gqslot,
				   xngholder_t *holder) {
    return (xngholder_t *)nextpq(&gqslot->gqueue,&holder->glink);
}

static inline void *getgq (xngqueue_t *gqslot) {
    xngholder_t *holder = getheadgq(gqslot);
    if (!holder) return NULL;
    appendq(gqslot->freehq,&getpq(&gqslot->gqueue)->plink);
    return holder->data;
}

static inline xngholder_t *popgq (xngqueue_t *gqslot,
				  xngholder_t *holder) {
    xngholder_t *nextholder = nextgq(gqslot,holder);
    removegh(gqslot,holder);
    return nextholder;
}

static inline xngholder_t *findgq (xngqueue_t *gqslot,
				   void *data) {
    xnholder_t *holder;

    for (holder = gqslot->gqueue.pqueue.head.next;
	 holder != &gqslot->gqueue.pqueue.head; holder = holder->next) {
         if (((xngholder_t *)holder)->data == data)
	     return (xngholder_t *)holder;
	}

    return NULL;
}

static inline void *removegq (xngqueue_t *gqslot,
			      void *data) {
    xngholder_t *holder = findgq(gqslot,data);
    return holder ? removegh(gqslot,holder) : NULL;
}

static inline int countgq (xngqueue_t *gqslot) {
    return countpq(&gqslot->gqueue);
}

#endif /* !_xenomai_queue_h */
