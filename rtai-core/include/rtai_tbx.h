/*
 * Copyright (C) 2001  G.M. Bertani <gmbertani@yahoo.it>
 * Copyright (C) 2002  P. Mantegazza <mantegazza@aero.polimi.it>
 *		         (LXRT extensions).
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

#ifndef _RTAI_TBX_H
#define _RTAI_TBX_H

#include <rtai_types.h>

/* TYPED MAILBOXES */

#define RT_TBX_MAGIC 0x6e93ad4b

#define TYPE_NONE      0x00
#define TYPE_NORMAL    0x01
#define TYPE_BROADCAST 0x02
#define TYPE_URGENT    0x04

#ifdef __KERNEL__

#ifdef CONFIG_RTAI_TBX_BUILTIN
#define TBX_INIT_MODULE     tbx_init_module
#define TBX_CLEANUP_MODULE  tbx_cleanup_module
#else  /* !CONFIG_RTAI_TBX_BUILTIN */
#define TBX_INIT_MODULE     init_module
#define TBX_CLEANUP_MODULE  cleanup_module
#endif /* CONFIG_RTAI_TBX_BUILTIN */

struct rt_typed_mailbox;

#ifndef __cplusplus

#include <rtai_sem.h>

typedef struct rt_typed_mailbox {

    int magic;
    int waiting_nr;   /* number of tasks waiting for a broadcast */
    SEM sndsmx, rcvsmx;
    SEM bcbsmx;       /* binary sem needed to wakeup the sleeping tasks 
                      when the broadcasting of a message is terminated */
    RT_TASK *waiting_task;
    char *bufadr;     /* mailbox buffer */
    char *bcbadr;     /* broadcasting buffer */
    int size;         /* mailbox size */
    int fbyte;        /* circular buffer read pointer */
    int avbs;         /* bytes occupied */
    int frbs;         /* bytes free */
    spinlock_t buflock;

} TBX;

