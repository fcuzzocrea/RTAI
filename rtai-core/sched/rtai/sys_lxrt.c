/*
 * Copyright (C) 2002  Paolo Mantegazza <mantegazza@aero.polimi.it>,
 *		         Pierre Cloutier <pcloutier@poseidoncontrols.com>,
 *		         Steve Papacharalambous <stevep@zentropix.com>.
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
Nov. 2001, Jan Kiszka (Jan.Kiszka@web.de) fix a tiny bug in __task_init.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/unistd.h>
#include <linux/mman.h>
#include <asm/uaccess.h>
#include <rtai_sched.h>
#include <rtai_lxrt.h>
#include <rtai_sem.h>
#include <rtai_mbx.h>
#include <rtai_rwl.h>
#include <rtai_spl.h>

#include <rtai_registry.h>
#include <rtai_proxies.h>
#include <rtai_msg.h>
#include <rtai_schedcore.h>

#define MAX_FUN_EXT  16
static struct rt_fun_entry *rt_fun_ext[MAX_FUN_EXT];

static struct desc_struct sidt;

/*
 * WATCH OUT for the max expected size of messages and arguments of rtai funs;
 */

#define MSG_SIZE    256  // Default max message size.
#define MAX_ARGS    10   // Max number of 4 bytes args in rtai functions.

#ifdef CONFIG_RTAI_TRACE
/****************************************************************************/
/* Trace functions. These functions have to be used rather than insert
the macros as-is. Otherwise the system crashes ... You've been warned. K.Y. */
void trace_true_lxrt_rtai_syscall_entry(void);
void trace_true_lxrt_rtai_syscall_exit(void);
/****************************************************************************/
#endif /* CONFIG_RTAI_TRACE */

DEFINE_LXRT_HANDLER();

int get_min_tasks_cpuid(void);

int set_rtext(RT_TASK *task,
	      int priority,
	      int uses_fpu,
	      void(*signal)(void),
	      unsigned int cpuid,
	      struct task_struct *relink);

int clr_rtext(RT_TASK *task);

void steal_from_linux(RT_TASK *task);

void give_back_to_linux(RT_TASK *task);

void rt_schedule_soft(RT_TASK *task);

void *rt_get_lxrt_fun_entry(int index);

static inline void lxrt_typed_sem_init(SEM *sem, int count, int type)
{
	((int (*)(SEM *, int, int))rt_get_lxrt_fun_entry(TYPED_SEM_INIT))(sem, count, type);
}

static inline int lxrt_typed_mbx_init(MBX *mbx, int bufsize, int type)
{
	return ((int (*)(MBX *, int, int))rt_get_lxrt_fun_entry(TYPED_MBX_INIT))(mbx, bufsize, type);
}

static inline int lxrt_rwl_init(RWL *rwl)
{
	return ((int (*)(RWL *))rt_get_lxrt_fun_entry(RWL_INIT))(rwl);
}

static inline int lxrt_spl_init(SPL *spl)
{
	return ((int (*)(SPL *))rt_get_lxrt_fun_entry(SPL_INIT))(spl);
}

static inline int lxrt_Proxy_detach(pid_t pid)
{
	return ((int (*)(int))rt_get_lxrt_fun_entry(PROXY_DETACH))(pid);
}

static inline int GENERIC_DELETE(int index, void *object)
{
	return ((int (*)(void *))rt_get_lxrt_fun_entry(index))(object);
}
			 
#define lxrt_sem_delete(sem)        GENERIC_DELETE(SEM_DELETE, sem)
#define lxrt_named_sem_delete(sem)  GENERIC_DELETE(NAMED_SEM_DELETE, sem)
#define lxrt_rwl_delete(rwl)        GENERIC_DELETE(RWL_DELETE, rwl)
#define lxrt_named_rwl_delete(rwl)  GENERIC_DELETE(NAMED_RWL_DELETE, rwl)
#define lxrt_spl_delete(spl)        GENERIC_DELETE(SPL_DELETE, spl)
#define lxrt_named_spl_delete(spl)  GENERIC_DELETE(NAMED_SPL_DELETE, spl)
#define lxrt_mbx_delete(mbx)        GENERIC_DELETE(MBX_DELETE, mbx)
#define lxrt_named_mbx_delete(mbx)  GENERIC_DELETE(NAMED_MBX_DELETE, mbx)

