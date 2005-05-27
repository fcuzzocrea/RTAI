/**
 * @ingroup lxrt
 * @file
 *
 * LXRT main header.
 *
 * @author Paolo Mantegazza
 *
 * @note Copyright &copy; 1999-2003 Paolo Mantegazza <mantegazza@aero.polimi.it>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 *
 * ACKNOWLEDGMENTS:
 * Pierre Cloutier (pcloutier@poseidoncontrols.com) has suggested the 6 
 * characters names and fixed many inconsistencies within this file.
 */

/**
 * @defgroup lxrt LXRT module.
 *
 * LXRT services (soft-hard real time in user space)
 *
 * LXRT is a module that allows you to use all the services made available by
 * RTAI and its schedulers in user space, both for soft and hard real time. At
 * the moment it is a feature youll find nowhere but with RTAI. For an
 * explanation of how it works see
 * @ref lxrt_faq "Pierre Cloutiers LXRT-INFORMED FAQs", and the explanation of
 * @ref whatis_lxrt "the implementation of hard real time in user space"
 * (contributed by: Pierre Cloutier, Paolo Mantegazza, Steve Papacharalambous).
 *
 * LXRT-INFORMED should be the production version of LXRT, the latter being the
 * development version. So it can happen that LXRT-INFORMED could be lagging
 * slightly behind LXRT.  If you need to hurry to the services not yet ported to
 * LXRT-INFORMED do it without pain. Even if you are likely to miss some useful
 * services found only in LXRT-INFORMED, we release only when a feature is
 * relatively stable.
 *
 * From what said above there should be no need for anything specific as all the
 * functions you can use in user space have been already documented in this
 * manual.   There are however a few exceptions that need to be explained.
 *
 * Note also that, as already done for the shared memory services in user space,
 * the function calls for Linux processes are inlined in the file
 * rtai_lxrt.h. This approach has been preferred to a library since it is
 * simpler, more effective, the calls are short and simple so that, even if it
 * is likely that there can be more than just a few per process, they could
 * never be charged of making codes too bigger.   Also common to shared memory
 * is the use of unsigned int to identify LXRT objects.   If you want to use
 * string identifiers the same support functions, i.e. nam2num() and
 * num2nam(), can be used.
 *
 *@{*/

#ifndef _RTAI_LXRT_H
#define _RTAI_LXRT_H

#include <rtai_sched.h>
#include <rtai_nam2num.h>

// scheduler
#define YIELD				 0
#define SUSPEND				 1
#define RESUME				 2
#define MAKE_PERIODIC 			 3
#define WAIT_PERIOD	 		 4
#define SLEEP		 		 5
#define SLEEP_UNTIL	 		 6
#define START_TIMER	 		 7
#define STOP_TIMER	 		 8
#define GET_TIME	 		 9
#define COUNT2NANO			10
#define NANO2COUNT			11
#define BUSY_SLEEP			12
#define SET_PERIODIC_MODE		13
#define SET_ONESHOT_MODE		14
#define SIGNAL_HANDLER	 		15
#define TASK_USE_FPU			16
#define LINUX_USE_FPU			17
#define PREEMPT_ALWAYS_GEN		18
#define GET_TIME_NS			19
#define GET_CPU_TIME_NS			20
#define SET_RUNNABLE_ON_CPUS		21 
#define SET_RUNNABLE_ON_CPUID		22	 
#define GET_TIMER_CPU			23	 
#define START_RT_APIC_TIMERS		24
#define PREEMPT_ALWAYS_CPUID		25
#define COUNT2NANO_CPUID		26
#define NANO2COUNT_CPUID		27
#define GET_TIME_CPUID			28
#define GET_TIME_NS_CPUID       	29
#define MAKE_PERIODIC_NS 		30
#define SET_SCHED_POLICY 		31
#define SET_RESUME_END 			32
#define SPV_RMS 			33
#define WAKEUP_SLEEPING			34
#define CHANGE_TASK_PRIO 		35
#define SET_RESUME_TIME 		36
#define SET_PERIOD        		37
#define HARD_TIMER_RUNNING              38

// semaphores
#define TYPED_SEM_INIT 			39
#define SEM_DELETE			40
#define NAMED_SEM_INIT 			41
#define NAMED_SEM_DELETE		42
#define SEM_SIGNAL			43
#define SEM_WAIT			44
#define SEM_WAIT_IF			45
#define SEM_WAIT_UNTIL			46
#define SEM_WAIT_TIMED			47
#define SEM_BROADCAST       		48
#define SEM_WAIT_BARRIER 		49
#define SEM_COUNT			50
#define COND_WAIT			51
#define COND_WAIT_UNTIL			52
#define COND_WAIT_TIMED			53
#define RWL_INIT 			54
#define RWL_DELETE			55
#define NAMED_RWL_INIT 			56
#define NAMED_RWL_DELETE		57
#define RWL_RDLOCK 			58
#define RWL_RDLOCK_IF 			59
#define RWL_RDLOCK_UNTIL 		60
#define RWL_RDLOCK_TIMED 		61
#define RWL_WRLOCK 			62	
#define RWL_WRLOCK_IF	 		63
#define RWL_WRLOCK_UNTIL		64
#define RWL_WRLOCK_TIMED		65
#define RWL_UNLOCK 			66
#define SPL_INIT 			67
#define SPL_DELETE			68
#define NAMED_SPL_INIT 			69
#define NAMED_SPL_DELETE		70
#define SPL_LOCK 			71	
#define SPL_LOCK_IF	 		72
#define SPL_LOCK_TIMED			73
#define SPL_UNLOCK 			74

// mail boxes
#define TYPED_MBX_INIT 			75
#define MBX_DELETE			76
#define NAMED_MBX_INIT 			77
#define NAMED_MBX_DELETE		78
#define MBX_SEND			79
#define MBX_SEND_WP 			80
#define MBX_SEND_IF 			81
#define MBX_SEND_UNTIL			82
#define MBX_SEND_TIMED			83
#define MBX_RECEIVE 			84
#define MBX_RECEIVE_WP 			85
#define MBX_RECEIVE_IF 			86
#define MBX_RECEIVE_UNTIL		87
#define MBX_RECEIVE_TIMED		88
#define MBX_EVDRP			89
#define MBX_OVRWR_SEND                  90

// short intertask messages
#define SENDMSG				91
#define SEND_IF				92
#define SEND_UNTIL			93
#define SEND_TIMED			94
#define RECEIVEMSG			95
#define RECEIVE_IF			96
#define RECEIVE_UNTIL			97
#define RECEIVE_TIMED			98
#define RPCMSG				99
#define RPC_IF			       100
#define RPC_UNTIL		       101
#define RPC_TIMED		       102
#define EVDRP			       103
#define ISRPC			       104
#define RETURNMSG		       105