#else /* __cplusplus */
extern "C" {
#endif /* !__cplusplus */

int TBX_INIT_MODULE(void);

void TBX_CLEANUP_MODULE(void);

/*
 * send_wp and receive_wp are not implemented because 
 * the packed message must be sent/received atomically
 */ 

int rt_tbx_init(struct rt_typed_mailbox *tbx,
		int size,
		int flags);

int rt_tbx_delete(struct rt_typed_mailbox *tbx);

int rt_tbx_send(struct rt_typed_mailbox *tbx,
		void *msg,
		int msg_size);

int rt_tbx_send_if(struct rt_typed_mailbox *tbx,
		   void *msg,
		   int msg_size);

int rt_tbx_send_until(struct rt_typed_mailbox *tbx,
		      void *msg,
		      int msg_size,
		      RTIME time);

int rt_tbx_send_timed(struct rt_typed_mailbox *tbx,
		      void *msg,
		      int msg_size,
		      RTIME delay);

int rt_tbx_receive(struct rt_typed_mailbox *tbx,
		   void *msg,
		   int msg_size);

int rt_tbx_receive_if(struct rt_typed_mailbox *tbx,
		      void *msg,
		      int msg_size);

int rt_tbx_receive_until(struct rt_typed_mailbox *tbx,
			 void *msg,
			 int msg_size,
			 RTIME time);

int rt_tbx_receive_timed(struct rt_typed_mailbox *tbx,
			 void *msg,
			 int msg_size,
			 RTIME delay);

int rt_tbx_broadcast(struct rt_typed_mailbox *tbx,
		     void *msg,
		     int msg_size);

int rt_tbx_broadcast_if(struct rt_typed_mailbox *tbx,
			void *msg,
			int msg_size);

int rt_tbx_broadcast_until(struct rt_typed_mailbox *tbx,
			   void *msg,
			   int msg_size,
			   RTIME time);

int rt_tbx_broadcast_timed(struct rt_typed_mailbox *tbx,
			   void *msg,
			   int msg_size,
			   RTIME delay);

int rt_tbx_urgent(struct rt_typed_mailbox *tbx,
		  void *msg,
		  int msg_size);

int rt_tbx_urgent_if(struct rt_typed_mailbox *tbx,
		     void *msg,
		     int msg_size);

int rt_tbx_urgent_until(struct rt_typed_mailbox *tbx,
			void *msg,
			int msg_size,
			RTIME time);

int rt_tbx_urgent_timed(struct rt_typed_mailbox *tbx,
			void *msg,
			int msg_size,
			RTIME delay);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#else /* !__KERNEL__ */

#include <rtai_lxrt.h>

#define TBXIDX 0

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

RTAI_PROTO(struct rt_typed_mailbox *, rt_tbx_init,(unsigned long name, int size, int flags))
{
	struct { unsigned long name; int size; int flags; } arg = { name, size, flags };
	return (struct rt_typed_mailbox *)rtai_lxrt(TBXIDX, SIZARG, TBX_INIT, &arg).v[LOW];
}

RTAI_PROTO(int, rt_tbx_delete,(struct rt_typed_mailbox *tbx))
{
	struct { struct rt_typed_mailbox *tbx; } arg = { tbx };
	return rtai_lxrt(TBXIDX, SIZARG, TBX_DELETE, &arg).i[LOW];
}

RTAI_PROTO(int, rt_tbx_send,(struct rt_typed_mailbox *tbx, void *msg, int msg_size))
{
	struct { struct rt_typed_mailbox *tbx; void *msg; int msg_size; } arg = { tbx, msg, msg_size };
	return rtai_lxrt(TBXIDX, SIZARG, TBX_SEND, &arg).i[LOW];
}

RTAI_PROTO(int, rt_tbx_send_if,(struct rt_typed_mailbox *tbx, void *msg, int msg_size))
{
	struct { struct rt_typed_mailbox *tbx; void *msg; int msg_size; } arg = { tbx, msg, msg_size };
	return rtai_lxrt(TBXIDX, SIZARG, TBX_SEND_IF, &arg).i[LOW];
}

RTAI_PROTO(int, rt_tbx_send_until,(struct rt_typed_mailbox *tbx, void *msg, int msg_size, RTIME time))
{
	struct { struct rt_typed_mailbox *tbx; void *msg; int msg_size; RTIME time; } arg = { tbx, msg, msg_size, time };
	return rtai_lxrt(TBXIDX, SIZARG, TBX_SEND_UNTIL, &arg).i[LOW];
}

RTAI_PROTO(int, rt_tbx_send_timed,(struct rt_typed_mailbox *tbx, void *msg, int msg_size, RTIME delay))
{
	struct { struct rt_typed_mailbox *tbx; void *msg; int msg_size; RTIME delay; } arg = { tbx, msg, msg_size, delay };
	return rtai_lxrt(TBXIDX, SIZARG, TBX_SEND_TIMED, &arg).i[LOW];
}

RTAI_PROTO(int, rt_tbx_receive,(struct rt_typed_mailbox *tbx, void *msg, int msg_size))
{
	struct { struct rt_typed_mailbox *tbx; void *msg; int msg_size; } arg = { tbx, msg, msg_size };
	return rtai_lxrt(TBXIDX, SIZARG, TBX_RECEIVE, &arg).i[LOW];
}

RTAI_PROTO(int, rt_tbx_receive_if,(struct rt_typed_mailbox *tbx, void *msg, int msg_size))
{
	struct { struct rt_typed_mailbox *tbx; void *msg; int msg_size; } arg = { tbx, msg, msg_size };
	return rtai_lxrt(TBXIDX, SIZARG, TBX_RECEIVE_IF, &arg).i[LOW];
}

RTAI_PROTO(int, rt_tbx_receive_until,(struct rt_typed_mailbox *tbx, void *msg, int msg_size, RTIME time))
{
	struct { struct rt_typed_mailbox *tbx; void *msg; int msg_size; RTIME time; } arg = { tbx, msg, msg_size, time };
	return rtai_lxrt(TBXIDX, SIZARG, TBX_RECEIVE_UNTIL, &arg).i[LOW];
}

RTAI_PROTO(int, rt_tbx_receive_timed,(struct rt_typed_mailbox *tbx, void *msg, int msg_size, RTIME delay))
{
	struct { struct rt_typed_mailbox *tbx; void *msg; int msg_size; RTIME delay; } arg = { tbx, msg, msg_size, delay };
	return rtai_lxrt(TBXIDX, SIZARG, TBX_RECEIVE_TIMED, &arg).i[LOW];
}

RTAI_PROTO(int, rt_tbx_broadcast,(struct rt_typed_mailbox *tbx, void *msg, int msg_size))
{
	struct { struct rt_typed_mailbox *tbx; void *msg; int msg_size; } arg = { tbx, msg, msg_size };
	return rtai_lxrt(TBXIDX, SIZARG, TBX_BROADCAST, &arg).i[LOW];
}

RTAI_PROTO(int, rt_tbx_broadcast_if,(struct rt_typed_mailbox *tbx, void *msg, int msg_size))
{
	struct { struct rt_typed_mailbox *tbx; void *msg; int msg_size; } arg = { tbx, msg, msg_size };
	return rtai_lxrt(TBXIDX, SIZARG, TBX_BROADCAST_IF, &arg).i[LOW];
}

RTAI_PROTO(int, rt_tbx_broadcast_until,(struct rt_typed_mailbox *tbx, void *msg, int msg_size, RTIME time))
{
	struct { struct rt_typed_mailbox *tbx; void *msg; int msg_size; RTIME time; } arg = { tbx, msg, msg_size, time };
	return rtai_lxrt(TBXIDX, SIZARG, TBX_BROADCAST_UNTIL, &arg).i[LOW];
}

RTAI_PROTO(int, rt_tbx_broadcast_timed,(struct rt_typed_mailbox *tbx, void *msg, int msg_size, RTIME delay))
{
	struct { struct rt_typed_mailbox *tbx; void *msg; int msg_size; RTIME delay; } arg = { tbx, msg, msg_size, delay };
	return rtai_lxrt(TBXIDX, SIZARG, TBX_BROADCAST_TIMED, &arg).i[LOW];
}

RTAI_PROTO(int, rt_tbx_urgent,(struct rt_typed_mailbox *tbx, void *msg, int msg_size))
{
	struct { struct rt_typed_mailbox *tbx; void *msg; int msg_size; } arg = { tbx, msg, msg_size };
	return rtai_lxrt(TBXIDX, SIZARG, TBX_URGENT, &arg).i[LOW];
}

RTAI_PROTO(int, rt_tbx_urgent_if,(struct rt_typed_mailbox *tbx, void *msg, int msg_size))
{
	struct { struct rt_typed_mailbox *tbx; void *msg; int msg_size; } arg = { tbx, msg, msg_size };
	return rtai_lxrt(TBXIDX, SIZARG, TBX_URGENT_IF, &arg).i[LOW];
}

RTAI_PROTO(int, rt_tbx_urgent_until,(struct rt_typed_mailbox *tbx, void *msg, int msg_size, RTIME time))
{
	struct { struct rt_typed_mailbox *tbx; void *msg; int msg_size; RTIME time; } arg = { tbx, msg, msg_size, time };
	return rtai_lxrt(TBXIDX, SIZARG, TBX_URGENT_UNTIL, &arg).i[LOW];
}

RTAI_PROTO(int, rt_tbx_urgent_timed,(struct rt_typed_mailbox *tbx, void *msg, int msg_size, RTIME delay))
{
	struct { struct rt_typed_mailbox *tbx; void *msg; int msg_size; RTIME delay; } arg = { tbx, msg, msg_size, delay };
	return rtai_lxrt(TBXIDX, SIZARG, TBX_URGENT_TIMED, &arg).i[LOW];
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __KERNEL__ */

#if !defined(__KERNEL__) || defined(__cplusplus)

typedef struct rt_typed_mailbox {
    int opaque;
} TBX;

#endif /* !__KERNEL__ || __cplusplus */

#endif /* !_RTAI_TBX_H */
