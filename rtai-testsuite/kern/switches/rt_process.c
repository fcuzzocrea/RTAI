/*
COPYRIGHT (C) 2000  Paolo Mantegazza (mantegazza@aero.polimi.it)

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


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <rtai_sem.h>

MODULE_DESCRIPTION("Measures task switching times");
MODULE_AUTHOR("Paolo Mantegazza <mantegazza@aero.polimi.it>");
MODULE_LICENSE("GPL");

/*
 * Command line parameters
 */
int ntasks = 30;
MODULE_PARM(ntasks, "i");
MODULE_PARM_DESC(ntasks, "Number of tasks to switch (default: 30)");

int loops = 2000;
MODULE_PARM(loops, "i");
MODULE_PARM_DESC(loops, "Number of switches per task (default: 2000)");

#ifdef CONFIG_RTAI_FPU_SUPPORT
int use_fpu = 1;
MODULE_PARM(use_fpu, "i");
MODULE_PARM_DESC(use_fpu, "Use full FPU support (default: 1)");
#else
int use_fpu = 0;
#endif

int stack_size = 2000;
MODULE_PARM(stack_size, "i");
MODULE_PARM_DESC(stack_size, "Task stack size in bytes (default: 2000)");

EXPORT_NO_SYMBOLS;

//#define DISTRIBUTE

#define SEM_TYPE (CNT_SEM | FIFO_Q)

static RT_TASK *thread, task;

static int cpu_used[NR_RT_CPUS];

static SEM sem;

static volatile int change;

static void rt_task(int t) {
	while(1) {
		if (change) {
			rt_sem_wait(&sem);
		} else {
			rt_task_suspend(thread + t);
		}
		cpu_used[hard_cpu_id()]++;
	}
}

static void sched_task(int t) {
	int i, k;
	change = 0;
	t = rdtsc();
	for (i = 0; i < loops; i++) {
		for (k = 0; k < ntasks; k++) {
			rt_task_resume(thread + k);
		}
	}
	t = rdtsc() - t;
	rt_printk("\n\nFOR %d TASKS: ", ntasks);
	rt_printk("TIME %d (ms), SUSP/RES SWITCHES %d, ", (int)llimd(t, 1000, CPU_FREQ), 2*ntasks*loops);
	rt_printk("SWITCH TIME%s %d (ns)\n", use_fpu ? " (INCLUDING FULL FP SUPPORT)":"",
	       (int)llimd(llimd(t, 1000000000, CPU_FREQ), 1, 2*ntasks*loops));

	change = 1;
	for (k = 0; k < ntasks; k++) {
		rt_task_resume(thread + k);
	}
	t = rdtsc();
	for (i = 0; i < loops; i++) {
		for (k = 0; k < ntasks; k++) {
			rt_sem_signal(&sem);
		}
	}
	t = rdtsc() - t;
	rt_printk("\nFOR %d TASKS: ", ntasks);
	rt_printk("TIME %d (ms), SEM SIG/WAIT SWITCHES %d, ", (int)llimd(t, 1000, CPU_FREQ), 2*ntasks*loops);
	rt_printk("SWITCH TIME%s %d (ns)\n\n", use_fpu ? " (INCLUDING FULL FP SUPPORT)":"",
	       (int)llimd(llimd(t, 1000000000, CPU_FREQ), 1, 2*ntasks*loops));
}

int init_module(void)
{
	int i;

	printk("\nWait for it ...\n");
	rt_typed_sem_init(&sem, 1, SEM_TYPE);
	rt_linux_use_fpu(1);
        thread = (RT_TASK *)kmalloc(ntasks*sizeof(RT_TASK), GFP_KERNEL);
	for (i = 0; i < ntasks; i++) {
#ifdef DISTRIBUTE
		rt_task_init_cpuid(thread + i, rt_task, i, stack_size, 0, use_fpu, 0,  i%2);
#else
		rt_task_init_cpuid(thread + i, rt_task, i, stack_size, 0, use_fpu, 0,  hard_cpu_id());
#endif
	}
	rt_task_init_cpuid(&task, sched_task, i, stack_size, 1, 0, 0, hard_cpu_id());
	rt_task_resume(&task);

	return 0;
}


void cleanup_module(void)
{
	int i;
	for (i = 0; i < ntasks; i++) {
		rt_task_delete(thread + i);
	}
	rt_task_delete(&task);
        kfree(thread);
	printk("\nCPU USE SUMMARY\n");
	for (i = 0; i < NR_RT_CPUS; i++) {
		printk("# %d -> %d\n", i, cpu_used[i]);
	}
	printk("END OF CPU USE SUMMARY\n\n");
	rt_sem_delete(&sem);
}