struct fun_args { int a0; int a1; int a2; int a3; int a4; int a5; int a6; int a7; int a8; int a9; long long (*fun)(int, ...); };

static inline long long lxrt_resume(void *fun, int narg, int *arg, unsigned long long type, RT_TASK *rt_task, int net_rpc)
{
	int wsize, w2size;
	int *wmsg_adr, *w2msg_adr;
	struct fun_args *funarg;

	memcpy(funarg = (void *)rt_task->fun_args, arg, narg*sizeof(int));
	funarg->fun = fun;
	if (net_rpc) {
		memcpy((void *)(rt_task->fun_args[4] = (int)(funarg + 1)), (void *)arg[4], arg[5]);
	}
/*
 * Here type > 0 means any messaging with the need of copying from/to user
 * space. My knowledge of Linux memory menagment has led to this mess.
 * Whoever can do it better is warmly welcomed.
 */
	wsize = w2size = 0 ;
	wmsg_adr = w2msg_adr = 0;
	if (NEED_TO_RW(type)) {
		int msg_size, rsize, r2size;
		int *fun_args;
		
		fun_args = (net_rpc ? (int *)rt_task->fun_args[4] : rt_task->fun_args) - 1;
		rsize = r2size = 0;
		if( NEED_TO_R(type)) {			
			rsize = USP_RSZ1(type);
			rsize = rsize ? *(fun_args + rsize) : (USP_RSZ1LL(type) ? sizeof(long long) : sizeof(int));
		}
		if (NEED_TO_W(type)) {
			wsize = USP_WSZ1(type);
			wsize = wsize ? *(fun_args + wsize) : (USP_WSZ1LL(type) ? sizeof(long long) : sizeof(int));
		}
		if ((msg_size = rsize > wsize ? rsize : wsize) > 0) {
			if (msg_size > rt_task->max_msg_size[0]) {
				rt_free(rt_task->msg_buf[0]);
				rt_task->max_msg_size[0] = (msg_size*120 + 50)/100;
				rt_task->msg_buf[0] = rt_malloc(rt_task->max_msg_size[0]);
			}
			if (rsize > 0) {			
				int *buf_arg, *rmsg_adr;
				buf_arg = fun_args + USP_RBF1(type);
				rmsg_adr = (int *)(*buf_arg);
				*(buf_arg) = (int)rt_task->msg_buf[0];
				if (rmsg_adr) {
					copy_from_user(rt_task->msg_buf[0], rmsg_adr, rsize);
				}
			}
			if (wsize > 0) {
				int *buf_arg;
				buf_arg = fun_args + USP_WBF1(type);
				wmsg_adr = (int *)(*buf_arg);
				*(buf_arg) = (int)rt_task->msg_buf[0];
			}
		}
/*
 * 2nd buffer next.
 */
		if (NEED_TO_R2ND(type)) {
			r2size = USP_RSZ2(type);
			r2size = r2size ? *(fun_args + r2size) : (USP_RSZ2LL(type) ? sizeof(long long) : sizeof(int));
		}
		if (NEED_TO_W2ND(type)) {
			w2size = USP_WSZ2(type);
			w2size = w2size ? *(fun_args + w2size) : (USP_WSZ2LL(type) ? sizeof(long long) : sizeof(int));
		}
		if ((msg_size = r2size > w2size ? r2size : w2size) > 0) {
			if (msg_size > rt_task->max_msg_size[1]) {
				rt_free(rt_task->msg_buf[1]);
				rt_task->max_msg_size[1] = (msg_size*120 + 50)/100;
				rt_task->msg_buf[1] = rt_malloc(rt_task->max_msg_size[1]);
			}
			if (r2size > 0) {
				int *buf_arg, *r2msg_adr;
				buf_arg = fun_args + USP_RBF2(type);
				r2msg_adr = (int *)(*buf_arg);
				*(buf_arg) = (int)rt_task->msg_buf[1];
				if (r2msg_adr) {
					copy_from_user(rt_task->msg_buf[1], r2msg_adr, r2size);
       				}
       			}
			if (w2size > 0) {
				int *buf_arg;
				buf_arg = fun_args + USP_WBF2(type);
				w2msg_adr = (int *)(*buf_arg);
       		        	*(buf_arg) = (int)rt_task->msg_buf[1];
       			}
		}
	}
/*
 * End of messaging mess.
 */
	if ((int)rt_task->is_hard > 0) {
		rt_task->retval = ((long long (*)(int, ...))fun)(funarg->a0, funarg->a1, funarg->a2, funarg->a3, funarg->a4, funarg->a5, funarg->a6, funarg->a7, funarg->a8, funarg->a9);
	} else {
		rt_schedule_soft(rt_task);
	}
/*
 * A trashing of the comment about messaging mess at the beginning.
 */
	if (wsize > 0 && wmsg_adr) {
		copy_to_user(wmsg_adr, rt_task->msg_buf[0], wsize);
	}
	if (w2size > 0 && w2msg_adr) {
		copy_to_user(w2msg_adr, rt_task->msg_buf[1], w2size);
	}
	return rt_task->retval;
}

