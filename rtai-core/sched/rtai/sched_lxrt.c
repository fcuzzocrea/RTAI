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

/*
ACKNOWLEDGMENTS:
- Steve Papacharalambous (stevep@zentropix.com) has contributed a very 
  informative proc filesystem procedure.
- Stefano Picerno (stefanopp@libero.it) for suggesting a simple fix to
  distinguish a timeout from an abnormal retrun in timed sem waits.
- Geoffrey Martin (gmartin@altersys.com) for a fix to functions with timeouts.
*/


#define USE_RTAI_TASKS  0

#define ALLOW_RR        1
#define ONE_SHOT        0

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/timex.h>
#include <linux/sched.h>
#include <linux/irq.h>
#include <linux/reboot.h>

#include <asm/param.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/hw_irq.h>
#include <asm/uaccess.h>
#include <asm/mmu_context.h>

#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
static int errno;

#ifdef CONFIG_PROC_FS
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <rtai_proc_fs.h>
static int rtai_proc_sched_register(void);
static void rtai_proc_sched_unregister(void);
int rtai_proc_lxrt_register(void);
void rtai_proc_lxrt_unregister(void);
#endif

#include <rtai.h>
#include <asm/rtai_sched.h>
#include <rtai_lxrt.h>
#include <rtai_registry.h>
#include <rtai_nam2num.h>
#include <rtai_schedcore.h>

MODULE_LICENSE("GPL");

/* +++++++++++++++++ WHAT MUST BE AVAILABLE EVERYWHERE ++++++++++++++++++++++ */

RT_TASK rt_smp_linux_task[NR_RT_CPUS];

RT_TASK *rt_smp_current[NR_RT_CPUS];

RTIME rt_smp_time_h[NR_RT_CPUS];

int rt_smp_oneshot_timer[NR_RT_CPUS];

struct klist_t wake_up_srq;

/* +++++++++++++++ END OF WHAT MUST BE AVAILABLE EVERYWHERE +++++++++++++++++ */

static int sched_rqsted[NR_RT_CPUS];

static int rt_smp_linux_cr0[NR_RT_CPUS];

static RT_TASK *rt_smp_fpu_task[NR_RT_CPUS];

static int rt_smp_half_tick[NR_RT_CPUS];

static int rt_smp_oneshot_running[NR_RT_CPUS];

static volatile int rt_smp_shot_fired[NR_RT_CPUS];

static struct rt_times *linux_times;

static RT_TASK *lxrt_wdog_task[NR_RT_CPUS];

static unsigned lxrt_migration_virq;

static int lxrt_notify_reboot(struct notifier_block *nb,
			      unsigned long event,
			      void *ptr);

static struct notifier_block lxrt_notifier_reboot = {
	.notifier_call	= &lxrt_notify_reboot,
	.next		= NULL,
	.priority	= 0
};

static struct klist_t klistb[NR_RT_CPUS];

static struct task_struct *kthreadb[NR_RT_CPUS];
static wait_queue_t kthreadb_q[NR_RT_CPUS];

static struct klist_t klistm[NR_RT_CPUS];

static struct task_struct *kthreadm[NR_RT_CPUS];

static struct semaphore resem[NR_RT_CPUS];

static int endkthread;

#define fpu_task (rt_smp_fpu_task[cpuid])

#define rt_half_tick (rt_smp_half_tick[cpuid])

#define oneshot_running (rt_smp_oneshot_running[cpuid])

#define oneshot_timer_cpuid (rt_smp_oneshot_timer[hard_cpu_id()])

#define shot_fired (rt_smp_shot_fired[cpuid])

#define rt_times (rt_smp_times[cpuid])

#define linux_cr0 (rt_smp_linux_cr0[cpuid])

#define MAX_FRESTK_SRQ  64
static struct {
    int srq, in, out;
    void *mp[MAX_FRESTK_SRQ];
} frstk_srq;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#define KTHREAD_B_PRIO MAX_LINUX_RTPRIO/2
#else /* KERNEL_VERSION >= 2.6.0 */
#define KTHREAD_B_PRIO MAX_LINUX_RTPRIO
#endif /* KERNEL_VERSION < 2.6.0 */
#define KTHREAD_M_PRIO MAX_LINUX_RTPRIO
#define KTHREAD_F_PRIO MAX_LINUX_RTPRIO

#ifdef CONFIG_SMP
unsigned long sqilter = 0xFFFFFFFF;
#endif

#ifdef __USE_APIC__

#define TIMER_FREQ RTAI_FREQ_APIC
#define TIMER_LATENCY RTAI_LATENCY_APIC
#define TIMER_SETUP_TIME RTAI_SETUP_TIME_APIC
#define ONESHOT_SPAN (0x7FFFFFFFLL*(CPU_FREQ/TIMER_FREQ))
#define update_linux_timer()

#else /* !__USE_APIC__ */

#define TIMER_FREQ RTAI_FREQ_8254
#define TIMER_LATENCY RTAI_LATENCY_8254
#define TIMER_SETUP_TIME RTAI_SETUP_TIME_8254
#define ONESHOT_SPAN (0x7FFF*(CPU_FREQ/TIMER_FREQ))
#define update_linux_timer() rt_pend_linux_irq(TIMER_8254_IRQ)

#endif /* __USE_APIC__ */

#ifdef CONFIG_SMP

#define rt_request_sched_ipi()  rt_request_cpu_own_irq(SCHED_IPI, rt_schedule_on_schedule_ipi)

#define rt_free_sched_ipi()     rt_free_cpu_own_irq(SCHED_IPI)

#define sched_get_global_lock(cpuid) \
do { \
	if (!test_and_set_bit(cpuid, locked_cpus)) { \
		while (test_and_set_bit(31, locked_cpus)) { \
			CPU_RELAX(cpuid); \
		} \
	} \
} while (0)

#define sched_release_global_lock(cpuid) \
do { \
	if (test_and_clear_bit(cpuid, locked_cpus)) { \
		test_and_clear_bit(31, locked_cpus); \
		CPU_RELAX(cpuid); \
	} \
} while (0)

#else /* !CONFIG_SMP */

#define rt_request_sched_ipi() 0

#define rt_free_sched_ipi()

#define sched_get_global_lock(cpuid)

#define sched_release_global_lock(cpuid)

#endif /* CONFIG_SMP */

/* ++++++++++++++++++++++++++++++++ TASKS ++++++++++++++++++++++++++++++++++ */

static int tasks_per_cpu[NR_RT_CPUS] = { 0, };

int get_min_tasks_cpuid(void)
{
	int i, cpuid, min;
	min =  tasks_per_cpu[cpuid = 0];
	for (i = 1; i < NR_RT_CPUS; i++) {
		if (tasks_per_cpu[i] < min) {
			min = tasks_per_cpu[cpuid = i];
		}
	}
	return cpuid;
}

static void put_current_on_cpu(int cpuid)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	current->cpus_allowed = 1 << cpuid;
	while (cpuid != hard_cpu_id()) {
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(2);
	}
#else /* KERNEL_VERSION >= 2.6.0 */
	if (set_cpus_allowed(current,cpumask_of_cpu(cpuid)))
	    {
	    ((RT_TASK *)(current->this_rt_task[0]))->runnable_on_cpus = smp_processor_id();
	    set_cpus_allowed(current,cpumask_of_cpu(smp_processor_id()));
	    }

#endif  /* KERNEL_VERSION < 2.6.0 */
}

int set_rtext(RT_TASK *task, int priority, int uses_fpu, void(*signal)(void), unsigned int cpuid, struct task_struct *relink)
{
	unsigned long flags;

	if (num_online_cpus() <= 1) {
		 cpuid = 0;
	}
	if (task->magic == RT_TASK_MAGIC || cpuid >= NR_RT_CPUS || priority < 0) {
		return -EINVAL;
	} 
	if (lxrt_wdog_task[cpuid] &&
	    lxrt_wdog_task[cpuid] != task &&
	    priority == RT_SCHED_HIGHEST_PRIORITY) {
	    	 rt_printk("Highest priority reserved for RTAI watchdog\n");
		 return -EBUSY;
	}
	task->uses_fpu = uses_fpu ? 1 : 0;
	task->runnable_on_cpus = cpuid;
	(task->stack_bottom = (int *)&task->fpu_reg)[0] = 0;
	task->magic = RT_TASK_MAGIC; 
	task->policy = 0;
	task->owndres = 0;
	task->priority = task->base_priority = priority;
	task->prio_passed_to = 0;
	task->period = 0;
	task->resume_time = RT_TIME_END;
	task->queue.prev = task->queue.next = &(task->queue);      
	task->queue.task = task;
	task->msg_queue.prev = task->msg_queue.next = &(task->msg_queue);      
	task->msg_queue.task = task;    
	task->msg = 0;  
	task->ret_queue.prev = task->ret_queue.next = &(task->ret_queue);
	task->ret_queue.task = NOTHING;
	task->tprev = task->tnext = task->rprev = task->rnext = task;
	task->blocked_on = NOTHING;        
	task->signal = signal;
	memset(task->task_trap_handler, 0, RTAI_NR_TRAPS*sizeof(void *));
	task->tick_queue        = NOTHING;
	task->trap_handler_data = NOTHING;
	task->resync_frame = 0;
	task->ExitHook = 0;
	task->usp_flags = task->usp_flags_mask = task->force_soft = 0;
	task->msg_buf[0] = 0;
	task->exectime[0] = task->exectime[1] = 0;
	task->system_data_ptr = 0;
	atomic_inc((atomic_t *)(tasks_per_cpu + cpuid));
	if (relink) {
		task->suspdepth = task->is_hard = 1;
		task->state = RT_SCHED_READY | RT_SCHED_SUSPENDED;
		relink->this_rt_task[0] = task;
		task->lnxtsk = relink;
	} else {
		task->suspdepth = task->is_hard = 0;
		task->state = RT_SCHED_READY;
		current->this_rt_task[0] = task;
		current->this_rt_task[1] = task->lnxtsk = current;
		put_current_on_cpu(cpuid);
	}
	flags = rt_global_save_flags_and_cli();
	task->next = 0;
	rt_linux_task.prev->next = task;
	task->prev = rt_linux_task.prev;
	rt_linux_task.prev = task;
	rt_global_restore_flags(flags);

	return 0;
}


static void start_stop_kthread(RT_TASK *, void (*)(int), int, int, int, void(*)(void), int);

int rt_kthread_init_cpuid(RT_TASK *task, void (*rt_thread)(int), int data,
			int stack_size, int priority, int uses_fpu,
			void(*signal)(void), unsigned int cpuid)
{
	start_stop_kthread(task, rt_thread, data, priority, uses_fpu, signal, cpuid);
	return (int)task->retval;
}


int rt_kthread_init(RT_TASK *task, void (*rt_thread)(int), int data,
			int stack_size, int priority, int uses_fpu,
			void(*signal)(void))
{
	return rt_kthread_init_cpuid(task, rt_thread, data, stack_size, priority, 
				 uses_fpu, signal, get_min_tasks_cpuid());
}


#if USE_RTAI_TASKS

asmlinkage static void rt_startup(void(*rt_thread)(int), int data)
{
	extern int rt_task_delete(RT_TASK *);
	rt_global_sti();
	RT_CURRENT->exectime[1] = rdtsc();
	rt_thread(data);
	rt_task_delete(rt_smp_current[hard_cpu_id()]);
	rt_printk("LXRT: task %p returned but could not be delated.\n", rt_smp_current[hard_cpu_id()]); 
}


