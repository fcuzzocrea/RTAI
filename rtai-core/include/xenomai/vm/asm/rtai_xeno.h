/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * As a special exception, the RTAI project gives permission
 * for additional uses of the text contained in its release of
 * Xenomai.
 *
 * The exception is that, if you link the Xenomai libraries with other
 * files to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the Xenomai libraries code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public
 * License.
 *
 * This exception applies only to the code released by the
 * RTAI project under the name Xenomai.  If you copy code from other
 * RTAI project releases into a copy of Xenomai, as the General Public
 * License permits, the exception does not apply to the code that you
 * add in this way.  To avoid misleading anyone as to the status of
 * such modified files, you must delete this exception notice from
 * them.
 *
 * If you write modifications of your own for Xenomai, it is your
 * choice whether to permit this exception to apply to your
 * modifications. If you do not wish that, delete this exception
 * notice.
 */

#ifndef _VM_ASM_RTAI_XENO_H
#define _VM_ASM_RTAI_XENO_H

#include <sys/time.h>
#include <errno.h>
#include <malloc.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <setjmp.h>
#include <asm/rtai_xnatomic.h>
#include <xenomai/fusion.h>

#define XNARCH_DEFAULT_TICK   1000000 /* ns, i.e. 1ms */
#define XNARCH_IRQ_MAX        32
#define XNARCH_SIG_RESTART    SIGUSR1
#define XNARCH_HOST_TICK      0	/* No host ticking service */
#define XNARCH_APERIODIC_PREC 0	/* No aperiodic support */
#define XNARCH_SCHED_LATENCY  0 /* No scheduling latency */

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

extern int vml_irqlock;

typedef void *xnarch_fltinfo_t;	/* Unused but required */

#define xnarch_fault_trap(fi)  (0)
#define xnarch_fault_code(fi)  (0)
#define xnarch_fault_pc(fi)    (0L)

#define XNARCH_THREAD_COOKIE  NULL
#define XNARCH_THREAD_STACKSZ 0 /* Use the default POSIX value. */
#define XNARCH_ROOT_STACKSZ   0	/* Only a placeholder -- no stack */

#define xnarch_printf              printf
#define printk                     printf
#define xnarch_llimd(ll,m,d)       ((int)(ll) * (int)(m) / (int)(d))
#define xnarch_imuldiv(i,m,d)      ((int)(i) * (int)(m) / (int)(d))
#define xnarch_ulldiv(ull,uld,rem) (((*rem) = ((ull) % (uld))), (ull) / (uld))
#define xnarch_ullmod(ull,uld,rem) ((*rem) = ((ull) % (uld)))

#define xnarch_stack_size(tcb)     ((tcb)->stacksize)
#define xnarch_fpu_ptr(tcb)        (NULL)

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

    extern int vml_irqlock;
    return xnarch_atomic_xchg(&vml_irqlock,1);
}

static inline void xnarch_unlock_irq (int x) {

    extern int vml_irqlock, vml_irqpend;

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

typedef int spl_t;

#define splhigh(x)    ((x) = xnarch_lock_irq())
#define splexit(x)    xnarch_unlock_irq(x)
#define splnone()     xnarch_unlock_irq(0)

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

#ifdef XENO_HEAP_MODULE

void *xnarch_sysalloc (unsigned bytes) {
    return malloc(bytes);
}

void xnarch_sysfree (void *chunk, unsigned bytes) {
    free(chunk);
}

#else /* !XENO_HEAP_MODULE */

void *xnarch_sysalloc(unsigned bytes);

void xnarch_sysfree(void *chunk,
		    unsigned bytes);

#endif /* XENO_HEAP_MODULE */

#ifdef XENO_TIMER_MODULE

void *vml_timer_handle;

static inline void xnarch_stop_timer (void) {
    __pthread_cancel_vm(vml_timer_handle,NULL);
}

#endif /* XENO_TIMER_MODULE */

#ifdef XENO_POD_MODULE

extern void *vml_timer_handle;

xnsysinfo_t vml_info;

xnarchtcb_t *vml_root;

xnarchtcb_t *vml_current;

#define xnarch_relay_tick()  /* Nullified. */

/* NOTES:

   o IRQ threads have no TCB, since they are not known by the VM
   abstraction.

   o All in-kernel IRQ threads serving the VM have the same priority
   level, except when they wait on a synchonization barrier.

   o In theory, the IRQ synchronization mechanism might end up
   causing interrupt loss, since we might be sleeping to much waiting
   on the irqlock barrier until it is signaled. In practice, this
   should not happen because interrupt-free sections are short thanks
   to the mutex scheme.
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
    pthread_migrate_rt(FUSION_RTAI_DOMAIN);

    ts.tv_sec = p->sec;
    ts.tv_nsec = p->nsec;

    for (;;)
	{
	nanosleep(&ts,NULL);
	xnarch_sync_irq();
	tickhandler(); /* Should end up in xnpod_clock_irq() here. */
	}

    return NULL;
}