static inline RT_TASK* __task_init(unsigned long name, int prio, int stack_size, int max_msg_size, int cpus_allowed)
{
	void *msg_buf;
	RT_TASK *rt_task;

	if (rt_get_adr(name)) {
		return 0;
	}
	if (prio > RT_SCHED_LOWEST_PRIORITY) {
		prio = RT_SCHED_LOWEST_PRIORITY;
	}
	if (!max_msg_size) {
		max_msg_size = MSG_SIZE;
	}
	if (!(msg_buf = rt_malloc(2*max_msg_size))) {
		return 0;
	}
	rt_task = rt_malloc(sizeof(RT_TASK) + 3*sizeof(struct fun_args)); 
	if (rt_task) {
	    rt_task->magic = 0;
	    if (num_online_cpus() > 1 && cpus_allowed) {
	    cpus_allowed = hweight32(cpus_allowed) > 1 ? get_min_tasks_cpuid() : ffnz(cpus_allowed);
	    } else {
	    cpus_allowed = 0;
	    }
	    if (!set_rtext(rt_task, prio, 0, 0, cpus_allowed, 0)) {
	        rt_task->fun_args = (int *)((struct fun_args *)(rt_task + 1));
		rt_task->msg_buf[0] = msg_buf;
		rt_task->msg_buf[1] = msg_buf + max_msg_size;
		rt_task->max_msg_size[0] =
		rt_task->max_msg_size[1] = max_msg_size;
		if (rt_register(name, rt_task, IS_TASK, 0)) {
			return rt_task;
		} else {
			clr_rtext(rt_task);
		}
	    }
	    rt_free(rt_task);
	}
	rt_free(msg_buf);
	return 0;
}

static int __task_delete(RT_TASK *rt_task)
{
	struct task_struct *process;
	if (current == rt_task->lnxtsk && rt_task->is_hard == 1) {
		give_back_to_linux(rt_task);
	}
	if (clr_rtext(rt_task)) {
		return -EFAULT;
	}
	rt_free(rt_task->msg_buf[0]);
	rt_free(rt_task);
	if ((process = rt_task->lnxtsk)) {
		process->this_rt_task[0] = process->this_rt_task[1] = 0;
	}
	return (!rt_drg_on_adr(rt_task)) ? -ENODEV : 0;
}

//#define ECHO_SYSW
#ifdef ECHO_SYSW
#define SYSW_DIAG_MSG(x) x
#else
#define SYSW_DIAG_MSG(x)
#endif

#if 1
static inline void __force_soft(RT_TASK *task)
{
	if (task && task->force_soft) {
		task->force_soft = 0;
		task->usp_flags &= ~FORCE_SOFT;
		give_back_to_linux(task);
	}
}
#else 
static inline void __force_soft(RT_TASK *task) { }
#endif

static inline long long handle_lxrt_request (unsigned int lxsrq, void *arg)

