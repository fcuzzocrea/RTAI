/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * RTAI/fusion is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RTAI/fusion; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _RTAI_ASM_UVM_SYSTEM_H
#define _RTAI_ASM_UVM_SYSTEM_H

#include <sys/time.h>
#include <errno.h>
#include <malloc.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <limits.h>
#include <setjmp.h>
#include <rtai_config.h>
#include <nucleus/asm/atomic.h>
#include <nucleus/fusion.h>

/* Module arg macros */
#define vartype(var)               var ## _ ## tYpE
#define MODULE_DESCRIPTION(s);
#define MODULE_LICENSE(s);
#define MODULE_AUTHOR(s);
#define MODULE_PARM(var,type)      static const char *vartype(var) = type
#define MODULE_PARM_DESC(var,desc);
#define MODULE_PARM_VALUE(var)     (xnarch_read_environ(#var,&vartype(var),&var),var)

/* Nullify other kernel macros */
#define EXPORT_SYMBOL(sym);
#define module_init(sym);
#define module_exit(sym);

typedef int spl_t;

typedef unsigned long cpumask_t;

#ifdef CONFIG_SMP
#error "SMP not supported for UVM yet"
#endif /* CONFIG_SMP */

extern int vml_irqlock;

#define splhigh(x)    ((x) = xnarch_lock_irq())
#define splexit(x)    xnarch_unlock_irq(x)
#define splnone()     xnarch_unlock_irq(0)

typedef unsigned long xnlock_t;

#define XNARCH_LOCK_UNLOCKED 0

#define xnlock_init(lock)              do { } while(0)
#define xnlock_get_irqsave(lock,x)     ((x) = xnarch_lock_irq())
#define xnlock_put_irqrestore(lock,x)  xnarch_unlock_irq(x)
#define xnlock_clear_irqoff(lock)      xnarch_lock_irq()
#define xnlock_clear_irqon(lock)       xnarch_unlock_irq(0)

#define XNARCH_NR_CPUS             1

#define XNARCH_DEFAULT_TICK        1000000 /* ns, i.e. 1ms */
#define XNARCH_SIG_RESTART         SIGUSR1
#define XNARCH_HOST_TICK           0	/* No host ticking service */

#define XNARCH_THREAD_STACKSZ 0 /* Use the default POSIX value. */
#define XNARCH_ROOT_STACKSZ   0	/* Only a placeholder -- no stack */

#define XNARCH_PROMPT "RTAI[nucleus/UVM]: "
#define xnarch_loginfo(fmt,args...)  fprintf(stdout, XNARCH_PROMPT fmt , ##args)
#define xnarch_logwarn(fmt,args...)  fprintf(stderr, XNARCH_PROMPT fmt , ##args)
#define xnarch_logerr(fmt,args...)   fprintf(stderr, XNARCH_PROMPT fmt , ##args)
#define xnarch_printf(fmt,args...)   fprintf(stdout, fmt, ##args)
#define printk(fmt,args...)          xnarch_loginfo(fmt, ##args)

typedef unsigned long xnarch_cpumask_t;
#define xnarch_num_online_cpus()         XNARCH_NR_CPUS
#define xnarch_cpu_online_map            ((1<<xnarch_num_online_cpus()) - 1)
#define xnarch_cpu_set(cpu, mask)        ((mask) |= 1 << (cpu))
#define xnarch_cpu_clear(cpu, mask)      ((mask) &= 1 << (cpu))
#define xnarch_cpus_clear(mask)          ((mask) = 0UL)
#define xnarch_cpu_isset(cpu, mask)      (!!((mask) & (1 << (cpu))))
#define xnarch_cpus_and(dst, src1, src2) ((dst) = (src1) & (src2))
#define xnarch_cpus_equal(mask1, mask2)  ((mask1) == (mask2))
#define xnarch_cpus_empty(mask)          ((mask) == 0UL)
#define xnarch_cpumask_of_cpu(cpu)       (1 << (cpu))
#define xnarch_first_cpu(mask)           (ffnz(mask))
#define XNARCH_CPU_MASK_ALL              (~0UL)

#define xnarch_ullmod(ull,uld,rem)   ((*rem) = ((ull) % (uld)))
#define xnarch_uldivrem(ull,uld,rem) ((u_long)xnarch_ulldiv((ull),(uld),(rem)))
#define xnarch_uldiv(ull, d)         xnarch_uldivrem(ull, d, NULL)
#define xnarch_ulmod(ull, d)         ({ u_long _rem;                    \
                                        rthal_uldivrem(ull,uld,&_rem); _rem; })

