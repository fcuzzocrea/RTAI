/*
COPYRIGHT (C) 2002  Pierre Cloutier  (pcloutier@poseidoncontrols.com)
                    Paolo Mantegazza (mantegazza@aero.polimi.it)

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
*/


struct mcb_t { void *sbuf; int sbytes; void *rbuf; int rbytes; };

#define SET_MCB \
	do { \
		mcb.sbuf = smsg; \
		mcb.sbytes = ssize; \
		mcb.rbuf = rmsg; \
		mcb.rbytes = rsize; \
	} while (0)

static __inline__ RT_TASK *rt_rpcx(RT_TASK *task, void *smsg, void *rmsg, int ssize, int rsize)
{
	if (task) {
		struct mcb_t mcb;
		SET_MCB;
		return rt_rpc(task, (unsigned int)&mcb, (unsigned int *)&mcb.rbytes);
	}
	return 0;
}

static __inline__ RT_TASK *rt_rpcx_if(RT_TASK *task, void *smsg, void *rmsg, int ssize, int rsize)
{
	if (task) {
		struct mcb_t mcb;
		SET_MCB;
		return rt_rpc_if(task, (unsigned int)&mcb, (unsigned int *)&mcb.rbytes);
	}
	return 0;
}

static __inline__ RT_TASK *rt_rpcx_until(RT_TASK *task, void *smsg, void *rmsg, int ssize, int rsize, RTIME time)
{
	if (task) {
		struct mcb_t mcb;
		SET_MCB;
		return rt_rpc_until(task, (unsigned int)&mcb, (unsigned int *)&mcb.rbytes, time);
	}
	return 0;
}

static __inline__ RT_TASK *rt_rpcx_timed(RT_TASK *task, void *smsg, void *rmsg, int ssize, int rsize, RTIME delay)
{
	if (task) {
		struct mcb_t mcb;
		SET_MCB;
		return rt_rpc_timed(task, (unsigned int)&mcb, (unsigned int *)&mcb.rbytes, delay);
	}
	return 0;
}

#define rt_sendx(task, msg, size) \
	rt_rpcx(task, msg, 0, size, -1)

#define rt_sendx_if(task, msg, size) \
	rt_rpcx_if(task, msg, 0, size, -1)

#define rt_sendx_until(task, msg, size, time) \
	rt_rpcx_until(task, msg, 0, size, -1, time)

#define rt_sendx_timed(task, msg, size, delay) \
	rt_rpcx_timed(task, msg, 0, size, -1, delay)

static __inline__ RT_TASK *rt_returnx(RT_TASK *task, void *msg, int size)
{
	if (task) {
		struct mcb_t *mcb;
		if ((mcb = (struct mcb_t *)task->msg)->rbytes < size) {
			size = mcb->rbytes;
		}
		if (size) {
			memcpy(mcb->rbuf, msg, size);
		}
		return rt_return(task, 0);
	}
	return 0;
}

#define rt_isrpcx(task) rt_isrpc(task)

#define DO_RCV_MSG \
	do { \
		if ((*len = size <= mcb->sbytes ? size : mcb->sbytes)) { \
			memcpy(msg, mcb->sbuf, *len); \
		} \
		if (mcb->rbytes < 0) { \
			rt_return(task, 0); \
		} \
	} while (0)

static __inline__ RT_TASK *rt_receivex(RT_TASK *task, void *msg, int size, int *len)
{
	struct mcb_t *mcb;
	if ((task = rt_receive(task, (unsigned int *)&mcb))) {
		DO_RCV_MSG;
		return task;
	}
	return 0;
}

static __inline__ RT_TASK *rt_receivex_if(RT_TASK *task, void *msg, int size, int *len)
{
	struct mcb_t *mcb;
	if ((task = rt_receive_if(task, (unsigned int *)&mcb))) {
		DO_RCV_MSG;
		return task;
	}
	return 0;
}

static __inline__ RT_TASK *rt_receivex_until(RT_TASK *task, void *msg, int size, int *len, RTIME time)
{
	struct mcb_t *mcb;
	if ((task = rt_receive_until(task, (unsigned int *)&mcb, time))) {
		DO_RCV_MSG;
		return task;
	}
	return 0;
}

static __inline__ RT_TASK *rt_receivex_timed(RT_TASK *task, void *msg, int size, int *len, RTIME delay)
{
	struct mcb_t *mcb;
	if ((task = rt_receive_timed(task, (unsigned int *)&mcb, delay))) {
		DO_RCV_MSG;
		return task;
	}
	return 0;
}