{
#define larg ((struct arg *)arg)
#define ar   ((unsigned long *)arg)
#define MANYARGS ar[0],ar[1],ar[2],ar[3],ar[4],ar[5],ar[6],ar[7],ar[8],ar[9]

    union {unsigned long name; RT_TASK *rt_task; SEM *sem; MBX *mbx; RWL *rwl; SPL *spl; } arg0;
	int srq;
	RT_TASK *task;

	__force_soft(task = current->this_rt_task[0]);
	srq = SRQ(lxsrq);
	if (srq < MAX_LXRT_FUN) {
		int idx;
		unsigned long long type;
		struct rt_fun_entry *funcm;
/*
 * The next two lines of code do a lot. It makes possible to extend the use of
 * USP to any other real time module service in user space, both for soft and
 * hard real time. Concept contributed and copyrighted by: Giuseppe Renoldi 
 * (giuseppe@renoldi.org).
 */
		idx   = INDX(lxsrq);
		funcm = rt_fun_ext[idx];

		if (!funcm) {
			rt_printk("BAD: null rt_fun_ext[%d]\n", idx);
			return -ENOSYS;
		}

		if ((type = funcm[srq].type)) {
			int net_rpc;
			if ((int)task->is_hard > 1) {
				SYSW_DIAG_MSG(rt_printk("GOING BACK TO HARD (SYSLXRT), PID = %d.\n", current->pid););
				task->is_hard = 0;
				steal_from_linux(task);
				SYSW_DIAG_MSG(rt_printk("GONE BACK TO HARD (SYSLXRT),  PID = %d.\n", current->pid););
			}
			net_rpc = idx == 2 && !srq;
			lxrt_resume(funcm[srq].fun, NARG(lxsrq), (int *)arg, net_rpc ? ((long long *)((int *)arg + 2))[0] : type, task, net_rpc);
			__force_soft(task);
			return task->retval;
		} else {
			return ((long long (*)(unsigned long, ...))funcm[srq].fun)(MANYARGS);
	        }
	}

	arg0.name = ar[0];
	switch (srq) {
		case LXRT_GET_ADR: {
			return (unsigned long)rt_get_adr(arg0.name);
		}

		case LXRT_GET_NAME: {
			return rt_get_name((void *)arg0.name);
		}

		case LXRT_TASK_INIT: {
			struct arg { int name, prio, stack_size, max_msg_size, cpus_allowed; };
			return (unsigned long) __task_init(arg0.name, larg->prio, larg->stack_size, larg->max_msg_size, larg->cpus_allowed);
		}

		case LXRT_TASK_DELETE: {
			return __task_delete(arg0.rt_task);
		}

		case LXRT_SEM_INIT: {
			if (rt_get_adr(arg0.name)) {
				return 0;
			}
			if ((arg0.sem = rt_malloc(sizeof(SEM)))) {
				struct arg { int name; int cnt; int typ; };
				lxrt_typed_sem_init(arg0.sem, larg->cnt, larg->typ);
				if (rt_register(larg->name, arg0.sem, IS_SEM, current)) {
					return arg0.name;
				} else {
					rt_free(arg0.sem);
				}
			}
			return 0;
		}

		case LXRT_SEM_DELETE: {
			if (lxrt_sem_delete(arg0.sem)) {
				return -EFAULT;
			}
			rt_free(arg0.sem);
			return rt_drg_on_adr(arg0.sem);
		}

		case LXRT_MBX_INIT: {
			if (rt_get_adr(arg0.name)) {
				return 0;
			}
			if ((arg0.mbx = rt_malloc(sizeof(MBX)))) {
				struct arg { int name; int size; int qtype; };
				if (lxrt_typed_mbx_init(arg0.mbx, larg->size, larg->qtype) < 0) {
					rt_free(arg0.mbx);
					return 0;
				}
				if (rt_register(larg->name, arg0.mbx, IS_MBX, current)) {
					return arg0.name;
				} else {
					rt_free(arg0.mbx);
				}
			}
			return 0;
		}

		case LXRT_MBX_DELETE: {
			if (lxrt_mbx_delete(arg0.mbx)) {
				return -EFAULT;
			}
			rt_free(arg0.mbx);
			return rt_drg_on_adr(arg0.mbx);
		}

		case LXRT_RWL_INIT: {
			if (rt_get_adr(arg0.name)) {
				return 0;
			}
			if ((arg0.rwl = rt_malloc(sizeof(RWL)))) {
				struct arg { int name; };
				lxrt_rwl_init(arg0.rwl);
				if (rt_register(larg->name, arg0.rwl, IS_SEM, current)) {
					return arg0.name;
				} else {
					rt_free(arg0.rwl);
				}
			}
			return 0;
		}

		case LXRT_RWL_DELETE: {
			if (lxrt_rwl_delete(arg0.rwl)) {
				return -EFAULT;
			}
			rt_free(arg0.rwl);
			return rt_drg_on_adr(arg0.rwl);
		}

		case LXRT_SPL_INIT: {
			if (rt_get_adr(arg0.name)) {
				return 0;
			}
			if ((arg0.spl = rt_malloc(sizeof(SPL)))) {
				struct arg { int name; };
				lxrt_spl_init(arg0.spl);
				if (rt_register(larg->name, arg0.spl, IS_SEM, current)) {
					return arg0.name;
				} else {
					rt_free(arg0.spl);
				}
			}
			return 0;
		}

		case LXRT_SPL_DELETE: {
			if (lxrt_spl_delete(arg0.spl)) {
				return -EFAULT;
			}
			rt_free(arg0.spl);
			return rt_drg_on_adr(arg0.spl);
		}

		case MAKE_HARD_RT: {
			if (!(task = current->this_rt_task[0]) || (int)task->is_hard == 1) {
				 return 0;
			}
			steal_from_linux(task);
			return 0;
		}

		case MAKE_SOFT_RT: {
			if (!(task = current->this_rt_task[0]) || (int)task->is_hard <= 0) {
				return 0;
			}
			if ((int)task->is_hard > 1) {
				task->is_hard = 0;
			} else {
				give_back_to_linux(task);
			}
			return 0;
		}
		case PRINT_TO_SCREEN: {
			struct arg { char *display; int nch; };
			return rtai_print_to_screen("%s", larg->display);
		}

		case PRINTK: {
			struct arg { char *display; int nch; };
			return rt_printk("%s", larg->display);
		}

		case NONROOT_HRT: {
			current->cap_effective |= ((1 << CAP_IPC_LOCK)  |
						   (1 << CAP_SYS_RAWIO) | 
						   (1 << CAP_SYS_NICE));
			return 0;
		}

		case RT_BUDDY: {
			return current->this_rt_task[0] && 
			       current->this_rt_task[1] == current ?
			       (unsigned long)(current->this_rt_task[0]) : 0;
		}

		case HRT_USE_FPU: {
			struct arg { RT_TASK *task; int use_fpu; };
			if(!larg->use_fpu) {
				((larg->task)->lnxtsk)->used_math = 0;
				set_tsk_used_fpu(((larg->task)->lnxtsk));
			} else {
				init_xfpu();
				((larg->task)->lnxtsk)->used_math = 1;
				set_tsk_used_fpu(((larg->task)->lnxtsk));
			}
			return 0;
		}

                case GET_USP_FLAGS: {
                        return arg0.rt_task->usp_flags;
                }
                case SET_USP_FLAGS: {
                        struct arg { RT_TASK *task; unsigned long flags; };
                        arg0.rt_task->usp_flags = larg->flags;
                        arg0.rt_task->force_soft = ((int)arg0.rt_task->is_hard > 0) && (larg->flags & arg0.rt_task->usp_flags_mask & FORCE_SOFT);
                        return 0;
                }

                case GET_USP_FLG_MSK: {
                        return arg0.rt_task->usp_flags_mask;
                }

                case SET_USP_FLG_MSK: {
                        (task = current->this_rt_task[0])->usp_flags_mask = arg0.name;
                        task->force_soft = ((int)task->is_hard > 0) && (task->usp_flags & arg0.name & FORCE_SOFT);
                        return 0;
                }

                case FORCE_TASK_SOFT: {
			extern void rt_do_force_soft(RT_TASK *rt_task);
                        struct task_struct *ltsk;
                        if ((ltsk = find_task_by_pid(arg0.name)))  {
                                if ((arg0.rt_task = ltsk->this_rt_task[0])) {
					if ((arg0.rt_task->force_soft = ((int)arg0.rt_task->is_hard > 0) && FORCE_SOFT)) {
						rt_do_force_soft(arg0.rt_task);
					}
                                        return (unsigned long)arg0.rt_task;
                                }
                        }
                        return 0;
                }

		case IS_HARD: {
			return (int)arg0.rt_task->is_hard > 0;
		}
		case GET_EXECTIME: {
			struct arg { RT_TASK *task; RTIME *exectime; };
			if ((larg->task)->exectime[0] && (larg->task)->exectime[1]) {
				larg->exectime[0] = (larg->task)->exectime[0]; 
				larg->exectime[1] = (larg->task)->exectime[1]; 
				larg->exectime[2] = rdtsc(); 
			}
                        return 0;
		}
		case GET_TIMEORIG: {
			struct arg { RTIME *time_orig; };
			rt_gettimeorig(larg->time_orig);
                        return 0;
		}

	        default:

		    rt_printk("RTAI/LXRT: Unknown srq #%d\n", srq);
		    return -ENOSYS;
	}
	return 0;
}