// extended intertask messages
#define RPCX			       106
#define RPCX_IF			       107
#define RPCX_UNTIL		       108
#define RPCX_TIMED		       109
#define SENDX			       110
#define SENDX_IF		       111
#define SENDX_UNTIL		       112
#define SENDX_TIMED		       113
#define RETURNX			       114
#define RECEIVEX		       115
#define RECEIVEX_IF		       116
#define RECEIVEX_UNTIL		       117
#define RECEIVEX_TIMED		       118
#define EVDRPX			       119

// proxies
#define PROXY_ATTACH                   120
#define PROXY_DETACH     	       121
#define PROXY_TRIGGER                  122


// synchronous user space specific intertask messages and related proxies
#define RT_SEND                        123
#define RT_RECEIVE                     124
#define RT_CRECEIVE          	       125
#define RT_REPLY                       126
#define RT_PROXY_ATTACH                127
#define RT_PROXY_DETACH                128
#define RT_TRIGGER                     129
#define RT_NAME_ATTACH                 130
#define RT_NAME_DETACH                 131
#define RT_NAME_LOCATE                 132

// bits
#define BITS_INIT          	       133	
#define BITS_DELETE        	       134
#define NAMED_BITS_INIT    	       135
#define NAMED_BITS_DELETE  	       136
#define BITS_GET           	       137
#define BITS_RESET         	       138
#define BITS_SIGNAL        	       139
#define BITS_WAIT          	       140
#define BITS_WAIT_IF       	       141		
#define BITS_WAIT_UNTIL    	       142
#define BITS_WAIT_TIMED   	       143

// typed mail boxes
#define TBX_INIT                       144
#define TBX_DELETE         	       145
#define NAMED_TBX_INIT                 146
#define NAMED_TBX_DELETE               147
#define TBX_SEND                       148
#define TBX_SEND_IF                    149
#define TBX_SEND_UNTIL                 150
#define TBX_SEND_TIMED                 151
#define TBX_RECEIVE                    152
#define TBX_RECEIVE_IF                 153
#define TBX_RECEIVE_UNTIL              154
#define TBX_RECEIVE_TIMED              155
#define TBX_BROADCAST                  156
#define TBX_BROADCAST_IF               157
#define TBX_BROADCAST_UNTIL            158
#define TBX_BROADCAST_TIMED            159
#define TBX_URGENT                     160
#define TBX_URGENT_IF                  161
#define TBX_URGENT_UNTIL               162
#define TBX_URGENT_TIMED               163

// pqueue
#define MQ_OPEN         	       164
#define MQ_RECEIVE      	       165
#define MQ_SEND         	       166
#define MQ_CLOSE        	       167
#define MQ_GETATTR     		       168
#define MQ_SETATTR      	       169
#define MQ_NOTIFY       	       170
#define MQ_UNLINK       	       171
#define MQ_TIMEDRECEIVE 	       172
#define MQ_TIMEDSEND    	       173

// named tasks init/delete
#define NAMED_TASK_INIT 	       174
#define NAMED_TASK_INIT_CPUID 	       175
#define NAMED_TASK_DELETE	       176

// registry
#define GET_ADR         	       177
#define GET_NAME         	       178

// netrpc
#define NETRPC			       179
#define SEND_REQ_REL_PORT	       180
#define DDN2NL			       181
#define SET_THIS_NODE		       182
#define FIND_ASGN_STUB		       183
#define REL_STUB		       184	
#define WAITING_RETURN		       185

// a semaphore extension
#define COND_SIGNAL		       186

// new shm
#define SHM_ALLOC                      187
#define SHM_FREE                       188
#define SHM_SIZE                       189
#define HEAP_SET                       190
#define HEAP_ALLOC                     191
#define HEAP_FREE                      192
#define HEAP_NAMED_ALLOC               193
#define HEAP_NAMED_FREE                194
#define MALLOC                         195
#define FREE                           196
#define NAMED_MALLOC                   197
#define NAMED_FREE                     198

#define SUSPEND_IF		       199
#define SUSPEND_UNTIL	 	       200
#define SUSPEND_TIMED		       201
#define IRQ_WAIT		       202	
#define IRQ_WAIT_IF		       203	
#define IRQ_WAIT_UNTIL		       204
#define IRQ_WAIT_TIMED		       205
#define IRQ_SIGNAL		       206
#define REQUEST_IRQ_TASK	       207
#define RELEASE_IRQ_TASK	       208
#define SCHED_LOCK		       209
#define SCHED_UNLOCK		       210
#define PEND_LINUX_IRQ		       211
#define RECEIVE_LINUX_SYSCALL          212
#define RETURN_LINUX_SYSCALL           213

#define MAX_LXRT_FUN                   215

// not recovered yet 
// Qblk's 
#define RT_INITTICKQUEUE		69
#define RT_RELEASETICKQUEUE     	70
#define RT_QDYNALLOC            	71
#define RT_QDYNFREE             	72
#define RT_QDYNINIT             	73
#define RT_QBLKWAIT			74
#define RT_QBLKREPEAT			75
#define RT_QBLKSOON			76
#define RT_QBLKDEQUEUE			77
#define RT_QBLKCANCEL			78
#define RT_QSYNC			79
#define RT_QRECEIVE			80
#define RT_QLOOP			81
#define RT_QSTEP			82
#define RT_QBLKBEFORE			83
#define RT_QBLKAFTER			84
#define RT_QBLKUNHOOK			85
#define RT_QBLKRELEASE			86
#define RT_QBLKCOMPLETE			87
#define RT_QHOOKFLUSH			88
#define RT_QBLKATHEAD			89
#define RT_QBLKATTAIL			90
#define RT_QHOOKINIT			91
#define RT_QHOOKRELEASE			92
#define RT_QBLKSCHEDULE			93
#define RT_GETTICKQUEUEHOOK		94
// Testing
#define RT_BOOM				95
#define RTAI_MALLOC			96
#define RT_FREE				97
#define RT_MMGR_STATS			98
#define RT_STOMP                	99
// VC
#define RT_VC_ATTACH            	100
#define RT_VC_RELEASE           	101
#define RT_VC_RESERVE          		102
// Linux Signal Support
#define RT_GET_LINUX_SIGNAL		103
#define RT_GET_ERRNO			104
#define RT_SET_LINUX_SIGNAL_HANDLER	105
// end of not recovered yet

#define LXRT_GET_ADR		1000
#define LXRT_GET_NAME   	1001
#define LXRT_TASK_INIT 		1002
#define LXRT_TASK_DELETE 	1003
#define LXRT_SEM_INIT  		1004
#define LXRT_SEM_DELETE		1005
#define LXRT_MBX_INIT 		1006
#define LXRT_MBX_DELETE		1007
#define MAKE_SOFT_RT		1008
#define MAKE_HARD_RT		1009
#define PRINT_TO_SCREEN		1010
#define NONROOT_HRT		1011
#define RT_BUDDY		1012
#define HRT_USE_FPU     	1013
#define USP_SIGHDL      	1014
#define GET_USP_FLAGS   	1015
#define SET_USP_FLAGS   	1016
#define GET_USP_FLG_MSK 	1017
#define SET_USP_FLG_MSK 	1018
#define IS_HARD         	1019
#define LINUX_SERVER_INIT	1020
#define ALLOC_REGISTER 		1021
#define DELETE_DEREGISTER	1022
#define FORCE_TASK_SOFT  	1023
#define PRINTK			1024
#define GET_EXECTIME		1025
#define GET_TIMEORIG 		1026
#define LXRT_RWL_INIT		1027
#define LXRT_RWL_DELETE 	1028
#define LXRT_SPL_INIT		1029
#define LXRT_SPL_DELETE 	1030