int rt_task_init_cpuid(RT_TASK *task, void (*rt_thread)(int), int data, int stack_size, int priority, int uses_fpu, void(*signal)(void), unsigned int cpuid)
{
	int *st, i;
	unsigned long flags;

	if (num_online_cpus() <= 1) {
		 cpuid = 0;
	}
	if (task->magic == RT_TASK_MAGIC || cpuid >= NR_RT_CPUS || priority < 0) {
		return -EINVAL;
	} 
	if (!(st = (int *)sched_malloc(stack_size))) {
		return -ENOMEM;
	}
	if (lxrt_wdog_task[cpuid] && lxrt_wdog_task[cpuid] != task 
		             && priority == RT_SCHED_HIGHEST_PRIORITY) {
	    	 rt_printk("Highest priority reserved for RTAI watchdog\n");
		 return -EBUSY;
	}

	task->bstack = task->stack = (int *)(((unsigned long)st + stack_size - 0x10) & ~0xF);
	task->stack[0] = 0;
	task->uses_fpu = uses_fpu ? 1 : 0;
	task->runnable_on_cpus = cpuid;
	atomic_inc((atomic_t *)(tasks_per_cpu + cpuid));
	*(task->stack_bottom = st) = 0;
	task->magic = RT_TASK_MAGIC; 
	task->policy = 0;
	task->suspdepth = 1;
	task->state = (RT_SCHED_SUSPENDED | RT_SCHED_READY);
	task->owndres = 0;
	task->is_hard = 1;
	task->lnxtsk = 0;
	task->priority = task->base_priority = priority;
	task->prio_passed_to = 0;
	task->period = 0;
	task->resume_time = RT_TIME_END;
	task->queue.prev = &(task->queue);      
	task->queue.next = &(task->queue);      
	task->queue.task = task;
	task->msg_queue.prev = &(task->msg_queue);      
	task->msg_queue.next = &(task->msg_queue);      
	task->msg_queue.task = task;    
	task->msg = 0;  
	task->ret_queue.prev = &(task->ret_queue);
	task->ret_queue.next = &(task->ret_queue);
	task->ret_queue.task = NOTHING;
	task->tprev = task->tnext =
	task->rprev = task->rnext = task;
	task->blocked_on = NOTHING;        
	task->signal = signal;
	for (i = 0; i < RTAI_NR_TRAPS; i++) {
		task->task_trap_handler[i] = NULL;
	}
	task->tick_queue        = SOMETHING;
	task->trap_handler_data = NOTHING;
	task->resync_frame = 0;
	task->ExitHook = 0;
	task->exectime[0] = task->exectime[1] = 0;
	task->system_data_ptr = 0;

	init_arch_stack();

	flags = rt_global_save_flags_and_cli();
	task->next = 0;
	rt_linux_task.prev->next = task;
	task->prev = rt_linux_task.prev;
	rt_linux_task.prev = task;
	cpuid = hard_cpu_id();
	init_fp_env();
	rt_global_restore_flags(flags);

	return 0;
}

int rt_task_init(RT_TASK *task, void (*rt_thread)(int), int data,
			int stack_size, int priority, int uses_fpu,
			void(*signal)(void))
{
	return rt_task_init_cpuid(task, rt_thread, data, stack_size, priority, 
				 uses_fpu, signal, get_min_tasks_cpuid());
}

#else /* !USE_RTAI_TASKS */

int rt_task_init_cpuid(RT_TASK *task, void (*rt_thread)(int), int data, int stack_size, int priority, int uses_fpu, void(*signal)(void), unsigned int cpuid)
{
	return rt_kthread_init_cpuid(task, rt_thread, data, stack_size, priority, uses_fpu, signal, cpuid);
}

int rt_task_init(RT_TASK *task, void (*rt_thread)(int), int data, int stack_size, int priority, int uses_fpu, void(*signal)(void))
{
	return rt_kthread_init(task, rt_thread, data, stack_size, priority, uses_fpu, signal);
}

#endif /* USE_RTAI_TASKS */

void rt_set_runnable_on_cpuid(RT_TASK *task, unsigned int cpuid)
{
	unsigned long flags;
	RT_TASK *linux_task;

	return;

	if (cpuid >= NR_RT_CPUS) {
		cpuid = get_min_tasks_cpuid();
	} 
	flags = rt_global_save_flags_and_cli();
	switch (rt_smp_oneshot_timer[task->runnable_on_cpus] | 
		(rt_smp_oneshot_timer[cpuid] << 1)) {	
                case 1:
                        task->period = llimd(task->period, TIMER_FREQ, tuned.cpu_freq);
                        task->resume_time = llimd(task->resume_time, TIMER_FREQ, tuned.cpu_freq);
                        break;
                case 2:
                        task->period = llimd(task->period, tuned.cpu_freq, TIMER_FREQ);
                        task->resume_time = llimd(task->resume_time, tuned.cpu_freq, TIMER_FREQ);
			break;
	}
	if (!((task->prev)->next = task->next)) {
		rt_smp_linux_task[task->runnable_on_cpus].prev = task->prev;
	} else {
		(task->next)->prev = task->prev;
	}
	task->runnable_on_cpus = cpuid;
	if ((task->state & RT_SCHED_DELAYED)) {
		(task->tprev)->tnext = task->tnext;
		(task->tnext)->tprev = task->tprev;
		enq_timed_task(task);
	}
	task->next = 0;
	(linux_task = rt_smp_linux_task + cpuid)->prev->next = task;
	task->prev = linux_task->prev;
	linux_task->prev = task;
	rt_global_restore_flags(flags);
}


void rt_set_runnable_on_cpus(RT_TASK *task, unsigned long run_on_cpus)
{
	int cpuid;

	return;

	run_on_cpus &= CPUMASK(cpu_online_map);
	cpuid = get_min_tasks_cpuid();
	if (!test_bit(cpuid, &run_on_cpus)) {
		cpuid = ffnz(run_on_cpus);
	}
	rt_set_runnable_on_cpuid(task, cpuid);
}


int rt_check_current_stack(void)
{
	DECLARE_RT_CURRENT;
	char *sp;

	ASSIGN_RT_CURRENT;
	if (rt_current != &rt_linux_task) {
		sp = get_stack_pointer();
		return (sp - (char *)(rt_current->stack_bottom));
	} else {
		return -0x7FFFFFFF;
	}
}


#define TASK_TO_SCHEDULE() \
	do { prio = (new_task = rt_linux_task.rnext)->priority; } while(0)

#if ALLOW_RR
#define RR_YIELD() \
if (rt_current->policy > 0) { \
	rt_current->rr_remaining = rt_current->yield_time - rt_times.tick_time; \
	if (rt_current->rr_remaining <= 0) { \
		rt_current->rr_remaining = rt_current->rr_quantum; \
		if (rt_current->state == RT_SCHED_READY) { \
			RT_TASK *task; \
			task = rt_current->rnext; \
			while (rt_current->priority == task->priority) { \
				task = task->rnext; \
			} \
			if (task != rt_current->rnext) { \
				(rt_current->rprev)->rnext = rt_current->rnext; \
				(rt_current->rnext)->rprev = rt_current->rprev; \
				task->rprev = (rt_current->rprev = task->rprev)->rnext = rt_current; \
				rt_current->rnext = task; \
			} \
		} \
	} \
} 

#define RR_SETYT() \
	if (new_task->policy > 0) { \
		new_task->yield_time = rt_time_h + new_task->rr_remaining; \
	}

#define RR_PREMP() \
	if (new_task->policy > 0) { \
		preempt = 1; \
		if (new_task->yield_time < rt_times.intr_time) { \
			rt_times.intr_time = new_task->yield_time; \
		} \
	} else { \
		preempt = 0; \
	}
#else
#define RR_YIELD()

#define RR_SETYT()

#define RR_PREMP()  do { preempt = 0; } while (0)
#endif

#define restore_fpu(tsk) \
	do { restore_fpenv_lxrt((tsk)); set_tsk_used_fpu(tsk); } while (0)

#define LOCK_LINUX(cpuid)    do { rt_switch_to_real_time(cpuid); } while (0)

#define UNLOCK_LINUX(cpuid)  do { if (rt_switch_to_linux(cpuid)) rt_printk("*** ERROR: EXCESS LINUX_UNLOCK ***\n"); } while (0)

#define EXECTIME
#ifdef EXECTIME
static RTIME switch_time[NR_RT_CPUS];
#define KEXECTIME() \
do { \
	RTIME now; \
	now = rdtsc(); \
	if (!rt_current->lnxtsk) { \
		rt_current->exectime[0] += (now - switch_time[cpuid]); \
	} \
	switch_time[cpuid] = now; \
} while (0)

#define UEXECTIME() \
do { \
	RTIME now; \
	now = rdtsc(); \
	if (rt_current->is_hard) { \
		rt_current->exectime[0] += (now - switch_time[cpuid]); \
	} \
	switch_time[cpuid] = now; \
} while (0)
#else
#define KEXECTIME()
#define UEXECTIME()
#endif

/* 
 * After innumerable tries this seems a safe mode to have gcc use 
 * lxrt_context_switch without making weird reordered things, anywhere. 
 * At least it is hoped so. In any case this seems a good way to safely
 * use Linux-2.6.xx "switch_to", thus avoiding the 2.4.xx way, even if
 * it works anyhow and seems more robust against gcc's black magic.
 * It must be remarked also that the most critical point appears to be 
 * at the the fast scheduling, in ADEOS virtual interrupt.
 */
 
static void lxrt_context_switch(struct task_struct *prev, struct task_struct *next, int cpuid)
{
	_lxrt_context_switch(prev, next, cpuid);
}

void rt_do_force_soft(RT_TASK *rt_task)
{
	rt_global_cli();
	if (rt_task->state != RT_SCHED_READY) {
		rt_task->state &= ~RT_SCHED_READY;
            	enq_ready_task(rt_task);
		RT_SCHEDULE(rt_task, hard_cpu_id());
	}
	rt_global_sti();
}

static inline void make_current_soft(RT_TASK *rt_current, int cpuid)
{
        void rt_schedule(void);
        rt_current->force_soft = 0;
	rt_current->state &= ~RT_SCHED_READY;;
        wake_up_srq.task[wake_up_srq.in] = rt_current->lnxtsk;
        wake_up_srq.in = (wake_up_srq.in + 1) & (MAX_WAKEUP_SRQ - 1);
        rt_pend_linux_srq(wake_up_srq.srq);
        (rt_current->rprev)->rnext = rt_current->rnext;
        (rt_current->rnext)->rprev = rt_current->rprev;
        rt_schedule();
        rt_current->is_hard = 0;
	rt_global_sti();
        __adeos_schedule_back_root(rt_current->lnxtsk);
	rt_global_cli();
        if (rt_current->state) {
        	rt_current->state |= RT_SCHED_READY;
		LOCK_LINUX(cpuid);
        	(rt_current->lnxtsk)->state = TASK_HARDREALTIME;
		rt_smp_current[cpuid] = rt_current;
		rt_schedule();
		rt_current->state &= ~RT_SCHED_READY;
		(rt_current->rprev)->rnext = rt_current->rnext;
		(rt_current->rnext)->rprev = rt_current->rprev;
		rt_smp_current[cpuid] = &rt_linux_task;
		UNLOCK_LINUX(cpuid);
        }
}