asmlinkage long long rtai_lxrt_invoke (unsigned int lxsrq, void *arg)

{
    long long retval;

#ifdef CONFIG_RTAI_TRACE
    trace_true_lxrt_rtai_syscall_entry();
#endif /* CONFIG_RTAI_TRACE */
    retval = handle_lxrt_request(lxsrq,arg);
#ifdef CONFIG_RTAI_TRACE
    trace_true_lxrt_rtai_syscall_exit();
#endif /* CONFIG_RTAI_TRACE */

    return retval;
}

asmlinkage int rtai_lxrt_fastpath (void)

{
    /* Returns zero if we may process pending Linux work on our return
       path (i.e. long return path through reschedule), or non-zero if
       we may not (fast return path). In the former case, also unstall
       the Linux stage into the Adeos pipeline so that interrupts can
       flow anew. */

	if (test_bit(hard_cpu_id(), &rtai_cpu_realtime)) {
		return 1;
	}
	rtai_linux_sti();
	return 0;
}

int set_rt_fun_ext_index(struct rt_fun_entry *fun, int idx)
{
	if (idx > 0 && idx < MAX_FUN_EXT && !rt_fun_ext[idx]) {
		rt_fun_ext[idx] = fun;
		return 0;
	}
	return -EACCES;
}

