/*
 * Copyright (C) 1999-2003 Paolo Mantegazza <mantegazza@aero.polimi.it>
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

#ifndef _RTAI_USI_H
#define _RTAI_USI_H

#include <rtai_types.h>

#define  FUN_USI_LXRT_INDX 3

#define USI_SEM      0
#define USI_TASKLET  1

#define _REQ_GLB_IRQ 	 	 0
#define _FREE_GLB_IRQ	 	 1
#define _STARTUP_IRQ	 	 2
#define _SHUTDOWN_IRQ	 	 3
#define _ENABLE_IRQ	 	 4
#define _DISABLE_IRQ	 	 5
#define _MASK_AND_ACK_IRQ 	 6
#define _ACK_IRQ 	  	 7
#define _UNMASK_IRQ 		 8
#define _PEND_LINUX_IRQ	 	 9
#define _INIT_SPIN_LOCK		10
#define _SPIN_LOCK		11
#define _SPIN_UNLOCK		12
#define _SPIN_LOCK_IRQ		13
#define _SPIN_UNLOCK_IRQ	14
#define _SPIN_LOCK_IRQSV	15
#define _SPIN_UNLOCK_IRQRST	16
#define _GLB_CLI		17
#define _GLB_STI		18
#define _GLB_SVFLAGS_CLI	19
#define _GLB_SVFLAGS		20
#define _GLB_RSTFLAGS		21
#define _CLI			22
#define _STI			23
#define _SVFLAGS_CLI     	24
#define _SVFLAGS 		25
#define _RSTFLAGS		26
#define _WAIT_INTR		27
#define _WAIT_INTR_IF		28
#define _WAIT_INTR_UNTIL	29
#define _WAIT_INTR_TIMED	30

#ifdef __KERNEL__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int __rtai_usi_init(void);

void __rtai_usi_exit(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#else /* !__KERNEL__ */

#include <rtai_lxrt.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

RTAI_PROTO(int, rt_expand_handler_data,(unsigned long data, int *irq))
{
	*irq = data & 0xFFFF;
	return (data & 0xFFFF0000) >> 16;
}

RTAI_PROTO(int, rt_request_global_irq,(unsigned int irq, void *hook, int hooktype))
{
        struct { unsigned int irq; void *hook; int hooktype; } arg = { irq, hook, hooktype };
        return rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _REQ_GLB_IRQ, &arg).i[LOW];
}
 
RTAI_PROTO(int, rt_free_global_irq,(unsigned int irq))
{
        struct { unsigned int irq; } arg = { irq };
        return rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _FREE_GLB_IRQ, &arg).i[LOW];
}
 
RTAI_PROTO(int, rt_startup_irq,(unsigned int irq))
{
        struct { unsigned int irq; } arg = { irq };
        return rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _STARTUP_IRQ, &arg).i[LOW];
}
 
RTAI_PROTO(void, rt_shutdown_irq,(unsigned int irq))
{
        struct { unsigned int irq; } arg = { irq };
        rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _SHUTDOWN_IRQ, &arg);
}
 
RTAI_PROTO(void, rt_enable_irq,(unsigned int irq))
{
        struct { unsigned int irq; } arg = { irq };
        rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _ENABLE_IRQ, &arg);
}
 
RTAI_PROTO(void, rt_disable_irq,(unsigned int irq))
{
        struct { unsigned int irq; } arg = { irq };
        rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _DISABLE_IRQ, &arg);
}
 
RTAI_PROTO(void, rt_mask_and_ack_irq,(unsigned int irq))
{
        struct { unsigned int irq; } arg = { irq };
        rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _MASK_AND_ACK_IRQ, &arg);
}
 
RTAI_PROTO(void, rt_ack_irq,(unsigned int irq))
{
        struct { unsigned int irq; } arg = { irq };
        rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _ACK_IRQ, &arg);
}
 
RTAI_PROTO(void, rt_unmask_irq,(unsigned int irq))
{
        struct { unsigned int irq; } arg = { irq };
        rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _UNMASK_IRQ, &arg);
}
 
RTAI_PROTO(void, rt_pend_linux_irq,(unsigned int irq))
{
        struct { unsigned int irq; } arg = { irq };
        rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _PEND_LINUX_IRQ, &arg);
}
 
RTAI_PROTO(void *, rt_spin_lock_init,(void))
{
        struct { int dummy; } arg = { 0 };
	return rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _INIT_SPIN_LOCK, &arg).v[LOW];
}
 
RTAI_PROTO(void, rt_spin_lock,(void *lock))
{
        struct { void *lock; } arg = { lock };
        rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _SPIN_LOCK, &arg);
}
 
RTAI_PROTO(void, rt_spin_unlock,(void *lock))
{
        struct { void *lock; } arg = { lock };
        rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _SPIN_UNLOCK, &arg);
}
 