#ifdef CONFIG_SMP
void rt_schedule_on_schedule_ipi(void)
{
	DECLARE_RT_CURRENT;
	RT_TASK *task, *new_task;
	int prio, preempt;
	unsigned long flags;

        rtai_hw_lock(flags);
	ASSIGN_RT_CURRENT;

	sched_rqsted[cpuid] = 1;
	prio = RT_SCHED_LINUX_PRIORITY;
	task = new_task = &rt_linux_task;

	sched_get_global_lock(cpuid);
	RR_YIELD();
	if (oneshot_running) {

		rt_time_h = rdtsc() + rt_half_tick;
		wake_up_timed_tasks(cpuid);
		TASK_TO_SCHEDULE();
		RR_SETYT();

		RR_PREMP();
		task = &rt_linux_task;
		while ((task = task->tnext) != &rt_linux_task) {
			if (task->priority <= prio && task->resume_time < rt_times.intr_time) {
				rt_times.intr_time = task->resume_time;
				preempt = 1;
				break;
			}
		}
		if (preempt) {
			RTIME now;
			int delay;
			delay = (int)(rt_times.intr_time - (now = rdtsc())) - tuned.latency;
			if (delay >= tuned.setup_time_TIMER_CPUNIT) {
				delay = imuldiv(delay, TIMER_FREQ, tuned.cpu_freq);
			} else {
				delay = tuned.setup_time_TIMER_UNIT;
				rt_times.intr_time = now + (tuned.setup_time_TIMER_CPUNIT);
			}
			rt_set_timer_delay(delay);
		}
	} else {
		TASK_TO_SCHEDULE();
		RR_SETYT();
	}
	sched_release_global_lock(cpuid);

	if (new_task != rt_current) {
		if (USE_RTAI_TASKS && (!new_task->lnxtsk || !rt_current->lnxtsk)) {
			if (rt_current->lnxtsk) {
				LOCK_LINUX(cpuid);
				save_cr0_and_clts(linux_cr0);
				rt_linux_task.nextp = (void *)rt_current;
			} else if (new_task->lnxtsk) {
				rt_linux_task.prevp = (void *)new_task;
				new_task = (void *)rt_linux_task.nextp;
			}
			KEXECTIME();
			rt_exchange_tasks(rt_smp_current[cpuid], new_task);
			if (rt_current->lnxtsk) {
				UNLOCK_LINUX(cpuid);
				restore_cr0(linux_cr0);
				if (rt_current != (void *)rt_linux_task.prevp) {
					new_task = (void *)rt_linux_task.prevp;
					goto schedlnxtsk;
				}
			} else if (rt_current->uses_fpu) {
				enable_fpu();
				if (rt_current != fpu_task) {
					save_fpenv(fpu_task->fpu_reg);
					fpu_task = rt_current;
					restore_fpenv(fpu_task->fpu_reg);
				}
			}
			if (rt_current->signal) {
				(*rt_current->signal)();
			}
			goto sched_exit;
		}
schedlnxtsk:
		if (new_task->is_hard || rt_current->is_hard) {
			struct task_struct *prev;
			if (!rt_current->is_hard) {
				LOCK_LINUX(cpuid);
				rt_linux_task.lnxtsk = prev = rtai_get_root_current(cpuid);
			} else {
				prev = rt_current->lnxtsk;
			}
			rt_smp_current[cpuid] = new_task;
			UEXECTIME();
			lxrt_context_switch(prev, new_task->lnxtsk, cpuid);
	                if (rt_current->signal) {
                        	rt_current->signal();
                	}
			if (!rt_current->is_hard) {
				UNLOCK_LINUX(cpuid);
			} else {
				if (prev->used_math) {
					restore_fpu(prev);
				}
			}
		}
	}
 sched_exit:
	rtai_cli();
        rtai_hw_unlock(flags);
}
#endif

#define enq_soft_ready_task(ready_task) \
do { \
	RT_TASK *task = rt_smp_linux_task[cpuid].rnext; \
	while (ready_task->priority >= task->priority) { \
		if ((task = task->rnext)->priority < 0) break; \
	} \
	task->rprev = (ready_task->rprev = task->rprev)->rnext = ready_task; \
	ready_task->rnext = task; \
} while (0)