void reset_rt_fun_ext_index( struct rt_fun_entry *fun, int idx)
{
	if (idx > 0 && idx < MAX_FUN_EXT && rt_fun_ext[idx] == fun) {
		rt_fun_ext[idx] = 0;
	}
}

static void linux_process_termination(void)

{
	unsigned long num;
	void *adr;
	int type;
	char name[8];
	RT_TASK *task2delete;
/*
 * Linux is just about to schedule current out of existence. With this feature, 
 * LXRT frees the real time resources allocated to it.
*/
	while ((num = is_process_registered(current))) {
		rt_global_cli();
		adr = rt_get_adr(num);
		type = rt_get_type(num);
		rt_drg_on_adr(adr); 
		rt_global_sti();
		num2nam(num, name);
       		switch (type) {
			case IS_SEM:
				rt_printk("LXRT releases SEM %s\n", name);
				lxrt_sem_delete(adr);
				rt_free(adr);
				break;
			case IS_RWL:
				rt_printk("LXRT releases RWL %s\n", name);
				lxrt_rwl_delete(adr);
				rt_free(adr);
				break;
			case IS_SPL:
				rt_printk("LXRT releases SPL %s\n", name);
				lxrt_spl_delete(adr);
				rt_free(adr);
				break;
			case IS_MBX:
				rt_printk("LXRT releases MBX %s\n", name);
				lxrt_mbx_delete(adr);
				rt_free(adr);
				break;
			case IS_PRX:
				num = rttask2pid(adr);
				rt_printk("LXRT releases PROXY PID %lu\n", num);
				lxrt_Proxy_detach(num);
				break;
			case IS_TASK:
				rt_printk("LXRT deregisters task %s\n", name);
				break;
		}
	}
	if ((task2delete = current->this_rt_task[0])) {
		if (!clr_rtext(task2delete)) {
			rt_drg_on_adr(task2delete); 
			rt_printk("LXRT releases PID %d (ID: %s).\n", current->pid, current->comm);
			rt_free(task2delete->msg_buf[0]);
			rt_free(task2delete);
			current->this_rt_task[0] = current->this_rt_task[1] = 0;
		}
	}
}

int lxrt_init_archdep (void)

{
    RT_TASK *rt_linux_tasks[NR_RT_CPUS];

    sidt = rt_set_full_intr_vect(RTAI_LXRT_VECTOR, 15, 3, (void *)&RTAI_LXRT_HANDLER);

    if (set_rtai_callback(linux_process_termination))
	{
	printk("Could not setup rtai_callback\n");
	return -ENODEV;
	}

    rt_fun_ext[0] = rt_fun_lxrt;
    rt_get_base_linux_task(rt_linux_tasks);
    rt_linux_tasks[0]->task_trap_handler[0] = (void *)set_rt_fun_ext_index;
    rt_linux_tasks[0]->task_trap_handler[1] = (void *)reset_rt_fun_ext_index;

    return 0;
}

void lxrt_exit_archdep (void)

{
    rt_reset_full_intr_vect(RTAI_LXRT_VECTOR, sidt);
    remove_rtai_callback(linux_process_termination);
}