RTAI_PROTO(void, rt_spin_lock_irq,(void *lock))
{
        struct { void *lock; } arg = { lock };
        rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _SPIN_LOCK_IRQ, &arg);
}
 
RTAI_PROTO(void, rt_spin_unlock_irq,(void *lock))
{
        struct { void *lock; } arg = { lock };
        rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _SPIN_UNLOCK_IRQ, &arg);
}
 
RTAI_PROTO(unsigned long, rt_spin_lock_irqsave,(void *lock))
{
	struct { void *lock; } arg = { lock };
	return rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _SPIN_LOCK_IRQSV, &arg).i[LOW];
}
 
RTAI_PROTO(void, rt_spin_unlock_irqrestore,(unsigned long flags, void *lock))
{
	struct { unsigned long flags; void *lock; } arg = { flags, lock };
	rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _SPIN_UNLOCK_IRQRST, &arg);
}
 

RTAI_PROTO(void, rt_global_cli,(void))
{
	struct { int dummy; } arg = { 0 };
	rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _GLB_CLI, &arg);
}

RTAI_PROTO(void, rt_global_sti,(void))
{
	struct { int dummy; } arg = { 0 };
	rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _GLB_STI, &arg);
}

RTAI_PROTO(unsigned long, rt_global_save_flags_and_cli,(void))
{
	struct { int dummy; } arg = { 0 };
	return rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _GLB_SVFLAGS_CLI, &arg).i[LOW];
}
 
RTAI_PROTO(unsigned long, rt_global_save_flags,(void))
{
	struct { int dummy; } arg = { 0 };
	return rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _GLB_SVFLAGS, &arg).i[LOW];
}
 
RTAI_PROTO(void, rt_global_restore_flags,(unsigned long flags))
{
	struct { unsigned long flags; } arg = { flags };
	rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _GLB_RSTFLAGS, &arg);
}

RTAI_PROTO(void, hard_cli,(void))
{
	struct { int dummy; } arg = { 0 };
	rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _CLI, &arg);
}

RTAI_PROTO(void, hard_sti,(void))
{
	struct { int dummy; } arg = { 0 };
	rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _STI, &arg);
}

RTAI_PROTO(unsigned long, hard_save_flags_and_cli,(void))
{
	struct { int dummy; } arg = { 0 };
	return rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _SVFLAGS_CLI, &arg).i[LOW];
}
 
RTAI_PROTO(unsigned long, hard_save_flags,(void))
{
	struct { int dummy; } arg = { 0 };
	return rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _SVFLAGS, &arg).i[LOW];
}
 
RTAI_PROTO(void, hard_restore_flags,(unsigned long flags))
{
	struct { unsigned long flags; } arg = { flags };
	rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _RSTFLAGS, &arg);
}

RTAI_PROTO(int, rt_wait_intr,(void *sem, unsigned long *irq))
{
	unsigned long lirq;
	int retval;
        struct { void *sem; unsigned long *lirq; int size; } arg = { sem, &lirq, sizeof(unsigned long *) };
	retval = rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _WAIT_INTR, &arg).i[LOW];
	if (irq) {
		*irq = lirq;
	}
	return retval;
}                                                                               

RTAI_PROTO(int, rt_wait_intr_if,(void *sem, unsigned long *irq))
{
	unsigned long lirq;
	int retval;
        struct { void *sem; unsigned long *lirq; int size; } arg = { sem, &lirq, sizeof(unsigned long *) };
	retval = rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _WAIT_INTR_IF, &arg).i[LOW];
	if (irq) {
		*irq = lirq;
	}
	return retval;
}                                                                               

RTAI_PROTO(int, rt_wait_intr_until,(void *sem, RTIME until, unsigned long *irq))
{
	unsigned long lirq;
	int retval;
        struct { void *sem; RTIME until; unsigned long *lirq; int size; } arg = { sem, until, &lirq, sizeof(unsigned long *) };
	retval = rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _WAIT_INTR_UNTIL, &arg).i[LOW];
	if (irq) {
		*irq = lirq;
	}
	return retval;
}                                                                               

RTAI_PROTO(int, rt_wait_intr_timed,(void *sem, RTIME delay, unsigned long *irq))
{
	unsigned long lirq;
	int retval;
        struct { void *sem; RTIME delay; unsigned long *lirq; int size; } arg = { sem, delay, &lirq, sizeof(unsigned long *) };
	retval = rtai_lxrt(FUN_USI_LXRT_INDX, SIZARG, _WAIT_INTR_TIMED, &arg).i[LOW];
	if (irq) {
		*irq = lirq;
	}
	return retval;
}                                                                               

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __KERNEL__ */

#endif /* !_RTAI_USI_H */
