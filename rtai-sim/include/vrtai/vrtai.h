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
 */

#ifndef _nslib_include_vrtai_h
#define _nslib_include_vrtai_h

#include <errno.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include "vrtai/rtai_types.h"

#define VRTAI_VERSION_STRING  "24.1.8-sr1"

#define printk printf
#define rt_printk printf

#define imuldiv(n,mul,div)      (int)((n)*(mul)/(div))
#define llimd(n,mul,div)        (((long long)n)*((long long)(mul))/((long long)div))
#define ulldiv(ull,uld,rem)     (((*rem) = ((ull) % (uld))), (ull) / (uld))

#define MODULE_DESCRIPTION(s);
#define MODULE_LICENSE(s);
#define MODULE_AUTHOR(s);
#define MODULE_PARM(var,type);
#define MODULE_PARM_DESC(var,desc);
#define MODULE_PARM_VALUE(var);

#define __mvm_breakable(f) f ## $kdoor$

#define DEFINE_LINUX_CR0
#define DECLR_8254_TSC_EMULATION
#define SETUP_8254_TSC_EMULATION
#define CLEAR_8254_TSC_EMULATION
#define rt_umount_rtai()
#define rt_switch_to_real_time(mode)
#define rt_switch_to_linux(mode)
#define save_cr0_and_clts(linux_cr0)
#define restore_cr0(linux_cr0)
#define enable_fpu()
#define init_fp_env()
#define save_fpenv(env)
#define	restore_fpenv(env)
#define RT_SET_RTAI_TRAP_HANDLER(x)
#define rt_set_timer_delay(x)

#define HZ                 100
#define FREQ_8254          1193180
#define LATENCY_8254       4700
#define SETUP_TIME_8254	   3000 
#define NR_RT_CPUS         1
#define TIMER_8254_IRQ     1
#define RTAI_NR_TRAPS      32
#define RT_TIME_END        0x7FFFFFFFFFFFFFFFLL
#define RT_LINUX_PRIORITY  0x7fffFfff
#define LATCH              ((FREQ_8254 + HZ/2) / HZ)
#define CPU_FREQ           (tuned.cpu_freq)

#define rt_mount_rtai() \
mvm_finalize_init(&rt_linux_task)

#define init_arch_stack() \
mvm_init_task_stack(task,rt_startup,rt_thread,data)

#define rt_switch_to(task) \
do { \
rt_current = task; \
__mvm_breakable(mvm_switch_to)(task); \
} while(0)

#define get_stack_pointer() \
(char *)rt_current->stack_bottom

#define hard_save_flags_and_cli(flags) \
((flags) = mvm_set_irqmask(-1))

#define hard_restore_flags(flags) \
mvm_set_irqmask(flags)

#define hard_cli() \
mvm_set_irqmask(-1)

#define hard_sti() \
mvm_set_irqmask(0)

#define rt_request_srq(label,handler,u_handler) \
mvm_request_srq(label,handler)

#define rt_free_srq(srq) \
mvm_free_srq(srq)

#define rt_pend_linux_srq(srq) \
mvm_post_srq(srq)

#define rt_request_linux_irq(irq,handler,id,dev) \
mvm_hook_irq(irq,(void (*)(void *))handler,(void *)irq)

#define rt_free_linux_irq(irq,dev) \
mvm_release_irq(irq)

#define rt_pend_linux_irq(irq) \
mvm_post_irq(irq)

#define rt_request_timer(handler,period,apic) \
mvm_request_timer(period,handler)

#define rt_free_timer() \
mvm_free_timer()

#define rdtsc() \
llimd(mvm_get_cpu_time(),tuned.cpu_freq,1000000000)

struct XenoThread;
struct rt_task_struct;
struct mvm_displayctx;

typedef int FPU_ENV;

typedef int spinlock_t;

#define spin_lock_init(slock) (*slock = 0)

#define rt_spin_lock_irqsave(slockp) \
hard_save_flags_and_cli(*slockp)

#define rt_spin_unlock_irqrestore(flags,slockp) \
hard_restore_flags(flags)

typedef struct vtasktcb {

    void (*task_body)(int);
    void (*trampoline)(void(*task_body)(int), int data);
    int data;
    struct rt_task_struct *task;
    struct XenoThread *vmthread;
    
} vtasktcb_t;

struct apic_timer_setup_data {

    int mode,
	count;
};

struct calibration_data {
	unsigned int cpu_freq;
	unsigned int apic_freq;
	int latency;
	int setup_time_TIMER_CPUNIT;
	int setup_time_TIMER_UNIT;
	int timers_tol[NR_RT_CPUS];
};

#define vmalloc(size)       malloc(size)
#define vfree(addr)         free(addr)
#define kmalloc(size,prio)  malloc(size)
#define kfree(addr)         free(addr)

