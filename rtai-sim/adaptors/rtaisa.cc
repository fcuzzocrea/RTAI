/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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
 *
 * Description: Simulation Adaptor module for the virtual RTAI module.
 */

#include <rtai_config.h>
#include <stdio.h>
#include "vrtai/rtai_sched.h"
#include "vm/manager.h"
#include "vm/interrupt.h"
#include "vm/display.h"

extern void (*gcic_dinsn)(int);

extern void (*gcic_dframe)(int);

static struct {

    void (*handler)(void);
    unsigned label;

} mvm_srq_table[RTAI_NR_TRAPS];

static u_long mvm_srq_pend;

static int mvm_tids;

static RT_TASK *mvm_running_task;

static void (*mvm_tick_handler)(void);

unsigned long jiffies = 0;

extern "C" {

static void real_dinsn (int tag) {

    MvmManager::This->khook(traceInsn)(tag);
}

static void real_dframe (int tag) {

    MvmManager::This->khook(trackFrame)(tag);
}

static const char *mvm_get_task_mode (void *tcbarg)

{
    vtasktcb_t *tcb = (vtasktcb_t *)tcbarg;
    static TclList modeString;

    modeString.clear();

    if (tcb->task != NULL)
	{
	if (tcb->task->policy)	// Round-robin
	    modeString.append("rrb");

	if (tcb->task->priority == -1) // Scheduler lock
	    modeString.append("lock");
	else
	    modeString.append(CString().format("prio=%d",tcb->task->priority));
	}

    return modeString;
}

static unsigned long long mvm_get_jiffies (void) {
    return jiffies;
}

void mvm_finalize_init (struct rt_task_struct *linux_task)

{
    mvm_running_task = linux_task;
    linux_task->vtcb.task = linux_task;
    linux_task->vtcb.vmthread = mvm_thread_self();
    MvmManager::This->setFlags(MVM_SIMREADY);
}

int mvm_test_predicate (int pred)

{
    if (MvmManager::This->testFlags(MVM_SIMREADY))
	{
	switch (pred)
	    {
	    case MVM_ON_CALLOUT:
	    case MVM_ON_ISERVICE:
		break;

	    case MVM_ON_ASYNCH:
	    case MVM_ON_IHANDLER:
		return MvmIrqManager::This->onHandlerP();
	    }
	}

    return 0;
}

static void kdsrt(mvm_fire_srqs) (void)

{
    for (unsigned srq = 0; srq < RTAI_NR_TRAPS; srq++)
	{
	if (mvm_srq_pend & (1 << srq))
	    {
	    mvm_srq_pend &= ~(1 << srq);
	    mvm_srq_table[srq].handler();
	    }
	}
}

static void kroot(mvm_task_trampoline) (void *tcbarg)

{
    vtasktcb_t *tcb = (vtasktcb_t *)tcbarg;
    mvm_running_task = tcb->task;
    tcb->vmthread = mvm_thread_self();
    tcb->trampoline(tcb->task_body,tcb->data);
}

int mvm_run (void *tcbarg, void *faddr)

{
    MvmManager::trampoline = &kroot(mvm_task_trampoline);
    MvmManager::threadmode = &mvm_get_task_mode;
    MvmManager::jiffies = &mvm_get_jiffies;
    MvmManager::predicate = &mvm_test_predicate;
    MvmManager::idletime = &kdsrt(mvm_fire_srqs);
    MvmManager::This->initialize(new XenoThread(tcbarg,faddr,0,"Linux"));
    gcic_dinsn = &real_dinsn;
    gcic_dframe = &real_dframe;

    int xcode = MvmManager::This->run();

    while (MvmManager::This->testFlags(MVM_SIMREADY))
	MvmManager::currentThread->delay(0);

    return xcode;
}

void mvm_create_display (mvm_displayctx_t *ctx,
			 mvm_displayctl_t *ctl,
			 void *obj,
			 const char *name)
{
    ctx->dashboard = new MvmDashboard(name,
				      ctl->prefix,
				      NULL,
				      ctx,
				      ctl->objctl);
    ctx->graph = new MvmGraph(name,
			      ctl->group,
			      ctl->sarray);
    ctx->control = ctl;
    ctx->obj = obj;
    ctx->dashboard->ifInit();
    ctx->graph->ifInit();
}

void mvm_delete_display (mvm_displayctx_t *ctx)

{
    if (ctx->dashboard != NULL)
	{
	delete ctx->dashboard;
	ctx->dashboard = NULL;
	}

    if (ctx->graph != NULL)
	{
	delete ctx->graph;
	ctx->graph = NULL;
	}
}

void mvm_send_display (mvm_displayctx_t *ctx, const char *s) {
    ctx->dashboard->ifInfo(MVM_IFACE_DASHBOARD_INFO,s,-1);
}

void kdoor(mvm_post_graph) (mvm_displayctx_t *ctx, int state) {

    if (ctx->graph != NULL)
	ctx->graph->setState(state);
}

void kdoor(mvm_switch_to) (struct rt_task_struct *task)

{
    RT_TASK *running_task = mvm_running_task;

    mvm_running_task = task;

    if (running_task->state != 0)
	kdoor(mvm_switch_threads)(running_task->vtcb.vmthread,
				  task->vtcb.vmthread);
    else
	mvm_finalize_switch_threads(running_task->vtcb.vmthread,
				    task->vtcb.vmthread);
}

void mvm_finalize_task(struct rt_task_struct *task)

{
    if (task != mvm_running_task)
	// A terminating running task's VM buddy will be finalized
	// when it switches out.
	mvm_finalize_thread(task->vtcb.vmthread);
}

void mvm_init_task_stack (struct rt_task_struct *task,
			  void (*user_trampoline)(void(*task_body)(int), int data),
			  void (*task_body)(int),
			  int data)
{
    task->vtcb.task = task;
    task->vtcb.data = data;
    task->vtcb.task_body = task_body;
    task->vtcb.trampoline = user_trampoline;
    task->vtcb.vmthread = mvm_spawn_thread(&task->vtcb,
					   (void *)task_body,
					   CString().format("t%.3d",++mvm_tids));
}

int mvm_request_srq (unsigned label,
		     void (*handler)(void))
{
    if (!handler)
	return -EINVAL;

    for (int srq = 0; srq < RTAI_NR_TRAPS; srq++)
	{
	if (mvm_srq_table[srq].handler == NULL)
	    {
	    mvm_srq_table[srq].handler = handler;
	    mvm_srq_table[srq].label = 0;
	    return srq;
	    }
	}

    return -EBUSY;
}

int mvm_free_srq (unsigned srq)

{
    if (srq >= RTAI_NR_TRAPS)
	return -EINVAL;

    mvm_srq_table[srq].handler = NULL;
    mvm_srq_table[srq].label = 0;
    mvm_srq_pend &= ~(1 << srq);

    return 0;
}

void mvm_post_srq (unsigned srq)

{
    if (srq < RTAI_NR_TRAPS)
	{
	mvm_srq_pend |= (1 << srq);
	MvmManager::This->setFlags(MVM_CALLIDLE);
	}
}

static void khide(mvm_timer_handler) (void) {
    jiffies++;
    mvm_tick_handler();
}

void mvm_request_timer (int ticks,
			void (*tickhandler)(void))
{
    unsigned long long nstick, count;

    mvm_tick_handler = tickhandler;

    if (ticks > 0)
	{
	rt_times.linux_tick = LATCH;
	rt_times.tick_time = ((RTIME)rt_times.linux_tick)*(jiffies + 1);
	rt_times.intr_time = rt_times.tick_time + ticks;
	rt_times.linux_time = rt_times.tick_time + rt_times.linux_tick;
	rt_times.periodic_tick = ticks;
	count = (unsigned long long)ticks;
	}
    else
	{
	rt_times.tick_time = mvm_get_cpu_time() * tuned.cpu_freq / 1000000000;
	rt_times.linux_tick = imuldiv(LATCH,tuned.cpu_freq,FREQ_8254);
	rt_times.intr_time = rt_times.tick_time + rt_times.linux_tick;
	rt_times.linux_time = rt_times.tick_time + rt_times.linux_tick;
	rt_times.periodic_tick = rt_times.linux_tick;
	count = LATCH;
	}

    nstick = count * 1000000000 / tuned.cpu_freq;
    mvm_start_timer(nstick,&khide(mvm_timer_handler));
}

void mvm_free_timer (void) {
    mvm_stop_timer();
}

}