#define FORCE_SOFT 0x80000000

// Keep LXRT call enc/decoding together, so you are sure to act consistently.
// This is the encoding, note " | GT_NR_SYSCALLS" to ensure not a Linux syscall, ...
#define GT_NR_SYSCALLS  (1 << 15)
#define ENCODE_LXRT_REQ(dynx, srq, lsize)  (((dynx) << 28) | (((srq) & 0xFFF) << 16) | GT_NR_SYSCALLS | (lsize))
// ... and this is the decoding.
#define SRQ(x)   (((x) >> 16) & 0xFFF)
#define NARG(x)  ((x) & (GT_NR_SYSCALLS - 1))
#define INDX(x)  (((x) >> 28) & 0xF)

#ifdef __KERNEL__

#include <asm/rtai_lxrt.h>

/*
     Encoding of system call argument
            31                                    0  
soft SRQ    .... |||| |||| |||| .... .... .... ....  0 - 4095 max
int  NARG   .... .... .... .... |||| |||| |||| ||||  
arg  INDX   |||| .... .... .... .... .... .... ....
*/

/*
These USP (unsigned long long) type fields allow to read and write up to 2 arguments.  
                                               
RW marker .... .... .... .... .... .... .... ..|| .... .... .... .... .... .... .... ...|

HIGH unsigned long encodes writes
W ARG1 BF .... .... .... .... .... ...| |||| ||..
W ARG1 SZ .... .... .... .... |||| |||. .... ....
W ARG2 BF .... .... .||| |||| .... .... .... ....
W ARG2 SZ ..|| |||| |... .... .... .... .... ....
W 1st  LL .|.. .... .... .... .... .... .... ....
W 2nd  LL |... .... .... .... .... .... .... ....

LOW unsigned long encodes reads
R ARG1 BF .... .... .... .... .... ...| |||| ||..
R ARG1 SZ .... .... .... .... |||| |||. .... ....
R ARG2 BF .... .... .||| |||| .... .... .... ....
R ARG2 SZ ..|| |||| |... .... .... .... .... ....
R 1st  LL .|.. .... .... .... .... .... .... ....
R 2nd  LL |... .... .... .... .... .... .... ....

LOW unsigned long encodes also
RT Switch .... .... .... .... .... .... .... ...|

and 
Always 0  .... .... .... .... .... .... .... ..|.

If SZ is zero sizeof(int) is copied by default, if LL bit is set sizeof(long long) is copied.
*/

// These are for setting appropriate bits in any function entry structure, OR
// them in fun entry type to obtain the desired encoding

// for writes
#define UW1(bf, sz)  ((((unsigned long long)((((bf) & 0x7F) <<  2) | (((sz) & 0x7F) <<  9))) << 32) | 0x300000001LL)
#define UW2(bf, sz)  ((((unsigned long long)((((bf) & 0x7F) << 16) | (((sz) & 0x7F) << 23))) << 32) | 0x300000001LL)
#define UWSZ1LL      (0x4000000300000001LL)
#define UWSZ2LL      (0x8000000300000001LL)

// for reads
#define UR1(bf, sz)  ((((bf) & 0x7F) <<  2) | (((sz) & 0x7F) <<  9) | 0x300000001LL)
#define UR2(bf, sz)  ((((bf) & 0x7F) << 16) | (((sz) & 0x7F) << 23) | 0x300000001LL)
#define URSZ1LL      (0x340000001LL)
#define URSZ2LL      (0x380000001LL)

// and these are for deciding what to do in lxrt.c
#if 0
#define	NEED_TO_RW(x)	(((unsigned long *)&(x))[HIGH])

#define NEED_TO_R(x)	(((unsigned long *)&(x))[LOW]  & 0x0000FFFC)
#define NEED_TO_W(x)	(((unsigned long *)&(x))[HIGH] & 0x0000FFFC)

#define NEED_TO_R2ND(x)	(((unsigned long *)&(x))[LOW]  & 0x3FFF0000)
#define NEED_TO_W2ND(x)	(((unsigned long *)&(x))[HIGH] & 0x3FFF0000)

#define USP_RBF1(x)  	((((unsigned long *)&(x))[LOW] >>  2) & 0x7F)
#define USP_RSZ1(x)    	((((unsigned long *)&(x))[LOW] >>  9) & 0x7F)
#define USP_RBF2(x)    	((((unsigned long *)&(x))[LOW] >> 16) & 0x7F)
#define USP_RSZ2(x)    	((((unsigned long *)&(x))[LOW] >> 23) & 0x7F)
#define USP_RSZ1LL(x)  	(((unsigned long *)&(x))[LOW] & 0x40000000)
#define USP_RSZ2LL(x)  	(((unsigned long *)&(x))[LOW] & 0x80000000)

#define USP_WBF1(x)   	((((unsigned long *)&(x))[HIGH] >>  2) & 0x7F)
#define USP_WSZ1(x)    	((((unsigned long *)&(x))[HIGH] >>  9) & 0x7F)
#define USP_WBF2(x)    	((((unsigned long *)&(x))[HIGH] >> 16) & 0x7F)
#define USP_WSZ2(x)    	((((unsigned long *)&(x))[HIGH] >> 23) & 0x7F)
#define USP_WSZ1LL(x)   (((unsigned long *)&(x))[HIGH] & 0x40000000)
#define USP_WSZ2LL(x)   (((unsigned long *)&(x))[HIGH] & 0x80000000)
#else
#define	NEED_TO_RW(x)	((x >> 32) & 0xFFFFFFFF)

#define NEED_TO_R(x)	(x & 0x0000FFFC)
#define NEED_TO_W(x)	((x >> 32) & 0x0000FFFC)

#define NEED_TO_R2ND(x)	(x & 0x3FFF0000)
#define NEED_TO_W2ND(x)	((x >> 32) & 0x3FFF0000)

#define USP_RBF1(x)  	((x >> 2) & 0x7F)
#define USP_RSZ1(x)    	((x >> 9) & 0x7F)
#define USP_RBF2(x)    	((x >> 16) & 0x7F)
#define USP_RSZ2(x)    	((x >> 23) & 0x7F)
#define USP_RSZ1LL(x)  	(x & 0x40000000)
#define USP_RSZ2LL(x)  	(x & 0x80000000)

