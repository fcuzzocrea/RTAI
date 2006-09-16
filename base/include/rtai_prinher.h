/*
 * Copyright (C) 2006 Paolo Mantegazza <mantegazza@aero.polimi.it>
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


#ifndef _RTAI_PRINHER_H
#define _RTAI_PRINHER_H

#include <rtai_schedcore.h>

#ifdef __KERNEL__

#ifdef CONFIG_RTAI_FULL_PRINHER

#define task_owns_sems(task)  ((task)->resq.next != &(task)->resq)

static inline void enqueue_resqel(QUEUE *resqel, RT_TASK *resownr)
{
	QUEUE *resq;
	resqel->next = resq = &resownr->resq;
	(resqel->prev = resq->prev)->next = resqel;
	resq->prev = resqel;
}

#define enqueue_resqtsk(resownr)

#define RESQEL_TASK ((((QUEUE *)resqel->task)->next)->task)

static inline int _set_task_prio_from_resq(RT_TASK *resownr)
{
	int hprio;
	RT_TASK *task;
	QUEUE *resq, *resqel;
	hprio = resownr->base_priority;
	resqel = resq = &resownr->resq;
	while ((resqel = resqel->next) != resq && (task = RESQEL_TASK) && task->priority < hprio) {
		hprio = task->priority;
	}
	return hprio;
}

static inline int dequeue_resqel_reset_task_priority(QUEUE *resqel, RT_TASK *resownr)
{
	int hprio, prio;
	QUEUE *q;
	(resqel->prev)->next = resqel->next;
	(resqel->next)->prev = resqel->prev;
	hprio = _set_task_prio_from_resq(resownr);
	return renq_ready_task(resownr, ((q = resownr->msg_queue.next) != &resownr->msg_queue && (prio = (q->task)->priority) < hprio) ? prio : hprio);
}

static inline int dequeue_resqel_reset_current_priority(QUEUE *resqel, RT_TASK *rt_current)
{
	int hprio, prio;
	QUEUE *q;
	(resqel->prev)->next = resqel->next;
	(resqel->next)->prev = resqel->prev;
	hprio = _set_task_prio_from_resq(rt_current);
	return renq_current(rt_current, ((q = rt_current->msg_queue.next) != &rt_current->msg_queue && (prio = (q->task)->priority) < hprio) ? prio : hprio);
}

static inline int set_task_prio_from_resq(RT_TASK *resownr)
{
	int hprio, prio;
	QUEUE *q;
	hprio = _set_task_prio_from_resq(resownr);
	return renq_ready_task(resownr, ((q = resownr->msg_queue.next) != &resownr->msg_queue && (prio = (q->task)->priority) < hprio) ? prio : hprio);
}

static inline int set_current_prio_from_resq(RT_TASK *rt_current)
{
	int hprio, prio;
	QUEUE *q;
	hprio = _set_task_prio_from_resq(rt_current);
	return renq_current(rt_current, ((q = rt_current->msg_queue.next) != &rt_current->msg_queue && (prio = (q->task)->priority) < hprio) ? prio : hprio);
}

#else /* !CONFIG_RTAI_FULL_PRINHER */

#define task_owns_sems(task)  ((task)->owndres)

#define enqueue_resqel(resqel, task) \
	do { (task)->owndres++; } while (0)

#define enqueue_resqtsk(task)
//	do { (task)->owndres += RPCINC; } while (0)

static inline int _set_task_prio_from_resq(RT_TASK *resownr)
{
	QUEUE *q;
	int prio;
	return renq_ready_task(resownr, ((q = resownr->msg_queue.next) != &resownr->msg_queue && (prio = (q->task)->priority) < resownr->base_priority) ? prio : resownr->base_priority);
}

static inline int _set_current_prio_from_resq(RT_TASK *rt_current)
{
	QUEUE *q;
	int prio;
	return renq_ready_task(rt_current, ((q = rt_current->msg_queue.next) != &rt_current->msg_queue && (prio = (q->task)->priority) < rt_current->base_priority) ? prio : rt_current->base_priority);
}

static inline int dequeue_resqel_reset_task_priority(QUEUE *resqel, RT_TASK *resownr)
{
	if (--resownr->owndres <= 0) {
		resownr->owndres = 0;
		return _set_task_prio_from_resq(resownr);
	}
	return 0;
}

static inline int dequeue_resqel_reset_current_priority(QUEUE *resqel, RT_TASK *rt_current)
{
	if (--rt_current->owndres <= 0) {
		rt_current->owndres = 0;
		return _set_current_prio_from_resq(rt_current);
	}
	return 0;
}

static inline int set_task_prio_from_resq(RT_TASK *resownr)
{
	return !resownr->owndres ? _set_task_prio_from_resq(resownr) : 0;
}

static inline int set_current_prio_from_resq(RT_TASK *rt_current)
{
	return !rt_current->owndres ? _set_current_prio_from_resq(rt_current) : 0;
}

#endif /* CONFIG_RTAI_FULL_PRINHER */

#define task_owns_msgs(task)  ((task)->msg_queue.next != &(task)->msg_queue)
#define task_owns_res(task)   (task_owns_sems(task) || task_owns_msgs(task))

#endif /* __KERNEL__ */

#endif /* !_RTAI_PRINHER_H */