static inline int xnarch_imuldiv(int i, int mult, int div)
{
    unsigned long long ull = (unsigned long long) (unsigned) i * (unsigned) mult;
    return ull / (unsigned) div;
}

static inline unsigned long long __xnarch_ullimd(unsigned long long ull,
                                                 u_long m,
                                                 u_long d) {

    unsigned long long mh, ml;
    u_long h, l, mlh, mll, qh, r, ql;

    h = ull >> 32; l = ull & 0xffffffff; /* Split ull. */
    mh = (unsigned long long) h * m;
    ml = (unsigned long long) l * m;
    mlh = ml >> 32; mll = ml & 0xffffffff; /* Split ml. */
    mh += mlh;
    qh = mh / d;
    r = mh % d;
    ml = (((unsigned long long) r) << 32) + mll; /* assemble r and mll */
    ql = ml / d;

    return (((unsigned long long) qh) << 32) + ql;
}

static inline long long xnarch_llimd(long long ll, u_long m, u_long d) {
    if(ll < 0)
        return -__xnarch_ullimd(-ll, m, d);
    return __xnarch_ullimd(ll, m, d);
}

static inline unsigned long long xnarch_ullmul(unsigned long m1,
                                               unsigned long m2) {
    return (unsigned long long) m1 * m2;
}

static inline unsigned long long xnarch_ulldiv (unsigned long long ull,
						unsigned long uld,
						unsigned long *rem)
{
    if (rem)
	*rem = ull % uld;

    return ull / uld;
}

static inline unsigned long ffnz (unsigned long word) {
    return ffs((int)word) - 1;
}

#define xnarch_stack_size(tcb)     ((tcb)->stacksize)
#define xnarch_fpu_ptr(tcb)        (NULL)

struct xnthread;

typedef struct xnarchtcb {	/* Per-thread arch-dependent block */

    const char *name;		/* Symbolic name of thread (can be NULL) */
    struct xnthread *thread;	/* VM thread pointer (opaque) */
    void *khandle;		/* Kernel handle (opaque) */
    void (*entry)(void *);	/* Thread entry */
    void *cookie;		/* Thread cookie passed on entry */
    int imask;			/* Initial interrupt mask */
    jmp_buf rstenv;             /* Restart context info */
    pid_t ppid;
    int syncflag;
    pthread_t thid;

    /* The following fields are not used by the Fusion skin, however
       they are set by the nucleus. */
    unsigned stacksize;		/* Aligned size of stack (bytes) */
    unsigned long *stackbase;	/* Stack space */

} xnarchtcb_t;

extern xnarchtcb_t *vml_root;

extern xnarchtcb_t *vml_current;

typedef void *xnarch_fltinfo_t;	/* Unused but required */

#define xnarch_fault_trap(fi)  (0)
#define xnarch_fault_code(fi)  (0)
#define xnarch_fault_pc(fi)    (0L)

typedef struct xnarch_heapcb {

#if (__GNUC__ <= 2)
    int old_gcc_dislikes_emptiness;
#endif

} xnarch_heapcb_t;

static inline void xnarch_init_heapcb (xnarch_heapcb_t *cb) {
}

static inline int __attribute__ ((unused))
xnarch_read_environ (const char *name, const char **ptype, void *pvar)

{
    char *value;

    if (*ptype == NULL)
	return 0;	/* Already read in */

    *ptype = NULL;

    value = getenv(name);

    if (!value)
	return -1;

    if (**ptype == 's')
	*((char **)pvar) = value;
    else
	*((int *)pvar) = atoi(value);

    return 1;
}

static int inline xnarch_lock_irq (void) {

    return xnarch_atomic_xchg(&vml_irqlock,1);
}

static inline void xnarch_unlock_irq (int x) {

    extern int vml_irqpend;

    if (!x && vml_irqlock)
	{
	if (xnarch_atomic_xchg(&vml_irqpend,0))
	    __pthread_release_vm(&vml_irqlock);
	else
	    vml_irqlock = 0;
	}
}

void xnarch_sync_irq(void);

int xnarch_setimask(int imask);