void rt_schedule(void)
{
	DECLARE_RT_CURRENT;
	RT_TASK *task, *new_task;
	int prio, preempt;
	unsigned long flags;

        rtai_hw_lock(flags);
	ASSIGN_RT_CURRENT;

	sched_rqsted[cpuid] = 1;
	prio = RT_SCHED_LINUX_PRIORITY;
	task = new_task = &rt_linux_task;

	RR_YIELD();
	if (oneshot_running) {
#ifndef __USE_APIC__
		int islnx;
#endif

		rt_time_h = rdtsc() + rt_half_tick;
		wake_up_timed_tasks(cpuid);
		TASK_TO_SCHEDULE();
		RR_SETYT();

		RR_PREMP();
		task = &rt_linux_task;
		while ((task = task->tnext) != &rt_linux_task) {
			if (task->priority <= prio && task->resume_time < rt_times.intr_time) {
				rt_times.intr_time = task->resume_time;
				preempt = 1;
				break;
			}
		}
#ifdef __USE_APIC__
		if (preempt) {
			RTIME now;
			int delay;
#else
		if ((islnx = (prio == RT_SCHED_LINUX_PRIORITY)) || preempt) {
			RTIME now;
			int delay;
			if (islnx && !shot_fired) {
				RTIME linux_intr_time;
				linux_intr_time = rt_times.linux_time > rt_times.tick_time ? rt_times.linux_time : rt_times.tick_time + rt_times.linux_tick;
				if (linux_intr_time < rt_times.intr_time) {
					rt_times.intr_time = linux_intr_time;
					shot_fired = 1;
				}
			}
#endif
			delay = (int)(rt_times.intr_time - (now = rdtsc())) - tuned.latency;
			if (delay >= tuned.setup_time_TIMER_CPUNIT) {
				delay = imuldiv(delay, TIMER_FREQ, tuned.cpu_freq);
			} else {
				delay = tuned.setup_time_TIMER_UNIT;
				rt_times.intr_time = now + (tuned.setup_time_TIMER_CPUNIT);
			}
			rt_set_timer_delay(delay);
		}
	} else {
		TASK_TO_SCHEDULE();
		RR_SETYT();
	}
	sched_release_global_lock(cpuid);

	if (new_task != rt_current) {
		if (USE_RTAI_TASKS && (!new_task->lnxtsk || !rt_current->lnxtsk)) {
			if (rt_current->lnxtsk) {
				LOCK_LINUX(cpuid);
				save_cr0_and_clts(linux_cr0);
				rt_linux_task.nextp = (void *)rt_current;
			} else if (new_task->lnxtsk) {
				rt_linux_task.prevp = (void *)new_task;
				new_task = (void *)rt_linux_task.nextp;
			}
			KEXECTIME();
			rt_exchange_tasks(rt_smp_current[cpuid], new_task);
			if (rt_current->lnxtsk) {
				UNLOCK_LINUX(cpuid);
				restore_cr0(linux_cr0);
				if (rt_current != (void *)rt_linux_task.prevp) {
					new_task = (void *)rt_linux_task.prevp;
					goto schedlnxtsk;
				}
			} else if (rt_current->uses_fpu) {
				enable_fpu();
				if (rt_current != fpu_task) {
					save_fpenv(fpu_task->fpu_reg);
					fpu_task = rt_current;
					restore_fpenv(fpu_task->fpu_reg);
				}
			}
			if (rt_current->signal) {
				(*rt_current->signal)();
			}
			goto sched_exit;
		}
schedlnxtsk:
		rt_smp_current[cpuid] = new_task;
		if (new_task->is_hard || rt_current->is_hard) {
			struct task_struct *prev;
			if (!rt_current->is_hard) {
				LOCK_LINUX(cpuid);
				rt_linux_task.lnxtsk = prev = adp_cpu_current[cpuid] != adp_root ? rtai_get_root_current(cpuid) : current;
			} else {
				prev = rt_current->lnxtsk;
			}
			UEXECTIME();
			lxrt_context_switch(prev, new_task->lnxtsk, cpuid);
	                if (rt_current->signal) {
                        	rt_current->signal();
                	}
			if (!rt_current->is_hard) {
				UNLOCK_LINUX(cpuid);
				if (rt_current->state != RT_SCHED_READY) {
					goto sched_soft;
				}
			} else {
				if (prev->used_math) {
					restore_fpu(prev);
				}
				if (rt_current->force_soft) {
					make_current_soft(rt_current, cpuid);
				}	
			}
		} else {
sched_soft:
			UNLOCK_LINUX(cpuid);
			rt_global_sti();
        		rtai_hw_unlock(flags);
			schedule();
        		rtai_hw_lock(flags);
			rt_global_cli();
			rt_current->state = (rt_current->state & ~RT_SCHED_SFTRDY) | RT_SCHED_READY;
			LOCK_LINUX(cpuid);
			enq_soft_ready_task(rt_current);
			rt_smp_current[cpuid] = rt_current;
        	}
	}
 sched_exit:
	rtai_cli();
        rtai_hw_unlock(flags);
}


void rt_spv_RMS(int cpuid)
{
	RT_TASK *task;
	int prio;
	if (cpuid < 0 || cpuid >= num_online_cpus()) {
		cpuid = hard_cpu_id();
	}
	prio = 0;
	task = &rt_linux_task;
	while ((task = task->next)) {
		RT_TASK *task, *htask;
		RTIME period;
		htask = 0;
		task = &rt_linux_task;
		period = RT_TIME_END;
		while ((task = task->next)) {
			if (task->priority >= 0 && task->policy >= 0 && task->period && task->period < period) {
				period = (htask = task)->period;
			}
		}
		if (htask) {
			htask->priority = -1;
			htask->base_priority = prio++;
		} else {
			goto ret;
		}
	}
ret:	task = &rt_linux_task;
	while ((task = task->next)) {
		if (task->priority < 0) {
			task->priority = task->base_priority;
		}
	}
	return;
}


void rt_sched_lock(void)
{
	DECLARE_RT_CURRENT;
	unsigned long flags;

	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if (rt_current->priority >= 0) {
		rt_current->sched_lock_priority = rt_current->priority;
		sched_rqsted[cpuid] = rt_current->priority = -1;
	} else {
		rt_current->priority--;
	}
	rt_global_restore_flags(flags);
}


void rt_sched_unlock(void)
{
	DECLARE_RT_CURRENT;
	unsigned long flags;

	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if (rt_current->priority < 0 && !(++rt_current->priority)) {
		if ((rt_current->priority = rt_current->sched_lock_priority) != RT_SCHED_LINUX_PRIORITY) {
			(rt_current->rprev)->rnext = rt_current->rnext;
			(rt_current->rnext)->rprev = rt_current->rprev;
			enq_ready_task(rt_current);
		}
		if (sched_rqsted[cpuid] > 0) {
			rt_schedule();
		}
	}
	rt_global_restore_flags(flags);
}


int clr_rtext(RT_TASK *task)
{
	DECLARE_RT_CURRENT;
	unsigned long flags;
	QUEUE *q;

	if (task->magic != RT_TASK_MAGIC || task->priority == RT_SCHED_LINUX_PRIORITY) {
		return -EINVAL;
	}

	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if (!(task->owndres & SEMHLF) || task == rt_current || rt_current->priority == RT_SCHED_LINUX_PRIORITY) {
		call_exit_handlers(task);
		rem_timed_task(task);
		if (task->blocked_on) {
			(task->queue.prev)->next = task->queue.next;
			(task->queue.next)->prev = task->queue.prev;
			if (task->state & RT_SCHED_SEMAPHORE) {
				((SEM *)(task->blocked_on))->count++;
				if (((SEM *)(task->blocked_on))->type && ((SEM *)(task->blocked_on))->count > 1) {
					((SEM *)(task->blocked_on))->count = 1;
				}
			}
		}
		q = &(task->msg_queue);
		while ((q = q->next) != &(task->msg_queue)) {
			rem_timed_task(q->task);
			if ((q->task)->state != RT_SCHED_READY && ((q->task)->state &= ~(RT_SCHED_SEND | RT_SCHED_RPC | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
				enq_ready_task(q->task);
			}       
			(q->task)->blocked_on = 0;
		}       
                q = &(task->ret_queue);
                while ((q = q->next) != &(task->ret_queue)) {
			rem_timed_task(q->task);
                       	if ((q->task)->state != RT_SCHED_READY && ((q->task)->state &= ~(RT_SCHED_RETURN | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
				enq_ready_task(q->task);
			}       
			(q->task)->blocked_on = 0;
               	}
		if (!((task->prev)->next = task->next)) {
			rt_smp_linux_task[task->runnable_on_cpus].prev = task->prev;
		} else {
			(task->next)->prev = task->prev;
		}
		if (rt_smp_fpu_task[task->runnable_on_cpus] == task) {
			rt_smp_fpu_task[task->runnable_on_cpus] = rt_smp_linux_task + task->runnable_on_cpus;;
		}
		if (!task->lnxtsk) {
			frstk_srq.mp[frstk_srq.in] = task->stack_bottom;
			frstk_srq.in = (frstk_srq.in + 1) & (MAX_FRESTK_SRQ - 1);
			rt_pend_linux_srq(frstk_srq.srq);
		}
		task->magic = 0;
		rem_ready_task(task);
		task->state = 0;
		atomic_dec((atomic_t *)(tasks_per_cpu + task->runnable_on_cpus));
		if (task == rt_current) {
			rt_schedule();
		}
	} else {
		task->suspdepth = -0x7FFFFFFF;
	}
	rt_global_restore_flags(flags);
	return 0;
}


int rt_task_delete(RT_TASK *task)
{
	if (!clr_rtext(task)) {
		if (task->lnxtsk) {
			start_stop_kthread(task, 0, 0, 0, 0, 0, 0);
		}
	}
	return 0;
}


int rt_get_timer_cpu(void)
{
	return 1;
}


static void rt_timer_handler(void)
{
	DECLARE_RT_CURRENT;
	RT_TASK *task, *new_task;
	int prio, preempt; 
	unsigned long flags;

        rtai_hw_lock(flags);
	ASSIGN_RT_CURRENT;

	sched_rqsted[cpuid] = 1;
	prio = RT_SCHED_LINUX_PRIORITY;
	task = new_task = &rt_linux_task;

#ifdef CONFIG_X86_REMOTE_DEBUG
	if (oneshot_timer) {    // Resync after possibly hitting a breakpoint
		rt_times.intr_time = rdtsc();
	}
#endif
	rt_times.tick_time = rt_times.intr_time;
	rt_time_h = rt_times.tick_time + rt_half_tick;
#ifndef __USE_APIC__
	if (rt_times.tick_time >= rt_times.linux_time) {
		rt_times.linux_time += rt_times.linux_tick;
		update_linux_timer();
	}
#endif

	sched_get_global_lock(cpuid);
	wake_up_timed_tasks(cpuid);
	RR_YIELD();
	TASK_TO_SCHEDULE();
	RR_SETYT();

	if (oneshot_timer) {
#ifndef __USE_APIC__
		int islnx;
#endif
		shot_fired = 0;
		rt_times.intr_time = rt_times.tick_time + ONESHOT_SPAN;
		RR_PREMP();

		task = &rt_linux_task;
		while ((task = task->tnext) != &rt_linux_task) {
			if (task->priority <= prio && task->resume_time < rt_times.intr_time) {
				rt_times.intr_time = task->resume_time;
				preempt = 1;
				break;
			}
		}
#ifdef __USE_APIC__
		if (preempt) {
			RTIME now;
			int delay;
#else
		if ((islnx = (prio == RT_SCHED_LINUX_PRIORITY)) || preempt) {
			RTIME now;
			int delay;
			if (islnx) {
				RTIME linux_intr_time;
				linux_intr_time = rt_times.linux_time > rt_times.tick_time ? rt_times.linux_time : rt_times.tick_time + rt_times.linux_tick;
				if (linux_intr_time < rt_times.intr_time) {
					rt_times.intr_time = linux_intr_time;
					shot_fired = 1;
				}
			}
#endif
			delay = (int)(rt_times.intr_time - (now = rdtsc())) - tuned.latency;
			if (delay >= tuned.setup_time_TIMER_CPUNIT) {
				delay = imuldiv(delay, TIMER_FREQ, tuned.cpu_freq);
			} else {
				delay = tuned.setup_time_TIMER_UNIT;
				rt_times.intr_time = now + (tuned.setup_time_TIMER_CPUNIT);
			}
			rt_set_timer_delay(delay);
		}
	} else {
		rt_times.intr_time += rt_times.periodic_tick;
                rt_set_timer_delay(0);
	}
	sched_release_global_lock(cpuid);

	if (new_task != rt_current) {
		if (USE_RTAI_TASKS && (!new_task->lnxtsk || !rt_current->lnxtsk)) {
			if (rt_current->lnxtsk) {
				LOCK_LINUX(cpuid);
				save_cr0_and_clts(linux_cr0);
				rt_linux_task.nextp = (void *)rt_current;
			} else if (new_task->lnxtsk) {
				rt_linux_task.prevp = (void *)new_task;
				new_task = (void *)rt_linux_task.nextp;
			}
			KEXECTIME();
			rt_exchange_tasks(rt_smp_current[cpuid], new_task);
			if (rt_current->lnxtsk) {
				UNLOCK_LINUX(cpuid);
				restore_cr0(linux_cr0);
				if (rt_current != (void *)rt_linux_task.prevp) {
					new_task = (void *)rt_linux_task.prevp;
					goto schedlnxtsk;
				}
			} else if (rt_current->uses_fpu) {
				enable_fpu();
				if (rt_current != fpu_task) {
					save_fpenv(fpu_task->fpu_reg);
					fpu_task = rt_current;
					restore_fpenv(fpu_task->fpu_reg);
				}
			}
			if (rt_current->signal) {
				(*rt_current->signal)();
			}	

			goto sched_exit;
		}
schedlnxtsk:
		if (new_task->is_hard || rt_current->is_hard) {
			struct task_struct *prev;
			if (!rt_current->is_hard) {
				LOCK_LINUX(cpuid);
				rt_linux_task.lnxtsk = prev = rtai_get_root_current(cpuid);
			} else {
				prev = rt_current->lnxtsk;
			}
			rt_smp_current[cpuid] = new_task;
			UEXECTIME();
			lxrt_context_switch(prev, new_task->lnxtsk, cpuid);
			if (rt_current->signal) {
        	       	        rt_current->signal();
                	}
			if (!rt_current->is_hard) {
				UNLOCK_LINUX(cpuid);
			} else {
				if (prev->used_math) {
					restore_fpu(prev);
				}
			}
		}
        }
 sched_exit:
	rtai_cli();
        rtai_hw_unlock(flags);
}


#ifndef __USE_APIC__
static irqreturn_t recover_jiffies(int irq, void *dev_id, struct pt_regs *regs)
{
	rt_global_cli();
	if (linux_times->tick_time >= linux_times->linux_time) {
		linux_times->linux_time += linux_times->linux_tick;
		update_linux_timer();
	}
	rt_global_sti();
	return RTAI_LINUX_IRQ_HANDLED;
} 
#endif


int rt_is_hard_timer_running(void) 
{ 
    int cpuid, running;
	for (running = cpuid = 0; cpuid < num_online_cpus(); cpuid++) {
		if (rt_time_h) {
		running |= (1 << cpuid);
		}
	}
	return running;
}


void rt_set_periodic_mode(void) 
{ 
	int cpuid;
	stop_rt_timer();
	for (cpuid = 0; cpuid < NR_RT_CPUS; cpuid++) {
		oneshot_timer = oneshot_running = 0;
	}
}


void rt_set_oneshot_mode(void)
{ 
	int cpuid;
	stop_rt_timer();
	for (cpuid = 0; cpuid < NR_RT_CPUS; cpuid++) {
		oneshot_timer = 1;
	}
}


#ifdef __USE_APIC__

void start_rt_apic_timers(struct apic_timer_setup_data *setup_data, unsigned int rcvr_jiffies_cpuid)
{
	unsigned long flags, cpuid;

	rt_request_apic_timers(rt_timer_handler, setup_data);
	flags = rt_global_save_flags_and_cli();
	for (cpuid = 0; cpuid < NR_RT_CPUS; cpuid++) {
		if (setup_data[cpuid].mode > 0) {
			oneshot_timer = oneshot_running = 0;
			tuned.timers_tol[cpuid] = rt_half_tick = (rt_times.periodic_tick + 1)>>1;
		} else {
			oneshot_timer = oneshot_running = 1;
			tuned.timers_tol[cpuid] = rt_half_tick = (tuned.latency + 1)>>1;
		}
		rt_time_h = rt_times.tick_time + rt_half_tick;
		shot_fired = 1;
	}
	linux_times = rt_smp_times + (rcvr_jiffies_cpuid < NR_RT_CPUS ? rcvr_jiffies_cpuid : 0);
	rt_global_restore_flags(flags);
}


RTIME start_rt_timer(int period)
{
	int cpuid;
	struct apic_timer_setup_data setup_data[NR_RT_CPUS];
	for (cpuid = 0; cpuid < NR_RT_CPUS; cpuid++) {
		setup_data[cpuid].mode = oneshot_timer ? 0 : 1;
		setup_data[cpuid].count = count2nano(period);
	}
	start_rt_apic_timers(setup_data, hard_cpu_id());
	return period;
}


void stop_rt_timer(void)
{
	unsigned long flags;
	int cpuid;
	rt_free_apic_timers();
	flags = rt_global_save_flags_and_cli();
	for (cpuid = 0; cpuid < NR_RT_CPUS; cpuid++) {
		oneshot_timer = oneshot_running = 0;
	}
	rt_global_restore_flags(flags);
	rt_busy_sleep(10000000);
}

#else


RTIME start_rt_timer(int period)
{
#define cpuid 0
#undef rt_times

        unsigned long flags;
        flags = rt_global_save_flags_and_cli();
        if (oneshot_timer) {
                rt_request_timer(rt_timer_handler, 0, 0);
                tuned.timers_tol[0] = rt_half_tick = (tuned.latency + 1)>>1;
                oneshot_running = shot_fired = 1;
        } else {
                rt_request_timer(rt_timer_handler, period > LATCH? LATCH: period, 0);
                tuned.timers_tol[0] = rt_half_tick = (rt_times.periodic_tick + 1)>>1;
        }
	rt_smp_times[cpuid].linux_tick    = rt_times.linux_tick;
	rt_smp_times[cpuid].tick_time     = rt_times.tick_time;
	rt_smp_times[cpuid].intr_time     = rt_times.intr_time;
	rt_smp_times[cpuid].linux_time    = rt_times.linux_time;
	rt_smp_times[cpuid].periodic_tick = rt_times.periodic_tick;
        rt_time_h = rt_times.tick_time + rt_half_tick;
	linux_times = rt_smp_times;
        rt_global_restore_flags(flags);
        rt_request_linux_irq(TIMER_8254_IRQ, recover_jiffies, "rtai_jif_chk", recover_jiffies);
        return period;

#undef cpuid
#define rt_times (rt_smp_times[cpuid])
}


void start_rt_apic_timers(struct apic_timer_setup_data *setup_mode, unsigned int rcvr_jiffies_cpuid)
{
	int cpuid, period;

	period = 0;
	for (cpuid = 0; cpuid < NR_RT_CPUS; cpuid++) {
		period += setup_mode[cpuid].mode;
	}
	if (period == NR_RT_CPUS) {
		period = 2000000000;
		for (cpuid = 0; cpuid < NR_RT_CPUS; cpuid++) {
			if (setup_mode[cpuid].count < period) {
				period = setup_mode[cpuid].count;
			}
		}
		start_rt_timer(nano2count(period));
	} else {
		rt_set_oneshot_mode();
		start_rt_timer(0);
	}
}


void stop_rt_timer(void)
{
        unsigned long flags;
        rt_free_linux_irq(TIMER_8254_IRQ, recover_jiffies);
        rt_free_timer();
        flags = rt_global_save_flags_and_cli();
	rt_smp_oneshot_timer[0] = rt_smp_oneshot_running[0] = 0;
        rt_global_restore_flags(flags);
        rt_busy_sleep(10000000);
}

#endif

RTIME start_rt_timer_cpuid(int period, int cpuid)
{
	return start_rt_timer(period);
}

int rt_sched_type(void)
{
	return RT_SCHED_MUP;
}


void rt_preempt_always(int yes_no)
{
	return;
}


void rt_preempt_always_cpuid(int yes_no, unsigned int cpuid)
{
	return;
}


RT_TRAP_HANDLER rt_set_task_trap_handler( RT_TASK *task, unsigned int vec, RT_TRAP_HANDLER handler)
{
	RT_TRAP_HANDLER old_handler;

	if (!task || (vec >= RTAI_NR_TRAPS)) {
		return (RT_TRAP_HANDLER) -EINVAL;
	}
	old_handler = task->task_trap_handler[vec];
	task->task_trap_handler[vec] = handler;
	return old_handler;
}

static int OneShot = ONE_SHOT;
MODULE_PARM(OneShot, "i");

static int Latency = TIMER_LATENCY;
MODULE_PARM(Latency, "i");

static int SetupTimeTIMER = TIMER_SETUP_TIME;
MODULE_PARM(SetupTimeTIMER, "i");

extern void krtai_objects_release(void);

static void frstk_srq_handler(void)
{
        while (frstk_srq.out != frstk_srq.in) {
		sched_free(frstk_srq.mp[frstk_srq.out]);
		frstk_srq.out = (frstk_srq.out + 1) & (MAX_FRESTK_SRQ - 1);
	}
}

static void nihil(void) { };
struct rt_fun_entry rt_fun_lxrt[MAX_LXRT_FUN];

void reset_rt_fun_entries(struct rt_native_fun_entry *entry)
{
	while (entry->fun.fun) {
		if (entry->index >= MAX_LXRT_FUN) {
			rt_printk("*** RESET ENTRY %d FOR USER SPACE CALLS EXCEEDS ALLOWD TABLE SIZE %d, NOT USED ***\n", entry->index, MAX_LXRT_FUN);
		} else {
			rt_fun_lxrt[entry->index] = (struct rt_fun_entry){ 1, nihil };
		}
		entry++;
        }
}

int set_rt_fun_entries(struct rt_native_fun_entry *entry)
{
	int error;
	error = 0;
	while (entry->fun.fun) {
		if (rt_fun_lxrt[entry->index].fun != nihil) {
			rt_printk("*** SUSPICIOUS ENTRY ASSIGNEMENT FOR USER SPACE CALL AT %d, DUPLICATED INDEX OR REPEATED INITIALIZATION ***\n", entry->index);
			error = -1;
		} else if (entry->index >= MAX_LXRT_FUN) {
			rt_printk("*** ASSIGNEMENT ENTRY %d FOR USER SPACE CALLS EXCEEDS ALLOWED TABLE SIZE %d, NOT USED ***\n", entry->index, MAX_LXRT_FUN);
			error = -1;
		} else {
			rt_fun_lxrt[entry->index] = entry->fun;
		}
		entry++;
        }
	if (error) {
		reset_rt_fun_entries(entry);
	}
	return 0;
}

void *rt_get_lxrt_fun_entry(int index) {
	return rt_fun_lxrt[index].fun;
}

static void lxrt_killall (void)

{
    int cpuid;

    stop_rt_timer();

    for (cpuid = 0; cpuid < NR_RT_CPUS; cpuid++)
	while (rt_linux_task.next)
	    rt_task_delete(rt_linux_task.next);
}

static int lxrt_notify_reboot (struct notifier_block *nb, unsigned long event, void *p)

{
    switch (event)
	{
	case SYS_DOWN:
	case SYS_HALT:
	case SYS_POWER_OFF:

	    /* FIXME: this is far too late. */
	    printk("LXRT: REBOOT NOTIFIED -- KILLING TASKS\n");
	    lxrt_killall();
	}

    return NOTIFY_DONE;
}

/* ++++++++++++++++++++++++++ TIME CONVERSIONS +++++++++++++++++++++++++++++ */

RTIME count2nano(RTIME counts)
{
	int sign;

	if (counts >= 0) {
		sign = 1;
	} else {
		sign = 0;
		counts = - counts;
	}
	counts = oneshot_timer_cpuid ?
		 llimd(counts, 1000000000, tuned.cpu_freq):
		 llimd(counts, 1000000000, TIMER_FREQ);
	return sign ? counts : - counts;
}


RTIME nano2count(RTIME ns)
{
	int sign;

	if (ns >= 0) {
		sign = 1;
	} else {
		sign = 0;
		ns = - ns;
	}
	ns =  oneshot_timer_cpuid ?
	      llimd(ns, tuned.cpu_freq, 1000000000) :
	      llimd(ns, TIMER_FREQ, 1000000000);
	return sign ? ns : - ns;
}

RTIME count2nano_cpuid(RTIME counts, unsigned int cpuid)
{
	int sign;

	if (counts >= 0) {
		sign = 1;
	} else {
		sign = 0;
		counts = - counts;
	}
	counts = oneshot_timer ?
		 llimd(counts, 1000000000, tuned.cpu_freq):
		 llimd(counts, 1000000000, TIMER_FREQ);
	return sign ? counts : - counts;
}


RTIME nano2count_cpuid(RTIME ns, unsigned int cpuid)
{
	int sign;

	if (ns >= 0) {
		sign = 1;
	} else {
		sign = 0;
		ns = - ns;
	}
	ns =  oneshot_timer ?
	      llimd(ns, tuned.cpu_freq, 1000000000) :
	      llimd(ns, TIMER_FREQ, 1000000000);
	return sign ? ns : - ns;
}

/* +++++++++++++++++++++++++++++++ TIMINGS ++++++++++++++++++++++++++++++++++ */

RTIME rt_get_time(void)
{
	int cpuid;
	return rt_smp_oneshot_timer[cpuid = hard_cpu_id()] ? rdtsc() : rt_smp_times[cpuid].tick_time;
}

RTIME rt_get_time_cpuid(unsigned int cpuid)
{
	return oneshot_timer ? rdtsc(): rt_times.tick_time;
}

RTIME rt_get_time_ns(void)
{
	int cpuid = hard_cpu_id();
	return oneshot_timer ? llimd(rdtsc(), 1000000000, tuned.cpu_freq) :
	    		       llimd(rt_times.tick_time, 1000000000, TIMER_FREQ);
}

RTIME rt_get_time_ns_cpuid(unsigned int cpuid)
{
	return oneshot_timer ? llimd(rdtsc(), 1000000000, tuned.cpu_freq) :
			       llimd(rt_times.tick_time, 1000000000, TIMER_FREQ);
}

RTIME rt_get_cpu_time_ns(void)
{
	return llimd(rdtsc(), 1000000000, tuned.cpu_freq);
}

/* +++++++++++++++++++++++++++ SECRET BACK DOORS ++++++++++++++++++++++++++++ */

RT_TASK *rt_get_base_linux_task(RT_TASK **base_linux_tasks)
{
        int cpuid;
        for (cpuid = 0; cpuid < num_online_cpus(); cpuid++) {
                base_linux_tasks[cpuid] = rt_smp_linux_task + cpuid;
        }
        return rt_smp_linux_task;
}

RT_TASK *rt_alloc_dynamic_task(void)
{
#ifdef CONFIG_RTAI_MALLOC
        return rt_malloc(sizeof(RT_TASK)); // For VC's, proxies and C++ support.
#else
	return NULL;
#endif
}

/* +++++++++++++++++++++++++++ WATCHDOG SUPPORT ++++++++++++++++++++++++++++ */

RT_TASK **rt_register_watchdog(RT_TASK *wd, int cpuid)
{
    	RT_TASK *task;

	if (lxrt_wdog_task[cpuid]) return (RT_TASK**) -EBUSY;
	task = &rt_linux_task;
	while ((task = task->next)) {
		if (task != wd && task->priority == RT_SCHED_HIGHEST_PRIORITY) {
			return (RT_TASK**) -EBUSY;
		}
	}
	lxrt_wdog_task[cpuid] = wd;
	return (RT_TASK**) 0;
}

void rt_deregister_watchdog(RT_TASK *wd, int cpuid)
{
    	if (lxrt_wdog_task[cpuid] != wd) return;
	lxrt_wdog_task[cpuid] = NULL;
}

/* +++++++++++++++ SUPPORT FOR LINUX TASKS AND KERNEL THREADS +++++++++++++++ */

//#define ECHO_SYSW
#ifdef ECHO_SYSW
#define SYSW_DIAG_MSG(x) x
#else
#define SYSW_DIAG_MSG(x)
#endif

int lxrtmode, lxrtmodechoed;

int LxrtMode = 0;
MODULE_PARM(LxrtMode, "i");

static RT_TRAP_HANDLER lxrt_old_trap_handler;

#ifdef CONFIG_RTAI_FPU_SUPPORT
static void init_fpu(struct task_struct *tsk)
{
        init_xfpu();
        tsk->used_math = 1;
        set_tsk_used_fpu(tsk);
}
#else
static void init_fpu(struct task_struct *tsk) { }
#endif

struct fun_args { int a0; int a1; int a2; int a3; int a4; int a5; int a6; int a7; int a8; int a9; long long (*fun)(int, ...); };

void rt_schedule_soft(RT_TASK *rt_task)
{
	struct fun_args *funarg;
	int cpuid;

	if (rt_task->priority < BASE_SOFT_PRIORITY) {
		atomic_add(BASE_SOFT_PRIORITY, (atomic_t *)&rt_task->priority);
	}
	if (rt_task->base_priority < BASE_SOFT_PRIORITY) {
		atomic_add(BASE_SOFT_PRIORITY, (atomic_t *)&rt_task->base_priority);
	}
	funarg = (void *)rt_task->fun_args;
	if (lxrtmode) {
		void steal_from_linux(RT_TASK *);
		SYSW_DIAG_MSG(rt_printk("GOING HARD (LXRTMODE), PID = %d.\n", current->pid););

		steal_from_linux(rt_task);
		SYSW_DIAG_MSG(rt_printk("GONE TO HARD (LXRTMODE),  PID = %d.\n", current->pid););
		rt_task->retval = funarg->fun(funarg->a0, funarg->a1, funarg->a2, funarg->a3, funarg->a4, funarg->a5, funarg->a6, funarg->a7, funarg->a8, funarg->a9);
		return;
	}

	rt_global_cli();
	rt_task->state |= RT_SCHED_READY;
	while (rt_task->state != RT_SCHED_READY) {
		current->state = TASK_HARDREALTIME;
		rt_global_sti();
		schedule();
		rt_global_cli();
	}
	LOCK_LINUX(cpuid = hard_cpu_id());
	enq_soft_ready_task(rt_task);
	rt_smp_current[cpuid] = rt_task;
	rt_global_sti();
	rt_task->retval = funarg->fun(funarg->a0, funarg->a1, funarg->a2, funarg->a3, funarg->a4, funarg->a5, funarg->a6, funarg->a7, funarg->a8, funarg->a9);
	rt_global_cli();
	rt_task->state &= ~(RT_SCHED_READY | RT_SCHED_SFTRDY);
	(rt_task->rprev)->rnext = rt_task->rnext;
       	(rt_task->rnext)->rprev = rt_task->rprev;
	rt_smp_current[cpuid] = &rt_linux_task;
	rt_schedule();
	UNLOCK_LINUX(cpuid);
	rt_global_sti();
}

static inline void fast_schedule(RT_TASK *new_task, int cpuid)
{
	unsigned long flags;
	RT_TASK *rt_current;
	rtai_hw_lock(flags);
	new_task->state |= RT_SCHED_READY;
	enq_soft_ready_task(new_task);
	sched_release_global_lock(cpuid);
	LOCK_LINUX(cpuid);
	(rt_current = &rt_linux_task)->lnxtsk = kthreadb[cpuid];
	UEXECTIME();
	rt_smp_current[cpuid] = new_task;
	lxrt_context_switch(rt_current->lnxtsk, new_task->lnxtsk, cpuid);
	UNLOCK_LINUX(cpuid);
	rtai_cli();
	rtai_hw_unlock(flags);
}

static void *task_to_make_hard[NR_RT_CPUS];
static void lxrt_migration_handler (unsigned virq)
{
	int cpuid;
	rt_global_cli();
	cpuid = hard_cpu_id();
	fast_schedule(task_to_make_hard[cpuid], cpuid);
	rt_global_sti();
	task_to_make_hard[cpuid] = NULL;
}

static void kthread_b(int cpuid)
{
	struct klist_t *klistp;
	RT_TASK *task;

	sprintf(current->comm, "RTAI_KTHRD_B:%d", cpuid);
	put_current_on_cpu(cpuid);
	kthreadb_q[cpuid].task = kthreadb[cpuid] = current;
	klistp = &klistb[hard_cpu_id()];
	rtai_set_linux_task_priority(current, SCHED_FIFO, KTHREAD_B_PRIO);
	sigfillset(&current->blocked);
	up(&resem[cpuid]);
	while (!endkthread) {
		current->state = TASK_UNINTERRUPTIBLE;
		schedule();
		while (klistp->out != klistp->in) {
    			task = klistp->task[klistp->out];
			klistp->out = (klistp->out + 1) & (MAX_WAKEUP_SRQ - 1);
		/* We must do this sync to be sure the task to be made 
		   hard has gone from Linux for sure. In 2.6.x it is not 
		   needed because it is easily possible to force the syncing 
		   with the default wake up function used with wait queues,
		   but we keep it anyhow for paranoia cautiousness. */
			while ((task->lnxtsk)->run_list.next != LIST_POISON1 || (task->lnxtsk)->run_list.prev != LIST_POISON2) {
				current->state = TASK_UNINTERRUPTIBLE;
				schedule_timeout(1);
			}
		/* end of sync comment */
			/* Escalate the request to the RTAI domain */
    			task_to_make_hard[cpuid] = task;
			adeos_hw_cli();
			adeos_trigger_irq(lxrt_migration_virq);
			adeos_hw_sti();
		/* let's use belt and laces */
			while (task_to_make_hard[cpuid]) {
				current->state = TASK_UNINTERRUPTIBLE;
				schedule_timeout(2);
			}
		}
	}
	kthreadb[cpuid] = 0;
}

static RT_TASK thread_task[NR_RT_CPUS];
static int rsvr_cnt[NR_RT_CPUS];

#define RESERVOIR 4
static int Reservoir = RESERVOIR;
MODULE_PARM(Reservoir, "i");

static int taskidx[NR_RT_CPUS];
static struct task_struct **taskav[NR_RT_CPUS];

static struct task_struct *__get_kthread(int cpuid)
{
	unsigned long flags;
	struct task_struct *p;

	flags = rt_global_save_flags_and_cli();
	if (taskidx[cpuid] > 0) {
		p = taskav[cpuid][--taskidx[cpuid]];
		rt_global_restore_flags(flags);
		return p;
	}
	rt_global_restore_flags(flags);
	return 0;
}


static void thread_fun(int cpuid) 
{
	void steal_from_linux(RT_TASK *);
	void give_back_to_linux(RT_TASK *);
	RT_TASK *task;

	rtai_set_linux_task_priority(current, SCHED_FIFO, KTHREAD_F_PRIO);
	sprintf(current->comm, "F:HARD:%d:%d", cpuid, ++rsvr_cnt[cpuid]);
	current->this_rt_task[0] = task = &thread_task[cpuid];
	current->this_rt_task[1] = task->lnxtsk = current;
	sigfillset(&current->blocked);
	put_current_on_cpu(cpuid);
	steal_from_linux(task);
	init_fpu(current);
	rt_task_suspend(task);
	current->comm[0] = 'U';
	task = (RT_TASK *)current->this_rt_task[0];
	task->exectime[1] = rdtsc();
	((void (*)(int))task->max_msg_size[0])(task->max_msg_size[1]);
	rt_task_suspend(task);
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,7)
static inline _syscall3(pid_t,waitpid,pid_t,pid,int *,wait_stat,int,options)
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(2,6,7) */


static void kthread_m(int cpuid)
{
	struct task_struct *lnxtsk;
	struct klist_t *klistp;
	RT_TASK *task;

	(task = &thread_task[cpuid])->magic = RT_TASK_MAGIC;
	task->runnable_on_cpus = cpuid;
	sprintf(current->comm, "RTAI_KTHRD_M:%d", cpuid);
	put_current_on_cpu(cpuid);
	kthreadm[cpuid] = current;
	klistp = &klistm[cpuid];
	rtai_set_linux_task_priority(current, SCHED_FIFO, KTHREAD_M_PRIO);
	sigfillset(&current->blocked);
	up(&resem[cpuid]);
	while (!endkthread) {
		current->state = TASK_UNINTERRUPTIBLE;
		schedule();
		while (klistp->out != klistp->in) {
			unsigned long hard, flags;
			flags = rt_global_save_flags_and_cli();
			hard = (unsigned long)(lnxtsk = klistp->task[klistp->out]);
			if (hard > 1) {
				lnxtsk->state = TASK_ZOMBIE;
				lnxtsk->exit_signal = SIGCHLD;
				rt_global_sti();
				waitpid(lnxtsk->pid, 0, 0);
				rt_global_cli();
			} else {
				if (taskidx[cpuid] < Reservoir) {
					task->suspdepth = task->state = 0;
					rt_global_sti();
					kernel_thread((void *)thread_fun, (void *)cpuid, 0);
					while (task->state != (RT_SCHED_READY | RT_SCHED_SUSPENDED)) {
						current->state = TASK_INTERRUPTIBLE;
						schedule_timeout(2);
					}
					rt_global_cli();
					taskav[cpuid][taskidx[cpuid]++] = (void *)task->lnxtsk;
				}
				klistp->out = (klistp->out + 1) & (MAX_WAKEUP_SRQ - 1);
				if (hard) {
					rt_task_resume((void *)klistp->task[klistp->out]);
				} else {
					rt_global_sti();
					up(&resem[cpuid]);
					rt_global_cli();
				}
			}
			klistp->out = (klistp->out + 1) & (MAX_WAKEUP_SRQ - 1);
			rt_global_restore_flags(flags);
		}
	}
	kthreadm[cpuid] = 0;
}

void steal_from_linux(RT_TASK *rt_task)
{
	int cpuid;
	struct klist_t *klistp;
	klistp = &klistb[cpuid = rt_task->runnable_on_cpus];
	rtai_cli();
	klistp->task[klistp->in] = rt_task;
	klistp->in = (klistp->in + 1) & (MAX_WAKEUP_SRQ - 1);
	rtai_sti();
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	wake_up_process(kthreadb[cpuid]);
#else /* KERNEL_VERSION >= 2.6.0 */
	default_wake_function(&kthreadb_q[cpuid], TASK_HARDREALTIME, 1, 0);
#endif /* KERNEL_VERSION < 2.6.0 */
	current->state = TASK_HARDREALTIME;
	schedule();
	rt_task->is_hard = 1;
	if (!rt_task->exectime[1]) {
		rt_task->exectime[1] = rdtsc();
	}
	if (current->used_math) {
		restore_fpu(current);
	}
	if (rt_task->priority >= BASE_SOFT_PRIORITY) {
		atomic_sub(BASE_SOFT_PRIORITY, (atomic_t *)&rt_task->priority);
	}
	if (rt_task->base_priority >= BASE_SOFT_PRIORITY) {
		atomic_sub(BASE_SOFT_PRIORITY, (atomic_t *)&rt_task->base_priority);
	}
	rtai_sti();
}

void give_back_to_linux(RT_TASK *rt_task)
{
	rt_global_cli();
	(rt_task->rprev)->rnext = rt_task->rnext;
	(rt_task->rnext)->rprev = rt_task->rprev;
	rt_task->state = 0;
	wake_up_srq.task[wake_up_srq.in] = rt_task->lnxtsk;
	wake_up_srq.in = (wake_up_srq.in + 1) & (MAX_WAKEUP_SRQ - 1);
	rt_pend_linux_srq(wake_up_srq.srq);
	rt_schedule();
	rt_task->is_hard = 0;
	rt_global_sti();
	/* Perform Linux's scheduling tail now since we woke up
	   outside the regular schedule() point. */
	__adeos_schedule_back_root(rt_task->lnxtsk);
}

static struct task_struct *get_kthread(int get, int cpuid, void *lnxtsk)
{
	struct task_struct *kthread;
	struct klist_t *klistp;
	RT_TASK *this_task;
	int hard;

	klistp = &klistm[cpuid];
	if (get) {
		while (!(kthread = __get_kthread(cpuid))) {
			this_task = rt_smp_current[hard_cpu_id()];
			rt_global_cli();
			klistp->task[klistp->in] = (void *)(hard = this_task->is_hard > 0 ? 1 : 0);
			klistp->in = (klistp->in + 1) & (MAX_WAKEUP_SRQ - 1);
			klistp->task[klistp->in] = (void *)this_task;
			klistp->in = (klistp->in + 1) & (MAX_WAKEUP_SRQ - 1);
			wake_up_srq.task[wake_up_srq.in] = kthreadm[cpuid];
			wake_up_srq.in = (wake_up_srq.in + 1) & (MAX_WAKEUP_SRQ - 1);
			rt_pend_linux_srq(wake_up_srq.srq);
			rt_global_sti();
        		if (hard) {
				rt_task_suspend(this_task);
			} else {
				down(&resem[cpuid]);
			}
		}
		rt_global_cli();
		klistp->task[klistp->in] = 0;
		klistp->in = (klistp->in + 1) & (MAX_WAKEUP_SRQ - 1);
		klistp->task[klistp->in] = 0;
	} else {
		kthread = 0;
		rt_global_cli();
		klistp->task[klistp->in] = lnxtsk;
	}
	klistp->in = (klistp->in + 1) & (MAX_WAKEUP_SRQ - 1);
	wake_up_srq.task[wake_up_srq.in] = kthreadm[cpuid];
	wake_up_srq.in = (wake_up_srq.in + 1) & (MAX_WAKEUP_SRQ - 1);
	rt_pend_linux_srq(wake_up_srq.srq);
	rt_global_sti();
	return kthread;
}

static void start_stop_kthread(RT_TASK *task, void (*rt_thread)(int), int data, int priority, int uses_fpu, void(*signal)(void), int runnable_on_cpus)
{
	if (rt_thread) {
		task->retval = set_rtext(task, priority, uses_fpu, signal, runnable_on_cpus, get_kthread(1, runnable_on_cpus, 0));
		task->max_msg_size[0] = (int)rt_thread;
		task->max_msg_size[1] = data;
	} else {
		get_kthread(0, task->runnable_on_cpus, task->lnxtsk);
	}
}

static void wake_up_srq_handler(void)
{
        while (wake_up_srq.out != wake_up_srq.in) {
#ifdef CONFIG_SMP
	/* This sync, SMP only, is needed to avoid waking up a ready Linux 
	soft process, using an RTAI API, before it has left the LXRT (for
	reentering it again through Linux schedule() */
		rt_global_cli();
		rt_global_sti();
	/* end of sync comment */
#endif
		wake_up_process(wake_up_srq.task[wake_up_srq.out]);
                wake_up_srq.out = (wake_up_srq.out + 1) & (MAX_WAKEUP_SRQ - 1);
        }
	set_need_resched();
}

static int lxrt_handle_trap(int vec, int signo, struct pt_regs *regs, void *dummy_data)
{
	RT_TASK *rt_task;

	rt_task = rt_smp_current[(int)dummy_data];
	if (USE_RTAI_TASKS && !rt_task->lnxtsk) {
		if (rt_task->task_trap_handler[vec]) {
			return rt_task->task_trap_handler[vec](vec, signo, regs, rt_task);
		}
		rt_printk("Default Trap Handler: vector %d: Suspend RT task %p\n", vec, rt_task);
		rt_task_suspend(rt_task);
		return 1;
	}

	if (rt_task->is_hard == 1) {
		if (!lxrtmodechoed) {
			lxrtmodechoed = 1;
			rt_printk("\nLXRT CHANGED MODE (TRAP, LxrtMode %d), PID = %d\n", LxrtMode, (rt_task->lnxtsk)->pid);
		}
		SYSW_DIAG_MSG(rt_printk("\nFORCING IT SOFT (TRAP), PID = %d.\n", (rt_task->lnxtsk)->pid););
		lxrtmode = 2 & LxrtMode;
	        give_back_to_linux(rt_task);
		rt_task->is_hard = lxrtmode;
		SYSW_DIAG_MSG(rt_printk("FORCED IT SOFT (TRAP),  PID = %d.\n", (rt_task->lnxtsk)->pid););
	}

	return 0;
}

static int lxrt_handle_signal(struct task_struct *lnxtsk, int sig)
{
        RT_TASK *task = (RT_TASK *)lnxtsk->this_rt_task[0];
        if ((task->force_soft = task->is_hard == 1)) {
		rt_do_force_soft(task);
                return 0;
        }
        if (task->state) {
                lnxtsk->state = TASK_INTERRUPTIBLE;
	}
        return 1;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

static struct mmreq {
    int in, out, count;
#define MAX_MM 32  /* Should be more than enough (must be a power of 2). */
#define bump_mmreq(x) do { x = (x + 1) & (MAX_MM - 1); } while(0)
    struct mm_struct *mm[MAX_MM];
} lxrt_mmrqtab[NR_CPUS];

static void lxrt_intercept_schedule_head (adevinfo_t *evinfo)

{
    struct { struct task_struct *prev, *next; } *evdata = (__typeof(evdata))evinfo->evdata;
    struct task_struct *prev = evdata->prev;

    /* The SCHEDULE_HEAD event is sent by the (Adeosized) Linux kernel
       each time it's about to switch a process out. This hook is
       aimed at preventing the last active MM from being dropped
       during the LXRT real-time operations since it's a lengthy
       atomic operation. See kernel/sched.c (schedule()) for more. The
       MM dropping is simply postponed until the SCHEDULE_TAIL event
       is received, right after the incoming task has been switched
       in. */

    if (!prev->mm)
	{
	struct mmreq *p = lxrt_mmrqtab + task_cpu(prev);
	struct mm_struct *oldmm = prev->active_mm;
	BUG_ON(p->count >= MAX_MM);
	/* Prevent the MM from being dropped in schedule(), then pend
	   a request to drop it later in lxrt_intercept_schedule_tail(). */
	atomic_inc(&oldmm->mm_count);
	p->mm[p->in] = oldmm;
	bump_mmreq(p->in);
	p->count++;
	}

    adeos_propagate_event(evinfo);
}

#endif  /* KERNEL_VERSION < 2.6.0 */

static void lxrt_intercept_schedule_tail (adevinfo_t *evinfo)

{
    if (evinfo->domid == RTAI_DOMAIN_ID)
	/* About to resume after the transition to hard LXRT mode.  Do
	   _not_ propagate this event so that Linux's tail scheduling
	   won't be performed. */
	return;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
    {
    struct mmreq *p;

#ifdef CONFIG_PREEMPT
    preempt_disable();
#endif /* CONFIG_PREEMPT */

    p = lxrt_mmrqtab + smp_processor_id();

    while (p->out != p->in)
	{
	struct mm_struct *oldmm = p->mm[p->out];
	mmdrop(oldmm);
	bump_mmreq(p->out);
	p->count--;
	}

#ifdef CONFIG_PREEMPT
    preempt_enable();
#endif /* CONFIG_PREEMPT */
    }
#endif  /* KERNEL_VERSION < 2.6.0 */

    adeos_propagate_event(evinfo);
}

static void lxrt_intercept_signal (adevinfo_t *evinfo)
{
	struct { struct task_struct *task; int sig; } *evdata = (__typeof(evdata))evinfo->evdata;
	struct task_struct *task = evdata->task;
	if (task->ptd[0]) {
		if (!lxrt_handle_signal(task, evdata->sig))
		/* Don't propagate so that Linux won't 
		   further process the signal. */
		return;
	}
	adeos_propagate_event(evinfo);
}

static void lxrt_intercept_syscall(adevinfo_t *evinfo)
{
	adeos_declare_cpuid;

	adeos_propagate_event(evinfo);
    	if (evinfo->domid != RTAI_DOMAIN_ID) {
		return;
	}
	adeos_load_cpuid();
	if (test_bit(cpuid, &rtai_cpu_realtime)) {
		RT_TASK *task = rt_smp_current[cpuid];
		if (task->is_hard == 1) {
			if (!lxrtmodechoed) {
				lxrtmodechoed = 1;
				rt_printk("\nLXRT CHANGED MODE (SYSCALL, LxrtMode %d), PID = %d\n", LxrtMode, (task->lnxtsk)->pid);
			}
			SYSW_DIAG_MSG(rt_printk("\nFORCING IT SOFT (SYSCALL), PID = %d.\n", (task->lnxtsk)->pid););
			lxrtmode = 2 & LxrtMode;
			give_back_to_linux(task);
			task->is_hard = lxrtmode;
			SYSW_DIAG_MSG(rt_printk("FORCED IT SOFT (SYSCALL),  PID = %d.\n", (task->lnxtsk)->pid););
		}
	}
}

/* ++++++++++++++++++++++++++ SCHEDULER PROC FILE +++++++++++++++++++++++++++ */

#ifdef CONFIG_PROC_FS
/* -----------------------< proc filesystem section >-------------------------*/

static int rtai_read_sched(char *page, char **start, off_t off, int count,
                           int *eof, void *data)
{
	PROC_PRINT_VARS;
        int cpuid, i = 1;
	unsigned long t;
	RT_TASK *task;

	PROC_PRINT("\nRTAI LXRT Real Time Task Scheduler.\n\n");
	PROC_PRINT("    Calibrated CPU Frequency: %lu Hz\n", tuned.cpu_freq);
	PROC_PRINT("    Calibrated 8254 interrupt to scheduler latency: %d ns\n", imuldiv(tuned.latency - tuned.setup_time_TIMER_CPUNIT, 1000000000, tuned.cpu_freq));
	PROC_PRINT("    Calibrated one shot setup time: %d ns\n\n",
                  imuldiv(tuned.setup_time_TIMER_CPUNIT, 1000000000, tuned.cpu_freq));
	PROC_PRINT("Number of RT CPUs in system: %d\n\n", NR_RT_CPUS);

	PROC_PRINT("Priority  Period(ns)  FPU  Sig  State  CPU  Task  HD/SF  PID  RT_TASK *  TIME\n" );
	PROC_PRINT("------------------------------------------------------------------------------\n" );
        for (cpuid = 0; cpuid < NR_RT_CPUS; cpuid++) {
                task = &rt_linux_task;
/*
* Display all the active RT tasks and their state.
*
* Note: As a temporary hack the tasks are given an id which is
*       the order they appear in the task list, needs fixing!
*/
		while ((task = task->next)) {
/*
* The display for the task period is set to an integer (%d) as 64 bit
* numbers are not currently handled correctly by the kernel routines.
* Hence the period display will be wrong for time periods > ~4 secs.
*/
			t = 0;
			if ((!task->lnxtsk || task->is_hard) && task->exectime[1]) {
				unsigned long den = (unsigned long)llimd(rdtsc() - task->exectime[1], 10, tuned.cpu_freq);
				if (den) {
					t = 1000UL*(unsigned long)llimd(task->exectime[0], 10, tuned.cpu_freq)/den;
				}				
			}
			PROC_PRINT("%-10d %-11lu %-4s %-3s 0x%-3x  %1lu:%1lu   %-4d   %-4d %-4d  %p   %-lu\n",
                               task->priority,
                               (unsigned long)count2nano_cpuid(task->period, task->runnable_on_cpus),
                               task->uses_fpu ? "Yes" : "No",
                               task->signal ? "Yes" : "No",
                               task->state,
			       task->runnable_on_cpus, // cpuid,
			       CPUMASK((task->lnxtsk)->cpus_allowed),
                               i,
			       task->is_hard,
			       task->lnxtsk ? task->lnxtsk->pid : 0,
			       task, t);
			i++;
                } /* End while loop - display all RT tasks on a CPU. */

		PROC_PRINT("TIMED\n");
		task = &rt_linux_task;
		while ((task = task->tnext) != &rt_linux_task) {
			PROC_PRINT("> %p ", task);
		}
		PROC_PRINT("\nREADY\n");
		task = &rt_linux_task;
		while ((task = task->rnext) != &rt_linux_task) {
			PROC_PRINT("> %p ", task);
		}

        }  /* End for loop - display RT tasks on all CPUs. */

	PROC_PRINT_DONE;

}  /* End function - rtai_read_sched */


static int rtai_proc_sched_register(void) 
{
        struct proc_dir_entry *proc_sched_ent;


        proc_sched_ent = create_proc_entry("scheduler", S_IFREG|S_IRUGO|S_IWUSR, rtai_proc_root);
        if (!proc_sched_ent) {
                printk("Unable to initialize /proc/rtai/scheduler\n");
                return(-1);
        }
        proc_sched_ent->read_proc = rtai_read_sched;
        return(0);
}  /* End function - rtai_proc_sched_register */


static void rtai_proc_sched_unregister(void) 
{
        remove_proc_entry("scheduler", rtai_proc_root);
}  /* End function - rtai_proc_sched_unregister */

/* --------------------< end of proc filesystem section >---------------------*/
#endif /* CONFIG_PROC_FS */

/* ++++++++++++++ SCHEDULER ENTRIES AND RELATED INITIALISATION ++++++++++++++ */

static struct rt_native_fun_entry rt_sched_entries[] = {
	{ { 0, rt_named_task_init },		    NAMED_TASK_INIT },  
	{ { 0, rt_named_task_init_cpuid },	    NAMED_TASK_INIT_CPUID },  
	{ { 0, rt_named_task_delete },	 	    NAMED_TASK_DELETE },  
	{ { 1, rt_task_yield },			    YIELD },  
	{ { 1, rt_task_suspend },		    SUSPEND },
	{ { 1, rt_task_resume },		    RESUME },
	{ { 1, rt_task_make_periodic },		    MAKE_PERIODIC },
	{ { 1, rt_task_wait_period },		    WAIT_PERIOD },
	{ { 1, rt_sleep },			    SLEEP },
	{ { 1, rt_sleep_until },		    SLEEP_UNTIL },
	{ { 0, start_rt_timer },		    START_TIMER },
	{ { 0, stop_rt_timer },			    STOP_TIMER },
	{ { 0, rt_get_time },			    GET_TIME },
	{ { 0, count2nano },			    COUNT2NANO },
	{ { 0, nano2count },			    NANO2COUNT },
	{ { 0, rt_busy_sleep },			    BUSY_SLEEP },
	{ { 0, rt_set_periodic_mode },		    SET_PERIODIC_MODE },
	{ { 0, rt_set_oneshot_mode },		    SET_ONESHOT_MODE },
	{ { 0, rt_task_signal_handler },	    SIGNAL_HANDLER  },
	{ { 0, rt_task_use_fpu },		    TASK_USE_FPU },
	{ { 0, rt_linux_use_fpu },		    LINUX_USE_FPU },
	{ { 0, rt_preempt_always },		    PREEMPT_ALWAYS_GEN },
	{ { 0, rt_get_time_ns },		    GET_TIME_NS },
	{ { 0, rt_get_cpu_time_ns },		    GET_CPU_TIME_NS },
	{ { 0, rt_set_runnable_on_cpus },	    SET_RUNNABLE_ON_CPUS },
	{ { 0, rt_set_runnable_on_cpuid },	    SET_RUNNABLE_ON_CPUID },
	{ { 0, rt_get_timer_cpu },		    GET_TIMER_CPU },
	{ { 0, start_rt_apic_timers },		    START_RT_APIC_TIMERS },
	{ { 0, rt_preempt_always_cpuid },	    PREEMPT_ALWAYS_CPUID },
	{ { 0, count2nano_cpuid },		    COUNT2NANO_CPUID },
	{ { 0, nano2count_cpuid },		    NANO2COUNT_CPUID },
	{ { 0, rt_get_time_cpuid },		    GET_TIME_CPUID },
	{ { 0, rt_get_time_ns_cpuid },		    GET_TIME_NS_CPUID },
	{ { 1, rt_task_make_periodic_relative_ns }, MAKE_PERIODIC_NS },
	{ { 0, rt_set_sched_policy },		    SET_SCHED_POLICY },
	{ { 1, rt_task_set_resume_end_times },	    SET_RESUME_END },
	{ { 0, rt_spv_RMS },			    SPV_RMS },
	{ { 0, rt_task_wakeup_sleeping },	    WAKEUP_SLEEPING },
	{ { 1, rt_change_prio },		    CHANGE_TASK_PRIO },
	{ { 0, rt_set_resume_time },  		    SET_RESUME_TIME },
	{ { 0, rt_set_period },			    SET_PERIOD },
	{ { 0, rt_is_hard_timer_running },	    HARD_TIMER_RUNNING },
	{ { 0, rt_get_adr },			    GET_ADR },
	{ { 0, rt_get_name },			    GET_NAME },
	{ { 0, 0 },			            000 }
};

static int lxrt_init(void)

{
    int lxrt_init_archdep(void);
    int cpuid, err;

    if (LxrtMode) LxrtMode = 2;
    err = lxrt_init_archdep();

    if (err)
	return err;
	
    /* 2865600023UL is nam2num("USPAPP") */
    if ((wake_up_srq.srq = rt_request_srq(2865600023UL, wake_up_srq_handler, 0)) < 0)
	{
	printk("LXRT: no wake_up_srq available.\n");
	return wake_up_srq.srq;
	}

    /* We will start stealing Linux tasks as soon as the reservoir is
       instantiated, so create the migration service now. */

    lxrt_migration_virq = adeos_alloc_irq();

    adeos_virtualize_irq_from(&rtai_domain,
			      lxrt_migration_virq,
			      lxrt_migration_handler,
			      NULL,
			      IPIPE_HANDLE_MASK);
    if (Reservoir <= 0)
	Reservoir = 1;

    Reservoir = (Reservoir + NR_RT_CPUS - 1)/NR_RT_CPUS;

    for (cpuid = 0; cpuid < num_online_cpus(); cpuid++)
	{
	taskav[cpuid] = (void *)kmalloc(Reservoir*sizeof(void *), GFP_KERNEL);
	init_MUTEX_LOCKED(&resem[cpuid]);
	kernel_thread((void *)kthread_b, (void *)cpuid, 0);
	kernel_thread((void *)kthread_m, (void *)cpuid, 0);
	down(&resem[cpuid]);
	down(&resem[cpuid]);
	klistm[cpuid].in = (2*Reservoir) & (MAX_WAKEUP_SRQ - 1);
	wake_up_process(kthreadm[cpuid]);
	}

    for (cpuid = 0; cpuid < MAX_LXRT_FUN; cpuid++)
	{
	rt_fun_lxrt[cpuid].type = 1;
	rt_fun_lxrt[cpuid].fun  = nihil;
	}
	
    set_rt_fun_entries(rt_sched_entries);

    lxrt_old_trap_handler = rt_set_rtai_trap_handler(lxrt_handle_trap);

#ifdef CONFIG_PROC_FS
    rtai_proc_lxrt_register();
#endif

    /* Must be called on behalf of the Linux domain. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
    adeos_catch_event(ADEOS_SCHEDULE_HEAD,&lxrt_intercept_schedule_head);
#endif  /* KERNEL_VERSION < 2.6.0 */
    adeos_catch_event(ADEOS_SCHEDULE_TAIL,&lxrt_intercept_schedule_tail);
    adeos_catch_event(ADEOS_SIGNAL_PROCESS,&lxrt_intercept_signal);
    adeos_catch_event_from(&rtai_domain,ADEOS_SYSCALL_PROLOGUE,&lxrt_intercept_syscall);

    return 0;
}

static void lxrt_exit(void)

{
    void lxrt_exit_archdep(void);
    unsigned long flags;
    int cpuid;

#ifdef CONFIG_PROC_FS
    rtai_proc_lxrt_unregister();
#endif

    for (cpuid = 0; cpuid < num_online_cpus(); cpuid++)
	{
	struct klist_t *klistp;
	struct task_struct *kthread;
	klistp = &klistm[cpuid];

	while ((kthread = __get_kthread(cpuid)))
	    {
	    klistp->task[klistp->in] = kthread;
	    klistp->in = (klistp->in + 1) & (MAX_WAKEUP_SRQ - 1);
	    }
	
	wake_up_process(kthreadm[cpuid]);
	
	while (kthreadm[cpuid]->state != TASK_UNINTERRUPTIBLE)
	    {
	    current->state = TASK_INTERRUPTIBLE;
	    schedule_timeout(2);
	    }
	}

    endkthread = 1;

    for (cpuid = 0; cpuid < num_online_cpus(); cpuid++)
	{
	wake_up_process(kthreadb[cpuid]);
	wake_up_process(kthreadm[cpuid]);

	while (kthreadb[cpuid] || kthreadm[cpuid])
	    {
	    current->state = TASK_INTERRUPTIBLE;
	    schedule_timeout(2);
	    }

	kfree(taskav[cpuid]);
	}

    rt_set_rtai_trap_handler(lxrt_old_trap_handler);

    if (rt_free_srq(wake_up_srq.srq) < 0)
	printk("LXRT: wake_up_srq %d illegal or already free.\n", wake_up_srq.srq);

    if (lxrt_migration_virq)
	{
	adeos_virtualize_irq_from(&rtai_domain,lxrt_migration_virq,NULL,NULL,0);
	adeos_free_irq(lxrt_migration_virq);
	}


    /* Must be called on behalf of the Linux domain. */
    adeos_catch_event(ADEOS_SIGNAL_PROCESS,NULL);
    adeos_catch_event(ADEOS_SCHEDULE_HEAD,NULL);
    adeos_catch_event(ADEOS_SCHEDULE_TAIL,NULL);
    adeos_catch_event_from(&rtai_domain,ADEOS_SYSCALL_PROLOGUE,NULL);
    
    flags = rtai_critical_enter(NULL);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
    {
    struct mmreq *p;

    /* Flush the MM log for all processors */
    for (p = lxrt_mmrqtab; p < lxrt_mmrqtab + NR_CPUS; p++)
	{
	while (p->out != p->in)
	    {
	    struct mm_struct *oldmm = p->mm[p->out];
	    mmdrop(oldmm);
	    bump_mmreq(p->out);
	    p->count--;
	    }
	}
    }
#endif  /* KERNEL_VERSION < 2.6.0 */

    rtai_critical_exit(flags);

    lxrt_exit_archdep();

    reset_rt_fun_entries(rt_sched_entries);
}

static int __rtai_lxrt_init(void)
{
	int cpuid, retval;
	sched_mem_init();

	for (cpuid = 0; cpuid < NR_RT_CPUS; cpuid++) {
		rt_linux_task.uses_fpu = 1;
		rt_linux_task.magic = 0;
		rt_linux_task.policy = rt_linux_task.is_hard = 0;
		rt_linux_task.runnable_on_cpus = cpuid;
		rt_linux_task.state = RT_SCHED_READY;
		rt_linux_task.msg_queue.prev = &(rt_linux_task.msg_queue);      
		rt_linux_task.msg_queue.next = &(rt_linux_task.msg_queue);      
		rt_linux_task.msg_queue.task = &rt_linux_task;    
		rt_linux_task.msg = 0;  
		rt_linux_task.ret_queue.prev = &(rt_linux_task.ret_queue);
		rt_linux_task.ret_queue.next = &(rt_linux_task.ret_queue);
		rt_linux_task.ret_queue.task = NOTHING;
		rt_linux_task.priority = RT_SCHED_LINUX_PRIORITY;
		rt_linux_task.base_priority = RT_SCHED_LINUX_PRIORITY;
		rt_linux_task.signal = 0;
		rt_linux_task.prev = &rt_linux_task;
                rt_linux_task.resume_time = RT_TIME_END;
                rt_linux_task.tprev = rt_linux_task.tnext =
                rt_linux_task.rprev = rt_linux_task.rnext = &rt_linux_task;
		rt_linux_task.next = 0;
		rt_linux_task.lnxtsk = current;
		rt_smp_current[cpuid] = &rt_linux_task;
		rt_smp_fpu_task[cpuid] = &rt_linux_task;
		oneshot_timer = OneShot ? 1 : 0;
		oneshot_running = 0;
	}
	tuned.latency = imuldiv(Latency, tuned.cpu_freq, 1000000000);
	tuned.setup_time_TIMER_CPUNIT = imuldiv( SetupTimeTIMER, 
						 tuned.cpu_freq, 
						 1000000000);
	tuned.setup_time_TIMER_UNIT   = imuldiv( SetupTimeTIMER, 
						 TIMER_FREQ, 
						 1000000000);
	tuned.timers_tol[0] = 0;
#ifdef CONFIG_PROC_FS
	if (rtai_proc_sched_register()) {
		retval = 1;
		goto mem_end;
	}
#endif

	rt_mount_rtai();
// 0x7dd763ad == nam2num("MEMSRQ").
	if ((frstk_srq.srq = rt_request_srq(0x7dd763ad, frstk_srq_handler, 0)) < 0) {
		printk("MEM SRQ: no sysrq available.\n");
		retval = frstk_srq.srq;
		goto umount_rtai;
	}

	frstk_srq.in = frstk_srq.out = 0;
	if ((retval = rt_request_sched_ipi()) != 0)
		goto free_srq;

	if ((retval = lxrt_init()) != 0)
		goto free_sched_ipi;

#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
	rt_set_ihook(&rtai_handle_isched_lock);
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */

	register_reboot_notifier(&lxrt_notifier_reboot);

	printk(KERN_INFO "RTAI[sched_lxrt]: loaded (LxrtMode %d).\n", LxrtMode);
	printk(KERN_INFO "RTAI[sched_lxrt]: timer=%s (%s).\n",
	       oneshot_timer ? "oneshot" : "periodic",
#ifdef __USE_APIC__
	       "APIC"
#else /* __USE_APIC__ */
	       "8254-PIT"
#endif /* __USE_APIC__ */
	       );
	printk(KERN_INFO "RTAI[sched_lxrt]: standard tick=%d hz, CPU freq=%lu hz.\n",
	       HZ,
	       (unsigned long)tuned.cpu_freq);
	printk(KERN_INFO "RTAI[sched_lxrt]: timer setup=%d ns, resched latency=%d ns.\n",
	       imuldiv(tuned.setup_time_TIMER_CPUNIT, 1000000000, tuned.cpu_freq),
	       imuldiv(tuned.latency - tuned.setup_time_TIMER_CPUNIT, 1000000000, tuned.cpu_freq));

	retval = rtai_init_features(); /* see rtai_schedcore.h */
exit:
	return retval;
free_sched_ipi:
	rt_free_sched_ipi();
free_srq:
	rt_free_srq(frstk_srq.srq);
umount_rtai:
	rt_umount_rtai();
#ifdef CONFIG_PROC_FS
	rtai_proc_sched_unregister();
#endif
mem_end:
	sched_mem_end();
	goto exit;
}

static void __rtai_lxrt_exit(void)
{
	unregister_reboot_notifier(&lxrt_notifier_reboot);

	lxrt_killall();

	krtai_objects_release();

	lxrt_exit();

	rtai_cleanup_features();

#ifdef CONFIG_PROC_FS
        rtai_proc_sched_unregister();
#endif
	while (frstk_srq.out != frstk_srq.in);
	if (rt_free_srq(frstk_srq.srq) < 0) {
		printk("MEM SRQ: frstk_srq %d illegal or already free.\n", frstk_srq.srq);
	}
	rt_free_sched_ipi();
	sched_mem_end();
#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
	rt_set_ihook(NULL);
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */
	rt_umount_rtai();
	printk(KERN_INFO "RTAI[sched_lxrt]: unloaded.\n");
}

module_init(__rtai_lxrt_init);
module_exit(__rtai_lxrt_exit);

#ifdef CONFIG_KBUILD

EXPORT_SYMBOL(rt_fun_lxrt);
EXPORT_SYMBOL(clr_rtext);
EXPORT_SYMBOL(set_rtext);
EXPORT_SYMBOL(get_min_tasks_cpuid);
EXPORT_SYMBOL(rt_schedule_soft);
EXPORT_SYMBOL(LxrtMode);
EXPORT_SYMBOL(rt_do_force_soft);

#endif /* CONFIG_KBUILD */