static inline void xnarch_start_timer (unsigned long nstick,
				       void (*tickhandler)(void))
{
    struct xnarch_tick_parms parms;
    struct sched_param param;
    unsigned long tickval;
    pthread_attr_t thattr;
    pthread_t thid;

    pthread_attr_init(&thattr);
    pthread_attr_setdetachstate(&thattr,PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedpolicy(&thattr,SCHED_FIFO);
    param.sched_priority = sched_get_priority_min(SCHED_FIFO) + 2;
    pthread_attr_setschedparam(&thattr,&param);

    if (vml_info.tickval > nstick)
	{
	fprintf(stderr,"Xenomai/VM: warning: VM tick freq > nucleus tick freq\n");
	fprintf(stderr,"          : rounding VM tick to %lu us\n",vml_info.tickval / 1000);
	tickval = vml_info.tickval;
	}
    else
	{
	tickval = ((nstick + vml_info.tickval - 1) / vml_info.tickval) * vml_info.tickval;

	if (tickval != nstick)
	    {
	    fprintf(stderr,"Xenomai/VM: warning: VM tick not a multiple of nucleus tick\n");
	    fprintf(stderr,"          : rounding VM tick to %lu us\n",tickval / 1000);
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

static inline void xnarch_save_fpu(xnarchtcb_t *tcb) {
    /* Handled by the in-kernel nucleus */
}

static inline void xnarch_restore_fpu(xnarchtcb_t *tcb) {
    /* Handled by the in-kernel nucleus */
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
	perror("Xenomai/VM");
	exit(1);
	}

    pthread_init_rt("vmroot",tcb,&tcb->khandle);
    pthread_migrate_rt(FUSION_RTAI_DOMAIN);

    tcb->name = name;
    vml_root = vml_current = tcb;
}

static inline void xnarch_init_tcb (xnarchtcb_t *tcb,
				    void *adcookie) { /* <= UNUSED */
    tcb->khandle = NULL;
}

static void *xnarch_thread_trampoline (void *cookie)

{
    xnarchtcb_t *tcb = (xnarchtcb_t *)cookie;

    if (!setjmp(tcb->rstenv))
	{
	/* After this, we are controlled by the in-kernel nucleus. */
	pthread_create_rt(tcb->name,tcb,tcb->ppid,&tcb->syncflag,&tcb->khandle);
	pthread_migrate_rt(FUSION_RTAI_DOMAIN);
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
}

static inline void xnarch_init_fpu(xnarchtcb_t *tcb) {
    /* Handled by the in-kernel nucleus */
}

int xnarch_setimask (int imask)

{
    spl_t s;
    splhigh(s);
    splexit(!!imask);
    return !!s;
}

#define xnarch_notify_ready()  /* Nullified */

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
    fprintf(stderr,"Xenomai/VM: fatal: %s\n",emsg);
    fflush(stderr);
    xnarch_exit_handler(SIGKILL);
    exit(99);
}

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

#endif /* !_VM_ASM_RTAI_XENO_H */