#define USP_WBF1(x)   	(((x >> 32) >>  2) & 0x7F)
#define USP_WSZ1(x)    	(((x >> 32) >>  9) & 0x7F)
#define USP_WBF2(x)    	(((x >> 32) >> 16) & 0x7F)
#define USP_WSZ2(x)    	(((x >> 32) >> 23) & 0x7F)
#define USP_WSZ1LL(x)   ((x >> 32) & 0x40000000)
#define USP_WSZ2LL(x)   ((x >> 32) & 0x80000000)
#endif

struct rt_fun_entry {
    unsigned long long type;
    void *fun;
};

struct rt_native_fun_entry {
    struct rt_fun_entry fun;
    int index;
};

extern struct rt_fun_entry rt_fun_lxrt[];

void reset_rt_fun_entries(struct rt_native_fun_entry *entry);

int set_rt_fun_entries(struct rt_native_fun_entry *entry);

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#if CONFIG_RTAI_INTERNAL_LXRT_SUPPORT
 
static inline struct rt_task_struct *pid2rttask(pid_t pid)
{
        return ((unsigned long)pid) > PID_MAX_LIMIT ? (struct rt_task_struct *)pid : find_task_by_pid(pid)->rtai_tskext(0);
}

static inline pid_t rttask2pid(struct rt_task_struct * task)
{
    return task->lnxtsk ? task->lnxtsk->pid : (int) task;
}

#else /* !CONFIG_RTAI_INTERNAL_LXRT_SUPPORT */

static inline struct rt_task_struct *pid2rttask(pid_t pid)
{
    return 0;
}

// The following might look strange but it must be so to work with
// buddies also.
static inline pid_t rttask2pid(struct rt_task_struct * task)
{
    return (long) task;
}

#endif /* CONFIG_RTAI_INTERNAL_LXRT_SUPPORT */

int set_rtai_callback(void (*fun)(void));

void remove_rtai_callback(void (*fun)(void));

RT_TASK *rt_lxrt_whoami(void);

void exec_func(void (*func)(void *data, int evn),
	       void *data,
	       int evn);

int  set_rt_fun_ext_index(struct rt_fun_entry *fun,
			  int idx);