#ifdef __cplusplus
extern "C" {
#endif

void xnpod_welcome_thread(struct xnthread *);

#ifdef XENO_INTR_MODULE

int vml_irqlock = 0;

int vml_irqpend = 0;

void xnarch_sync_irq (void)

{
    if (vml_irqlock)
	__pthread_hold_vm(&vml_irqpend);
}

static inline int xnarch_hook_irq (unsigned irq,
				   void (*handler)(unsigned irq,
						   void *cookie),
				   void *cookie) {
    if (irq == 0)
	return -EINVAL;	/* Reserved for the timer thread. */

    return -ENOSYS;
}

static inline int xnarch_release_irq (unsigned irq) {
    return -ENOSYS;
}

static inline int xnarch_enable_irq (unsigned irq) {
    return -ENOSYS;
}

static inline int xnarch_disable_irq (unsigned irq) {
    return -ENOSYS;
}

static inline void xnarch_isr_chain_irq (unsigned irq) {
    /* Nop */
}

static inline void xnarch_isr_enable_irq (unsigned irq) {
    /* Nop */
}

static inline unsigned long xnarch_set_irq_affinity (unsigned irq,
						     unsigned long affinity) {
    return 0;
}

#define xnarch_relay_tick()  /* Nullified. */

#endif /* XENO_INTR_MODULE */

#ifdef XENO_MAIN_MODULE

int __xeno_main_init(void);

void __xeno_main_exit(void);

int __xeno_skin_init(void);

void __xeno_skin_exit(void);

int __xeno_user_init(void);

void __xeno_user_exit(void);

int vml_done = 0;

static inline int xnarch_init (void) {
    return 0;
}

static inline void xnarch_exit (void) {
}

static void xnarch_restart_handler (int sig) {

    longjmp(vml_current->rstenv,1);
}

void xnarch_exit_handler (int sig)

{
    vml_done = 1;
    __pthread_activate_vm(vml_root->khandle,vml_current->khandle);
    exit(99);
}

int main (int argc, char *argv[])

{
    struct sigaction sa;
    int err;

    if (geteuid() !=0)
	{
        fprintf(stderr,"This program must be run with root privileges");
	exit(1);
	}

    err = __xeno_main_init();

    if (err)
	{
        fprintf(stderr,"main_init() failed, err=%x\n",err);
        exit(2);
	}

    err = __xeno_skin_init();

    if (err)
	{
        fprintf(stderr,"skin_init() failed, err=%x\n",err);
        exit(3);
	}

    err = __xeno_user_init();

    if (err)
	{
        fprintf(stderr,"user_init() failed, err=%x\n",err);
        exit(4);
	}

    sa.sa_handler = &xnarch_restart_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(XNARCH_SIG_RESTART,&sa,NULL);

    sa.sa_handler = &xnarch_exit_handler;
    sa.sa_flags = 0;
    sigaction(SIGTERM,&sa,NULL);
    sigaction(SIGHUP,&sa,NULL);
    sigaction(SIGINT,&sa,NULL);

    while (!vml_done)
	__pthread_idle_vm(&vml_irqlock);

    __xeno_user_exit();
    __xeno_skin_exit();
    __xeno_main_exit();

    exit(0);
}

#endif  /* !XENO_MAIN_MODULE */

#ifdef XENO_TIMER_MODULE

void *vml_timer_handle;

static inline void xnarch_program_timer_shot (unsigned long long delay) {
    /* Empty -- not available */
}

static inline void xnarch_stop_timer (void) {
    __pthread_cancel_vm(vml_timer_handle,NULL);
}

static inline int xnarch_send_timer_ipi (xnarch_cpumask_t mask) {

    return 0;
}

static inline void xnarch_read_timings (unsigned long long *shot,
					unsigned long long *delivery,
					unsigned long long defval)
{
    *shot = defval;
    *delivery = defval;
}

#endif /* XENO_TIMER_MODULE */

#ifdef XENO_POD_MODULE

extern void *vml_timer_handle;

xnsysinfo_t vml_info;

xnarchtcb_t *vml_root;

xnarchtcb_t *vml_current;

/* NOTES:

   o IRQ threads have no TCB, since they are not known by the VM
   abstraction.

   o All in-kernel IRQ threads serving the VM have the same priority
   level, except when they wait on a synchonization barrier.

   o In theory, the IRQ synchronization mechanism might end up
   causing interrupt loss, since we might be sleeping to much waiting
   on the irqlock barrier until it is signaled. In practice, this
   should not happen because interrupt-free sections are short.
*/

struct xnarch_tick_parms {
    time_t sec;
    long nsec;
    void (*tickhandler)(void);
    pid_t ppid;
    int syncflag;
};

static void *xnarch_timer_thread (void *cookie)

{
    struct xnarch_tick_parms *p = (struct xnarch_tick_parms *)cookie;
    void (*tickhandler)(void) = p->tickhandler;
    struct timespec ts;

    pthread_create_rt("vmtimer",NULL,p->ppid,&p->syncflag,&vml_timer_handle);
    pthread_barrier_rt();

    ts.tv_sec = p->sec;
    ts.tv_nsec = p->nsec;

    for (;;)
	{
	nanosleep(&ts,NULL);
	xnarch_sync_irq();
	tickhandler(); /* Should end up in xnintr_clock_handler() here. */
	}

    return NULL;
}

static inline int xnarch_start_timer (unsigned long nstick,
				      void (*tickhandler)(void))
{
    struct xnarch_tick_parms parms;
    struct sched_param param;
    unsigned long tickval;
    pthread_attr_t thattr;
    pthread_t thid;

    if (nstick == 0)
	/* Cannot do aperiodic timing in UVMs */
	return -ENODEV;

    pthread_attr_init(&thattr);
    pthread_attr_setdetachstate(&thattr,PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedpolicy(&thattr,SCHED_FIFO);
    param.sched_priority = sched_get_priority_min(SCHED_FIFO) + 2;
    pthread_attr_setschedparam(&thattr,&param);

    if (vml_info.tickval > nstick)
	{
	fprintf(stderr,"UVM: warning: VM tick freq > nucleus tick freq\n");
	fprintf(stderr,"   : rounding VM tick to %lu us\n",vml_info.tickval / 1000);
	tickval = vml_info.tickval;
	}
    else
	{
	tickval = ((nstick + vml_info.tickval - 1) / vml_info.tickval) * vml_info.tickval;

	if (tickval != nstick)
	    {
	    fprintf(stderr,"UVM: warning: VM tick not a multiple of nucleus tick\n");
	    fprintf(stderr,"   : rounding VM tick to %lu us\n",tickval / 1000);
	    }
	}

    parms.sec = tickval / 1000000000;
    parms.nsec = tickval % 1000000000;
    parms.tickhandler = tickhandler;
    parms.syncflag = 0;
    parms.ppid = getpid();

    pthread_create(&thid,&thattr,&xnarch_timer_thread,&parms);
    pthread_sync_rt(&parms.syncflag);
    pthread_start_rt(vml_timer_handle);

    return 0;
}

static inline void xnarch_leave_root(xnarchtcb_t *rootcb) {
}

static inline void xnarch_enter_root(xnarchtcb_t *rootcb) {
}

static inline void xnarch_switch_to (xnarchtcb_t *out_tcb,
				     xnarchtcb_t *in_tcb) {
    vml_current = in_tcb;
    __pthread_activate_vm(in_tcb->khandle,out_tcb->khandle);
}

static inline void xnarch_finalize_and_switch (xnarchtcb_t *dead_tcb,
					       xnarchtcb_t *next_tcb) {
    vml_current = next_tcb;
    __pthread_cancel_vm(dead_tcb->khandle,next_tcb->khandle);
}

static inline void xnarch_finalize_no_switch (xnarchtcb_t *dead_tcb) {

    __pthread_cancel_vm(dead_tcb->khandle,NULL);
}

static inline void xnarch_init_root_tcb (xnarchtcb_t *tcb,
					 struct xnthread *thread,
					 const char *name)
{
    struct sched_param param;

    param.sched_priority = sched_get_priority_min(SCHED_FIFO);

    if (sched_setscheduler(0,SCHED_FIFO,&param) < 0 ||
	pthread_info_rt(&vml_info) < 0)
	{
	perror("UVM");
	exit(1);
	}

    pthread_init_rt("vmroot",tcb,&tcb->khandle);

    tcb->name = name;
    vml_root = vml_current = tcb;
}

static inline void xnarch_init_tcb (xnarchtcb_t *tcb) {

    tcb->khandle = NULL;
}

static void *xnarch_thread_trampoline (void *cookie)

{
    xnarchtcb_t *tcb = (xnarchtcb_t *)cookie;

    if (!setjmp(tcb->rstenv))
	{
	pthread_create_rt(tcb->name,tcb,tcb->ppid,&tcb->syncflag,&tcb->khandle);
	pthread_barrier_rt();	/* Wait for start. */
	}

    xnarch_setimask(tcb->imask);

    xnpod_welcome_thread(tcb->thread);

    tcb->entry(tcb->cookie);

    return NULL;
}

static inline void xnarch_init_thread (xnarchtcb_t *tcb,
				       void (*entry)(void *),
				       void *cookie,
				       int imask,
				       struct xnthread *thread,
				       char *name)
{
    struct sched_param param;
    pthread_attr_t thattr;

    if (tcb->khandle)	/* Restarting thread */
	{
        pthread_kill(tcb->thid,XNARCH_SIG_RESTART);
	return;
	}

    tcb->imask = imask;
    tcb->entry = entry;
    tcb->cookie = cookie;
    tcb->thread = thread;
    tcb->name = name;
    tcb->ppid = getpid();
    tcb->syncflag = 0;

    pthread_attr_init(&thattr);
    pthread_attr_setdetachstate(&thattr,PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedpolicy(&thattr,SCHED_FIFO);
    param.sched_priority = sched_get_priority_min(SCHED_FIFO);
    pthread_attr_setschedparam(&thattr,&param);
    pthread_create(&tcb->thid,&thattr,&xnarch_thread_trampoline,tcb);

    pthread_sync_rt(&tcb->syncflag);
    pthread_start_rt(&tcb->khandle);
}

static inline void xnarch_init_fpu(xnarchtcb_t *tcb) {
    /* Handled by the in-kernel nucleus */
}

static inline void xnarch_save_fpu(xnarchtcb_t *tcb) {
    /* Handled by the in-kernel nucleus */
}

static inline void xnarch_restore_fpu(xnarchtcb_t *tcb) {
    /* Handled by the in-kernel nucleus */
}

int xnarch_setimask (int imask)

{
    spl_t s;
    splhigh(s);
    splexit(!!imask);
    return !!s;
}

static inline int xnarch_send_ipi (cpumask_t cpumask) {

    return 0;
}

static inline int xnarch_hook_ipi (void (*handler)(void)) {

    return 0;
}

static inline int xnarch_release_ipi (void) {

    return 0;
}

int xnarch_sleep_on (int *flagp)

{
    while (!*flagp)
	{
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = 10000000;
#if !CONFIG_RTAI_OPT_DEBUG
	nanosleep(&ts,NULL);
#else /* CONFIG_RTAI_OPT_DEBUG. */
        if(nanosleep(&ts, NULL))
            return -errno;
#endif /* !CONFIG_RTAI_OPT_DEBUG. */
	}
    return 0;
}

static inline void xnarch_escalate (void) {

    void xnpod_schedule_handler(void);
    xnpod_schedule_handler();
}

#define xnarch_notify_ready()  /* Nullified */
#define xnarch_notify_shutdown() /* Nullified */
#define xnarch_notify_halt() /* Nullified */

#endif /* XENO_POD_MODULE */

extern xnsysinfo_t vml_info;

void xnarch_exit_handler(int);

static inline unsigned long long xnarch_tsc_to_ns (unsigned long long ts) {
    return ts;
}

static inline unsigned long long xnarch_ns_to_tsc (unsigned long long ns) {
    return ns;
}

static inline unsigned long long xnarch_get_cpu_time (void) {
    unsigned long long t;
    pthread_time_rt(&t);
    return t;
}

static inline unsigned long long xnarch_get_cpu_tsc (void) {
    return xnarch_get_cpu_time();
}

static inline unsigned long long xnarch_get_cpu_freq (void) {
    return vml_info.cpufreq;
}

static inline void xnarch_halt (const char *emsg) {
    fprintf(stderr,"UVM: fatal: %s\n",emsg);
    fflush(stderr);
    xnarch_exit_handler(SIGKILL);
    exit(99);
}

static inline void *xnarch_sysalloc (u_long bytes) {
    return malloc(bytes);
}

static inline void xnarch_sysfree (void *chunk, u_long bytes) {
    free(chunk);
}

#define xnarch_current_cpu()  0
#define xnarch_declare_cpuid  const int cpuid = 0
#define xnarch_get_cpu(x)     do  { (x) = (x); } while(0)
#define xnarch_put_cpu(x)     do { } while(0)

#define xnarch_alloc_stack xnmalloc
#define xnarch_free_stack  xnfree

#ifdef __cplusplus
}
#endif

/* Dashboard and graph control. */
#define XNARCH_DECL_DISPLAY_CONTEXT();
#define xnarch_init_display_context(obj)
#define xnarch_create_display(obj,name,tag)
#define xnarch_delete_display(obj)
#define xnarch_post_graph(obj,state)
#define xnarch_post_graph_if(obj,state,cond)

#endif /* !_RTAI_ASM_UVM_SYSTEM_H */