extern struct rt_times rt_times;
extern struct calibration_data tuned;
extern unsigned long jiffies;

typedef struct mvm_displayctl {

    void (*objctl)(struct mvm_displayctx *ctx, int op, const char *arg);
    const char *prefix;		/* Tcl prefix for iface procs */
    const char *group;		/* Plotting group of state diagram */
    const char *const *sarray;	/* States displayed in state diagram */

} mvm_displayctl_t;

#define MVM_DECL_DISPLAY_CONTROL(tag,objctl,group,slist...) \
void objctl(struct mvm_displayctx *ctx, int op, const char *arg); \
static const char *__mvm_sarray ## tag [] = { slist, NULL }; \
 mvm_displayctl_t __mvm_displayctl_ ## tag = { \
 objctl, \
 #tag, \
 (group), \
 __mvm_sarray ## tag \
}

struct MvmDashboard;
struct MvmGraph;

typedef struct mvm_displayctx {

    struct MvmDashboard *dashboard; /* A control board */
    struct MvmGraph *graph;	/* A state diagram */
    mvm_displayctl_t *control;	/* The associated control block */
    void *obj;			/* The rt-iface object */

} mvm_displayctx_t;

#ifdef INTERFACE_TO_LINUX

/* Rename rtai_sched's entry/exit points so we can have both the
   application's and the scheduler's init/cleanup routines in a single
   userland executable without conflict. */

void mvm_init(int argc,
	      char *argv[]);

int mvm_run(void *tcbarg,
	    void *faddr);

void __mvm_breakable(mvm_join_threads)(void);

void __mvm_breakable(mvm_terminate)(int xcode);

int vrtai_init_module(void);

void vrtai_cleanup_module(void);

int init_module(void);

void cleanup_module(void);

struct rt_times rt_times;

struct calibration_data tuned;

void mvm_root (int data)

{
    int err;

    tuned.cpu_freq = FREQ_8254;
    tuned.apic_freq = 0;

    /* Call rtai_sched's init_module() */
    err = vrtai_init_module();

    if (!err)
	{
	/* Call application's init_module() */
	err = init_module();

	if (!err)
	    {
	    /* Wait for all RTAI tasks to finish */
	    __mvm_breakable(mvm_join_threads)();
	    /* Call application's cleanup_module() */
	    cleanup_module();
	    }

	/* Call rtai_sched's cleanup_module() */
	vrtai_cleanup_module();
	}

    /* No more RTAI tasks alive. The simulation is over. */
    __mvm_breakable(mvm_terminate)(err);
}

#define init_module    vrtai_init_module
#define cleanup_module vrtai_cleanup_module

static void rt_startup(void(*rt_thread)(int),
		       int data);

int main (int argc, char *argv[])

{
    vtasktcb_t tcb;

    tcb.task_body = &mvm_root;
    tcb.trampoline = &rt_startup;
    tcb.data = 0;
    tcb.task = NULL;
    tcb.vmthread = NULL;

    mvm_init(argc,argv);

    return mvm_run(&tcb,(void *)&mvm_root);
}

#endif /* INTERFACE_TO_LINUX */

#ifdef __cplusplus
extern "C" {
#endif

struct XenoThread *mvm_spawn_thread(void *tcbarg,
				    void *faddr,
				    const char *name);

struct XenoThread *mvm_thread_self(void);

void __mvm_breakable(mvm_switch_threads)(struct XenoThread *out,
					 struct XenoThread *in);

void mvm_finalize_switch_threads(struct XenoThread *dead,
				 struct XenoThread *in);

void __mvm_breakable(mvm_break)(void);

void mvm_finalize_thread(struct XenoThread *dead);

void mvm_start_timer(unsigned long nstick,
		     void (*tickhandler)(void));

void mvm_stop_timer(void);

int mvm_set_irqmask(int level);

int mvm_hook_irq(unsigned irq,
		 void (*handler)(void *cookie),
		 void *cookie);

int mvm_release_irq(unsigned irq);

int mvm_post_irq(unsigned irq);

unsigned long long mvm_get_cpu_time(void);

void __mvm_breakable(mvm_switch_to)(struct rt_task_struct *task);

void mvm_init_task_stack(struct rt_task_struct *task,
			 void (*user_trampoline)(void(*task_body)(int),int data),
			 void (*task_body)(int),
			 int data);

int mvm_request_srq(unsigned label,
		    void (*handler)(void));

int mvm_free_srq(unsigned srq);

void mvm_post_srq(unsigned srq);

void mvm_finalize_task(struct rt_task_struct *task);

void mvm_request_timer(int ticks,
		       void (*tickhandler)(void));

void mvm_free_timer(void);

void mvm_finalize_init(struct rt_task_struct *linux_task);

#ifdef __cplusplus
}
#endif

#endif /* !_nslib_include_vrtai_h */