void reset_rt_fun_ext_index(struct rt_fun_entry *fun,
			    int idx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#else /* !__KERNEL__ */

#include <sys/types.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <asm/rtai_lxrt.h>

struct apic_timer_setup_data;

#define rt_grow_and_lock_stack(incr) \
        do { \
                char buf[incr]; \
                memset(buf, 0, incr); \
                mlockall(MCL_CURRENT | MCL_FUTURE); \
        } while (0)

#define BIDX   0 // rt_fun_ext[0]
#define SIZARG sizeof(arg)

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Get an object address by its name.
 *
 * rt_get_adr returns the address associated to @a name.
 *
 * @return the address associated to @a name on success, 0 on failure
 */
RTAI_PROTO(void *, rt_get_adr, (unsigned long name))
{
	struct { unsigned long name; } arg = { name };
	return rtai_lxrt(BIDX, SIZARG, LXRT_GET_ADR, &arg).v[LOW];
} 

/**
 * Get an object name by its address.
 *
 * rt_get_name returns the name pointed by the address @a adr.
 *
 * @return the identifier pointed by the address @a adr on success, 0 on
 * failure.
 */
RTAI_PROTO(unsigned long, rt_get_name, (void *adr))
{
	struct { void *adr; } arg = { adr };
	return rtai_lxrt(BIDX, SIZARG, LXRT_GET_NAME, &arg).i[LOW];
}

RTAI_PROTO(RT_TASK *, rt_task_init_schmod, (unsigned long name, int priority, int stack_size, int max_msg_size, int policy, int cpus_allowed))
{
        struct sched_param mysched;
        struct { unsigned long name; int priority, stack_size, max_msg_size, cpus_allowed; } arg = { name, priority, stack_size, max_msg_size, cpus_allowed };

        mysched.sched_priority = sched_get_priority_max(policy) - priority;
        if (mysched.sched_priority < 1 ) {
        	mysched.sched_priority = 1;
	}
        if (sched_setscheduler(0, policy, &mysched) < 0) {
                return 0;
        }
	rtai_iopl();

	return (RT_TASK *)rtai_lxrt(BIDX, SIZARG, LXRT_TASK_INIT, &arg).v[LOW];
}

#define RT_THREAD_STACK_MIN 64*1024

#if 1
#include <pthread.h>

RTAI_PROTO(int, rt_thread_create,(void *fun, void *args, int stack_size))
{
	pthread_t thread;
	pthread_attr_t attr;
        pthread_attr_init(&attr);
        if (pthread_attr_setstacksize(&attr, stack_size > RT_THREAD_STACK_MIN ? stack_size : RT_THREAD_STACK_MIN)) {
                return -1;
        }
	if (pthread_create(&thread, &attr, (void *(*)(void *))fun, args)) {
		return -1;
	}
	return thread;
}

RTAI_PROTO(int, rt_thread_join, (int thread))
{
	return pthread_join((pthread_t)thread, NULL);
}

#else

#include <sys/wait.h>

RTAI_PROTO(int, rt_thread_create, (void *fun, void *args, int stack_size))
{
	void *sp;
	if (stack_size < RT_THREAD_STACK_MIN) {
		stack_size = RT_THREAD_STACK_MIN;
	}
	memset(sp = malloc(stack_size), 0, stack_size);
	sp = (void *)(((unsigned long)sp + stack_size - 16) & ~0xF);
	return clone(fun, sp, CLONE_VM | CLONE_FS | CLONE_FILES, args);
}

RTAI_PROTO(int, rt_thread_join, (int thread))
{
	return waitpid(thread, NULL, 0);
}

#endif

#ifndef __SUPPORT_LINUX_SERVER__
#define __SUPPORT_LINUX_SERVER__

#include <asm/ptrace.h>
#include <unistd.h>

static inline void rt_receive_linux_syscall(RT_TASK *task, struct pt_regs *regs)
{
	struct { RT_TASK *task; struct pt_regs *regs; } arg = { task, regs };
	rtai_lxrt(BIDX, SIZARG, RECEIVE_LINUX_SYSCALL, &arg);
}

static inline void rt_return_linux_syscall(RT_TASK *task, unsigned long retval)
{
	struct { RT_TASK *task; unsigned long retval; } arg = { task, retval };
	rtai_lxrt(BIDX, SIZARG, RETURN_LINUX_SYSCALL, &arg);
}

#include <rtai_msg.h>
static void linux_syscall_server_fun(RT_TASK *task)
{
        struct pt_regs regs;
	rtai_lxrt(BIDX, sizeof(RT_TASK *), LINUX_SERVER_INIT, &task);
	rtai_lxrt(BIDX, sizeof(RT_TASK *), RESUME, &task);
        for (;;) {
#if 1
                rt_receive_linux_syscall(task, &regs);
		rt_return_linux_syscall(task, syscall(regs.LINUX_SYSCALL_NR, regs.LINUX_SYSCALL_REG1, regs.LINUX_SYSCALL_REG2, regs.LINUX_SYSCALL_REG3, regs.LINUX_SYSCALL_REG4, regs.LINUX_SYSCALL_REG5, regs.LINUX_SYSCALL_REG6));
#else
		int retval;
		rt_receivex(task, &regs, sizeof(struct pt_regs), &retval);
		retval = syscall(regs.LINUX_SYSCALL_NR, regs.LINUX_SYSCALL_REG1, regs.LINUX_SYSCALL_REG2, regs.LINUX_SYSCALL_REG3, regs.LINUX_SYSCALL_REG4, regs.LINUX_SYSCALL_REG5, regs.LINUX_SYSCALL_REG6);
		rt_returnx(task, &retval, sizeof(retval));
#endif
        }
}

#endif /* __SUPPORT_LINUX_SERVER__ */

RTAI_PROTO(int, rt_linux_syscall_server_create, (RT_TASK * task))
{
	if (rt_thread_create((void *)linux_syscall_server_fun, task, 0) > 0) {
		printf(" \b");
		rtai_lxrt(BIDX, sizeof(RT_TASK *), SUSPEND, &task);
		return 0;
	}
	return -1;
}

RTAI_PROTO(RT_TASK *, rt_thread_init, (unsigned long name, int priority, int max_msg_size, int policy, int cpus_allowed))
{
	return rt_task_init_schmod(name, priority, 0, max_msg_size, policy, cpus_allowed);
}

/**
 * Create a new real time task in user space.
 * 
 * rt_task_init provides a real time buddy, also called proxy, task to the Linux
 * process that wants to access RTAI scheduler services.   It needs no task
 * function as none is used, but it does need to setup a task structure and
 * initialize it appropriately as the provided services are carried out as if
 * the Linux process has become an RTAI task.   Because of that it requires less
 * arguments and returns the pointer to the task that is to be used in related
 * calls.
 *
 * @param name is a unique identifier that is possibly used by easing
 * referencing the buddy RTAItask, and thus its peer Linux process.
 *
 * @param priority is the priority of the buddys priority.
 *
 * @param stack_size is just what is implied by such a name and refers to the
 * stack size used by the buddy.
 *
 * @param max_msg_size is a hint for the size of the most lengthy message than
 * is likely to be exchanged.
 *
 * @a stack_size and @a max_msg_size can be zero, in which case the default
 * internal values are used.  The assignment of a different value should be
 * required only if you want to use task signal functions. In such a case note
 * that these signal functions are intended to catch asyncrounous events in
 * kernel space and, as such, must be programmed into a companion module and
 * interfaced to their parent Linux process through the available services.
 *
 * Keep an eye on the default stack (512) and message (256) sizes as they seem
 * to be acceptable, but this API has not been used extensively with complex
 * interrupt service  routines.   Since the latter are served on the stack of
 * any task being interrupted, and more than one can pile up on the same stack,
 * it can be possible that a larger stack is required.   In such a case either
 * recompile lxrt.c with macros STACK_SIZE and MSG_SIZE set appropriately, or
 * explicitly assign larger values at your buddy tasks  inits.   Note that while
 * the stack size can be critical the message size will not. In fact the module
 * reassigns it, appropriately sized, whenever it is needed.   The cost is a
 * kmalloc with GFP_KERNEL that can block, but within the Linux environment.
 * Note also that @a max_msg_size is for a buffer to be used to copy whatever
 * message, either mailbox or inter task, from user to kernel space, as messages
 * are not necessarily copied immediately, and has nothing to do directly with
 * what you are doing.
 *
 * It is important to remark that the returned task pointers cannot be used
 * directly, they are for kernel space data, but just passed as arguments when
 * needed.
 *
 * @return On success a pointer to the task structure initialized in kernel
 * space.
 * @return On failure a 0 value is returned if it was not possible to setup the
 * buddy task or something using the same name was found.
 */
RTAI_PROTO(RT_TASK *,rt_task_init,(unsigned long name, int priority, int stack_size, int max_msg_size))
{
	return rt_task_init_schmod(name, priority, 0, max_msg_size, SCHED_FIFO, 0xFF);
}

RTAI_PROTO(void,rt_set_sched_policy,(RT_TASK *task, int policy, int rr_quantum_ns))
{
	struct { RT_TASK *task; long policy; long rr_quantum_ns; } arg = { task, policy, rr_quantum_ns };
	rtai_lxrt(BIDX, SIZARG, SET_SCHED_POLICY, &arg);
}

RTAI_PROTO(int,rt_change_prio,(RT_TASK *task, int priority))
{
	struct { RT_TASK *task; long priority; } arg = { task, priority };
	return rtai_lxrt(BIDX, SIZARG, CHANGE_TASK_PRIO, &arg).i[LOW];
}

/**
 * Return a hard real time Linux process, or pthread to the standard Linux
 * behavior.
 *
 * rt_make_soft_real_time returns to soft Linux POSIX real time a process, from
 * which it is called, that was made hard real time by a call to
 * rt_make_hard_real_time.
 *
 * Only the process itself can use this functions, it is not possible to impose
 * the related transition from another process.
 *
 */
RTAI_PROTO(void,rt_make_soft_real_time,(void))
{
	struct { unsigned long dummy; } arg;
	rtai_lxrt(BIDX, SIZARG, MAKE_SOFT_RT, &arg);
}

RTAI_PROTO(int,rt_task_delete,(RT_TASK *task))
{
	struct { RT_TASK *task; } arg = { task };
	rt_make_soft_real_time();
	return rtai_lxrt(BIDX, SIZARG, LXRT_TASK_DELETE, &arg).i[LOW];
}

RTAI_PROTO(int,rt_task_yield,(void))
{
	struct { unsigned long dummy; } arg;
	return rtai_lxrt(BIDX, SIZARG, YIELD, &arg).i[LOW];
}

RTAI_PROTO(int,rt_task_suspend,(RT_TASK *task))
{
	struct { RT_TASK *task; } arg = { task };
	return rtai_lxrt(BIDX, SIZARG, SUSPEND, &arg).i[LOW];
}

RTAI_PROTO(int,rt_task_suspend_if,(RT_TASK *task))
{
	struct { RT_TASK *task; } arg = { task };
	return rtai_lxrt(BIDX, SIZARG, SUSPEND_IF, &arg).i[LOW];
}

RTAI_PROTO(int,rt_task_suspend_until,(RT_TASK *task, RTIME time))
{
	struct { RT_TASK *task; RTIME time; } arg = { task, time };
	return rtai_lxrt(BIDX, SIZARG, SUSPEND_UNTIL, &arg).i[LOW];
}

RTAI_PROTO(int,rt_task_suspend_timed,(RT_TASK *task, RTIME delay))
{
	struct { RT_TASK *task; RTIME delay; } arg = { task, delay };
	return rtai_lxrt(BIDX, SIZARG, SUSPEND_TIMED, &arg).i[LOW];
}

RTAI_PROTO(int,rt_task_resume,(RT_TASK *task))
{
	struct { RT_TASK *task; } arg = { task };
	return rtai_lxrt(BIDX, SIZARG, RESUME, &arg).i[LOW];
}

RTAI_PROTO(void, rt_sched_lock, (void))
{
	struct { int dummy; } arg;
	rtai_lxrt(BIDX, SIZARG, SCHED_LOCK, &arg);
}

RTAI_PROTO(void, rt_sched_unlock, (void))
{
	struct { int dummy; } arg;
	rtai_lxrt(BIDX, SIZARG, SCHED_UNLOCK, &arg);
}

RTAI_PROTO(void, rt_pend_linux_irq, (unsigned irq))
{
	struct { unsigned irq; } arg = { irq };
	rtai_lxrt(BIDX, SIZARG, PEND_LINUX_IRQ, &arg);
}

RTAI_PROTO(int, rt_irq_wait, (unsigned irq))
{
	struct { unsigned irq; } arg = { irq };
	return rtai_lxrt(BIDX, SIZARG, IRQ_WAIT, &arg).i[LOW];
}

RTAI_PROTO(int, rt_irq_wait_if, (unsigned irq))
{
	struct { unsigned irq; } arg = { irq };
	return rtai_lxrt(BIDX, SIZARG, IRQ_WAIT_IF, &arg).i[LOW];
}

RTAI_PROTO(int, rt_irq_wait_until, (unsigned irq, RTIME time))
{
	struct { unsigned irq; RTIME time; } arg = { irq, time };
	return rtai_lxrt(BIDX, SIZARG, IRQ_WAIT_UNTIL, &arg).i[LOW];
}

RTAI_PROTO(int, rt_irq_wait_timed, (unsigned irq, RTIME delay))
{
	struct { unsigned irq; RTIME delay; } arg = { irq, delay };
	return rtai_lxrt(BIDX, SIZARG, IRQ_WAIT_TIMED, &arg).i[LOW];
}

RTAI_PROTO(int, rt_irq_signal, (unsigned irq))
{
	struct { unsigned irq; } arg = { irq };
	return rtai_lxrt(BIDX, SIZARG, IRQ_SIGNAL, &arg).i[LOW];
}

RTAI_PROTO(int, rt_request_irq_task, (unsigned irq, void *handler, int type, int affine2task))
{
	struct { unsigned irq; void *handler; long type, affine2task; } arg = { irq, handler, type, affine2task };
	return rtai_lxrt(BIDX, SIZARG, REQUEST_IRQ_TASK, &arg).i[LOW];
}


RTAI_PROTO(int, rt_release_irq_task, (unsigned irq))
{
	struct { unsigned irq; } arg = { irq };
	return rtai_lxrt(BIDX, SIZARG, RELEASE_IRQ_TASK, &arg).i[LOW];
}

RTAI_PROTO(int, rt_task_make_periodic,(RT_TASK *task, RTIME start_time, RTIME period))
{
	struct { RT_TASK *task; RTIME start_time, period; } arg = { task, start_time, period };
	return rtai_lxrt(BIDX, SIZARG, MAKE_PERIODIC, &arg).i[LOW];
}

RTAI_PROTO(int,rt_task_make_periodic_relative_ns,(RT_TASK *task, RTIME start_delay, RTIME period))
{
	struct { RT_TASK *task; RTIME start_time, period; } arg = { task, start_delay, period };
	return rtai_lxrt(BIDX, SIZARG, MAKE_PERIODIC_NS, &arg).i[LOW];
}

RTAI_PROTO(int,rt_task_wait_period,(void))
{
	struct { unsigned long dummy; } arg;
	return rtai_lxrt(BIDX, SIZARG, WAIT_PERIOD, &arg).i[LOW];
}

RTAI_PROTO(int,rt_sleep,(RTIME delay))
{
	struct { RTIME delay; } arg = { delay };
	return rtai_lxrt(BIDX, SIZARG, SLEEP, &arg).i[LOW];
}

RTAI_PROTO(int,rt_sleep_until,(RTIME time))
{
	struct { RTIME time; } arg = { time };
	return rtai_lxrt(BIDX, SIZARG, SLEEP_UNTIL, &arg).i[LOW];
}

RTAI_PROTO(int,rt_is_hard_timer_running,(void))
{
	struct { unsigned long dummy; } arg;
	return rtai_lxrt(BIDX, SIZARG, HARD_TIMER_RUNNING, &arg).i[LOW];
}

RTAI_PROTO(RTIME, start_rt_timer,(int period))
{
	struct { long period; } arg = { period };
	return rtai_lxrt(BIDX, SIZARG, START_TIMER, &arg).rt;
}

RTAI_PROTO(void, stop_rt_timer,(void))
{
	struct { unsigned long dummy; } arg;
	rtai_lxrt(BIDX, SIZARG, STOP_TIMER, &arg);
}

RTAI_PROTO(RTIME,rt_get_time,(void))
{
	struct { unsigned long dummy; } arg;
	return rtai_lxrt(BIDX, SIZARG, GET_TIME, &arg).rt;
}

RTAI_PROTO(RTIME,count2nano,(RTIME count))
{
	struct { RTIME count; } arg = { count };
	return rtai_lxrt(BIDX, SIZARG, COUNT2NANO, &arg).rt;
}

RTAI_PROTO(RTIME,nano2count,(RTIME nanos))
{
	struct { RTIME nanos; } arg = { nanos };
	return rtai_lxrt(BIDX, SIZARG, NANO2COUNT, &arg).rt;
}

RTAI_PROTO(void,rt_busy_sleep,(int ns))
{
	struct { long ns; } arg = { ns };
	rtai_lxrt(BIDX, SIZARG, BUSY_SLEEP, &arg);
}

RTAI_PROTO(void,rt_set_periodic_mode,(void))
{
	struct { unsigned long dummy; } arg;
	rtai_lxrt(BIDX, SIZARG, SET_PERIODIC_MODE, &arg);
}

RTAI_PROTO(void,rt_set_oneshot_mode,(void))
{
	struct { unsigned long dummy; } arg;
	rtai_lxrt(BIDX, SIZARG, SET_ONESHOT_MODE, &arg);
}

RTAI_PROTO(int,rt_task_signal_handler,(RT_TASK *task, void (*handler)(void)))
{
	struct { RT_TASK *task; void (*handler)(void); } arg = { task, handler };
	return rtai_lxrt(BIDX, SIZARG, SIGNAL_HANDLER, &arg).i[LOW];
}

RTAI_PROTO(int,rt_task_use_fpu,(RT_TASK *task, int use_fpu_flag))
{
        struct { RT_TASK *task; long use_fpu_flag; } arg = { task, use_fpu_flag };
        if (rtai_lxrt(BIDX, SIZARG, RT_BUDDY, &arg).v[LOW] != task) {
                return rtai_lxrt(BIDX, SIZARG, TASK_USE_FPU, &arg).i[LOW];
        } else {
// note that it would be enough to do whatever FP op here to have it OK. But
// that is scary if it is done when already in hard real time, and we do not
// want to force users to call this before making it hard.
                rtai_lxrt(BIDX, SIZARG, HRT_USE_FPU, &arg);
                return 0;
        }
}

RTAI_PROTO(int,rt_buddy_task_use_fpu,(RT_TASK *task, int use_fpu_flag))
{
	struct { RT_TASK *task; long use_fpu_flag; } arg = { task, use_fpu_flag };
	return rtai_lxrt(BIDX, SIZARG, TASK_USE_FPU, &arg).i[LOW];
}

RTAI_PROTO(int,rt_linux_use_fpu,(int use_fpu_flag))
{
	struct { long use_fpu_flag; } arg = { use_fpu_flag };
	return rtai_lxrt(BIDX, SIZARG, LINUX_USE_FPU, &arg).i[LOW];
}

RTAI_PROTO(void,rt_preempt_always,(int yes_no))
{
	struct { long yes_no; } arg = { yes_no };
	rtai_lxrt(BIDX, SIZARG, PREEMPT_ALWAYS_GEN, &arg);
}

RTAI_PROTO(RTIME,rt_get_time_ns,(void))
{
	struct { unsigned long dummy; } arg;
	return rtai_lxrt(BIDX, SIZARG, GET_TIME_NS, &arg).rt;
}

RTAI_PROTO(RTIME,rt_get_cpu_time_ns,(void))
{
	struct { unsigned long dummy; } arg;
	return rtai_lxrt(BIDX, SIZARG, GET_CPU_TIME_NS, &arg).rt;
}

#define rt_named_task_init(task_name, thread, data, stack_size, prio, uses_fpu, signal) \
	rt_task_init(nam2num(task_name), thread, data, stack_size, prio, uses_fpu, signal)

#define rt_named_task_init_cpuid(task_name, thread, data, stack_size, prio, uses_fpu, signal, run_on_cpu) \
	rt_task_init_cpuid(nam2num(task_name), thread, data, stack_size, prio, uses_fpu, signal, run_on_cpu)

RTAI_PROTO(void,rt_set_runnable_on_cpus,(RT_TASK *task, unsigned long cpu_mask))
{
	struct { RT_TASK *task; unsigned long cpu_mask; } arg = { task, cpu_mask };
	rtai_lxrt(BIDX, SIZARG, SET_RUNNABLE_ON_CPUS, &arg);
}

RTAI_PROTO(void,rt_set_runnable_on_cpuid,(RT_TASK *task, unsigned int cpuid))
{
	struct { RT_TASK *task; unsigned long cpuid; } arg = { task, cpuid };
	rtai_lxrt(BIDX, SIZARG, SET_RUNNABLE_ON_CPUID, &arg);
}

RTAI_PROTO(int,rt_get_timer_cpu,(void))
{
	struct { unsigned long dummy; } arg;
	return rtai_lxrt(BIDX, SIZARG, GET_TIMER_CPU, &arg).i[LOW];
}

RTAI_PROTO(void,start_rt_apic_timers,(struct apic_timer_setup_data *setup_mode, unsigned int rcvr_jiffies_cpuid))
{
	struct { struct apic_timer_setup_data *setup_mode; unsigned long rcvr_jiffies_cpuid; } arg = { setup_mode, rcvr_jiffies_cpuid };
	rtai_lxrt(BIDX, SIZARG, START_RT_APIC_TIMERS, &arg);
}

RTAI_PROTO(void,rt_preempt_always_cpuid,(int yes_no, unsigned int cpuid))
{
	struct { long yes_no; unsigned long cpuid; } arg = { yes_no, cpuid };
	rtai_lxrt(BIDX, SIZARG, PREEMPT_ALWAYS_CPUID, &arg);
}

RTAI_PROTO(RTIME,count2nano_cpuid,(RTIME count, unsigned int cpuid))
{
	struct { RTIME count; unsigned long cpuid; } arg = { count, cpuid };
	return rtai_lxrt(BIDX, SIZARG, COUNT2NANO_CPUID, &arg).rt;
}

RTAI_PROTO(RTIME,nano2count_cpuid,(RTIME nanos, unsigned int cpuid))
{
	struct { RTIME nanos; unsigned long cpuid; } arg = { nanos, cpuid };
	return rtai_lxrt(BIDX, SIZARG, NANO2COUNT_CPUID, &arg).rt;
}

RTAI_PROTO(RTIME,rt_get_time_cpuid,(unsigned int cpuid))
{
	struct { unsigned long cpuid; } arg = { cpuid };
	return rtai_lxrt(BIDX, SIZARG, GET_TIME_CPUID, &arg).rt;
}

RTAI_PROTO(RTIME,rt_get_time_ns_cpuid,(unsigned int cpuid))
{
	struct { unsigned long cpuid; } arg = { cpuid };
	return rtai_lxrt(BIDX, SIZARG, GET_TIME_NS_CPUID, &arg).rt;
}

RTAI_PROTO(void,rt_boom,(void))
{
	struct { int dummy; } arg = { 0 };
	rtai_lxrt(BIDX, SIZARG, RT_BOOM, &arg);
}

RTAI_PROTO(void,rt_mmgr_stats,(void))
{
	struct { int dummy; } arg = { 0 };
	rtai_lxrt(BIDX, SIZARG, RT_MMGR_STATS, &arg);
}

RTAI_PROTO(void,rt_stomp,(void) )
{
	struct { int dummy; } arg = { 0 };
	rtai_lxrt(BIDX, SIZARG, RT_STOMP, &arg);
}

RTAI_PROTO(int,rt_get_linux_signal,(RT_TASK *task))
{
    struct { RT_TASK *task; } arg = { task };
    return rtai_lxrt(BIDX, SIZARG, RT_GET_LINUX_SIGNAL, &arg).i[LOW];
}

RTAI_PROTO(int,rt_get_errno,(RT_TASK *task))
{
    struct { RT_TASK *task; } arg = { task };
    return rtai_lxrt(BIDX, SIZARG, RT_GET_ERRNO, &arg).i[LOW];
}

RTAI_PROTO(int,rt_set_linux_signal_handler,(RT_TASK *task, void (*handler)(int sig)))
{
    struct { RT_TASK *task; void (*handler)(int sig); } arg = { task, handler };
    return rtai_lxrt(BIDX, SIZARG, RT_SET_LINUX_SIGNAL_HANDLER, &arg).i[LOW];
}

RTAI_PROTO(int,rtai_print_to_screen,(const char *format, ...))
{
	char display[256];
	struct { const char *display; long nch; } arg = { display, 0 };
	va_list args;

	va_start(args, format);
	arg.nch = vsprintf(display, format, args);
	va_end(args);
	rtai_lxrt(BIDX, SIZARG, PRINT_TO_SCREEN, &arg);
	return arg.nch;
}

RTAI_PROTO(int,rt_printk,(const char *format, ...))
{
	char display[256];
	struct { const char *display; long nch; } arg = { display, 0 };
	va_list args;

	va_start(args, format);
	arg.nch = vsprintf(display, format, args);
	va_end(args);
	rtai_lxrt(BIDX, SIZARG, PRINTK, &arg);
	return arg.nch;
}

RTAI_PROTO(int,rt_usp_signal_handler,(void (*handler)(void)))
{
	struct { void (*handler)(void); } arg = { handler };
	return rtai_lxrt(BIDX, SIZARG, USP_SIGHDL, &arg).i[0];
}

RTAI_PROTO(unsigned long,rt_get_usp_flags,(RT_TASK *rt_task))
{
	struct { RT_TASK *task; } arg = { rt_task };
	return rtai_lxrt(BIDX, SIZARG, GET_USP_FLAGS, &arg).i[LOW];
}

RTAI_PROTO(unsigned long,rt_get_usp_flags_mask,(RT_TASK *rt_task))
{
	struct { RT_TASK *task; } arg = { rt_task };
	return rtai_lxrt(BIDX, SIZARG, GET_USP_FLG_MSK, &arg).i[LOW];
}

RTAI_PROTO(void,rt_set_usp_flags,(RT_TASK *rt_task, unsigned long flags))
{
	struct { RT_TASK *task; unsigned long flags; } arg = { rt_task, flags };
	rtai_lxrt(BIDX, SIZARG, SET_USP_FLAGS, &arg);
}

RTAI_PROTO(void,rt_set_usp_flags_mask,(unsigned long flags_mask))
{
	struct { unsigned long flags_mask; } arg = { flags_mask };
	rtai_lxrt(BIDX, SIZARG, SET_USP_FLG_MSK, &arg);
}

RTAI_PROTO(RT_TASK *,rt_force_task_soft,(int pid))
{
	struct { long pid; } arg = { pid };
	return (RT_TASK *)rtai_lxrt(BIDX, SIZARG, FORCE_TASK_SOFT, &arg).v[LOW];
}

RTAI_PROTO(RT_TASK *,rt_agent,(void))
{
	struct { unsigned long dummy; } arg;
	return (RT_TASK *)rtai_lxrt(BIDX, SIZARG, RT_BUDDY, &arg).v[LOW];
}

#define rt_buddy() rt_agent()

/**
 * Give a Linux process, or pthread, hard real time execution capabilities 
 * allowing full kernel preemption.
 *
 * rt_make_hard_real_time makes the soft Linux POSIX real time process, from
 * which it is called, a hard real time LXRT process.   It is important to
 * remark that this function must be used only with soft Linux POSIX processes
 * having their memory locked in memory.   See Linux man pages.
 *
 * Only the process itself can use this functions, it is not possible to impose
 * the related transition from another process.
 *
 * Note that processes made hard real time should avoid making any Linux System
 * call that can lead to a task switch as Linux cannot run anymore processes
 * that are made hard real time.   To interact with Linux you should couple the
 * process that was made hard real time with a Linux buddy server, either
 * standard or POSIX soft real time.   To communicate and synchronize with the
 * buddy you can use the wealth of available RTAI, and its schedulers, services.
 * 
 * After all it is pure nonsense to use a non hard real time Operating System,
 * i.e. Linux, from within hard real time processes.
 */
RTAI_PROTO(void,rt_make_hard_real_time,(void))
{
	struct { unsigned long dummy; } arg;
	rtai_lxrt(BIDX, SIZARG, MAKE_HARD_RT, &arg);
}

/**
 * Allows a non root user to use the Linux POSIX soft real time process 
 * management and memory lock functions, and allows it to do any input-output
 * operation from user space.
 *
 * Only the process itself can use this functions, it is not possible to impose
 * the related transition from another process.
 */
RTAI_PROTO(void,rt_allow_nonroot_hrt,(void))
{
	struct { unsigned long dummy; } arg;
	rtai_lxrt(BIDX, SIZARG, NONROOT_HRT, &arg);
}

RTAI_PROTO(int,rt_is_hard_real_time,(RT_TASK *rt_task))
{
	struct { RT_TASK *task; } arg = { rt_task };
	return rtai_lxrt(BIDX, SIZARG, IS_HARD, &arg).i[LOW];
}

#define rt_is_soft_real_time(rt_task) (!rt_is_hard_real_time((rt_task)))

RTAI_PROTO(void,rt_task_set_resume_end_times,(RTIME resume, RTIME end))
{
	struct { RTIME resume, end; } arg = { resume, end };
	rtai_lxrt(BIDX, SIZARG, SET_RESUME_END, &arg);
}

RTAI_PROTO(int,rt_set_resume_time,(RT_TASK *rt_task, RTIME new_resume_time))
{
	struct { RT_TASK *rt_task; RTIME new_resume_time; } arg = { rt_task, new_resume_time };
	return rtai_lxrt(BIDX, SIZARG, SET_RESUME_TIME, &arg).i[LOW];
}

RTAI_PROTO(int,rt_set_period,(RT_TASK *rt_task, RTIME new_period))
{
	struct { RT_TASK *rt_task; RTIME new_period; } arg = { rt_task, new_period };
	return rtai_lxrt(BIDX, SIZARG, SET_PERIOD, &arg).i[LOW];
}

RTAI_PROTO(void,rt_spv_RMS,(int cpuid))
{
	struct { long cpuid; } arg = { cpuid };
	rtai_lxrt(BIDX, SIZARG, SPV_RMS, &arg);
}

RTAI_PROTO(int, rt_task_masked_unblock,(RT_TASK *task, unsigned long mask))
{
	struct { RT_TASK *task; unsigned long mask; } arg = { task, mask };
	return rtai_lxrt(BIDX, SIZARG, WAKEUP_SLEEPING, &arg).i[LOW];
}

#define rt_task_wakeup_sleeping(task, mask)  rt_task_masked_unblock(task, RT_SCHED_DELAYED)

RTAI_PROTO(void,rt_get_exectime,(RT_TASK *task, RTIME *exectime))
{
	RTIME lexectime[] = { 0LL, 0LL, 0LL };
	struct { RT_TASK *task; RTIME *lexectime; } arg = { task, lexectime };
	rtai_lxrt(BIDX, SIZARG, GET_EXECTIME, &arg);
	memcpy(exectime, lexectime, sizeof(lexectime));
}

RTAI_PROTO(void,rt_gettimeorig,(RTIME time_orig[]))
{
	struct { RTIME *time_orig; } arg = { time_orig };
	rtai_lxrt(BIDX, SIZARG, GET_TIMEORIG, &arg);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __KERNEL__ */

/*@}*/

#endif /* !_RTAI_LXRT_H */
