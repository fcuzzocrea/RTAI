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
 * This file implements the interface between the Xenomai nucleus and
 * the Minute Virtual Machine.
 */

#ifndef _nslib_include_asm_rtai_xeno_h
#define _nslib_include_asm_rtai_xeno_h

#include <errno.h>
#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>

struct xnthread;
struct xnsynch;
struct XenoThread;
struct mvm_displayctx;
struct mvm_displayctl;
struct TclList;
typedef struct TclList *mvm_tcl_listobj_t;

typedef struct xnarchtcb {	/* Per-thread arch-dependent block */

    struct xnthread *kthread;	/* Kernel thread pointer (opaque) */
    struct XenoThread *vmthread;  /* Simulation thread pointer (opaque) */
    void (*entry)(void *);	/* Thread entry */
    void *cookie;		/* Thread cookie passed on entry */
    int imask;			/* Initial interrupt mask */
    /* The following fields are not used by the Minute VM, however
       they are set by the nucleus. */
    unsigned stacksize;		/* Aligned size of stack (bytes) */
    unsigned long *stackbase;	/* Stack space */

} xnarchtcb_t;

typedef void *xnarch_fltinfo_t;	/* Unused but required */

#define xnarch_fault_trap(fi)  (0)
#define xnarch_fault_code(fi)  (0)
#define xnarch_fault_pc(fi)    (0L)

typedef int spl_t;

#define splhigh(x)    ((x) = mvm_set_irqmask(-1))
#define splexit(x)    mvm_set_irqmask(x)
#define splnone(x)    mvm_set_irqmask(0)

#define XNARCH_DEFAULT_TICK       10000000 /* ns, i.e. 10ms */

#define XNARCH_THREAD_COOKIE NULL
#define XNARCH_THREAD_STACKSZ 0 /* Let the simulator choose. */
#define XNARCH_ROOT_STACKSZ   0	/* Only a placeholder -- no stack */

#define xnarch_printf              printf
#define printk                     printf
#define xnarch_llimd(ll,m,d)       ((int)(ll) * (int)(m) / (int)(d))
#define xnarch_imuldiv(i,m,d)      ((int)(i) * (int)(m) / (int)(d))
#define xnarch_ulldiv(ull,uld,rem) (((*rem) = ((ull) % (uld))), (ull) / (uld))
#define xnarch_ullmod(ull,uld,rem) ((*rem) = ((ull) % (uld)))
#define xnarch_stack_size(tcb)     ((tcb)->stacksize)

/* Under the MVM, preemption only occurs at the C-source line level,
   so we just need plain C bitops and counter support. */

typedef int atomic_counter_t;
typedef unsigned long atomic_flags_t;

#define xnarch_memory_barrier()
#define xnarch_atomic_set(pcounter,i)          (*(pcounter) = (i))
#define xnarch_atomic_get(pcounter)            (*(pcounter))
#define xnarch_atomic_inc(pcounter)            (++(*(pcounter)))
#define xnarch_atomic_dec(pcounter)            (--(*(pcounter)))
#define xnarch_atomic_inc_and_test(pcounter)   (!(++(*(pcounter))))
#define xnarch_atomic_dec_and_test(pcounter)   (!(--(*(pcounter))))
#define xnarch_atomic_set_mask(pflags,mask)    (*(pflags) |= (mask))
#define xnarch_atomic_clear_mask(pflags,mask)  (*(pflags) &= ~(mask))

#define __mvm_breakable(f) f ## $kdoor$

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

#define MAIN_INIT_MODULE     mvm_init_nanokernel
#define MAIN_CLEANUP_MODULE  mvm_cleanup_nanokernel
#define SKIN_INIT_MODULE     mvm_init_skin
#define SKIN_CLEANUP_MODULE  mvm_cleanup_skin
#define USER_INIT_MODULE     mvm_init_application
#define USER_CLEANUP_MODULE  mvm_cleanup_application

#ifdef __cplusplus
extern "C" {
#endif

void xnpod_welcome_thread(struct xnthread *);

void mvm_init(int argc,
	      char *argv[]);

int mvm_run(void *tcbarg,
	    void *faddr);

void mvm_finalize_init(void);

int mvm_hook_irq(unsigned irq,
		 void (*handler)(unsigned irq,
				 void *cookie),
		 void *cookie);

int mvm_release_irq(unsigned irq);

int mvm_post_irq(unsigned irq);

int mvm_enable_irq(unsigned irq);

int mvm_disable_irq(unsigned irq);

int mvm_set_irqmask(int level);

void mvm_start_timer(unsigned long nstick,
		     void (*tickhandler)(void));

void mvm_stop_timer(void);

void *mvm_create_callback(void (*handler)(void *),
			  void *cookie);

void mvm_delete_callback (void *cbhandle);

void mvm_schedule_callback(void *cbhandle,
			   unsigned long ns);

unsigned long long mvm_get_cpu_time(void);

unsigned long mvm_get_cpu_freq(void);

struct XenoThread *mvm_spawn_thread(void *tcbarg,
				    void *faddr,
				    const char *name);

int mvm_get_thread_imask (void *tcbarg);

const char *mvm_get_thread_state(void *tcbarg);

void mvm_restart_thread(struct XenoThread *thread);

struct XenoThread *mvm_thread_self(void);

void __mvm_breakable(mvm_switch_threads)(struct XenoThread *out,
					   struct XenoThread *in);

void mvm_finalize_switch_threads(struct XenoThread *dead,
				 struct XenoThread *in);

void mvm_finalize_thread(struct XenoThread *dead);

void __mvm_breakable(mvm_terminate)(int xcode);

void __mvm_breakable(mvm_fatal)(const char *format, ...);

void __mvm_breakable(mvm_break)(void);

void __mvm_breakable(mvm_join_threads)(void);

void mvm_create_display(struct mvm_displayctx *ctx,
			struct mvm_displayctl *ctl,
			void *obj,
			const char *name);

void mvm_delete_display(struct mvm_displayctx *ctx);

void mvm_send_display(struct mvm_displayctx *ctx,
		      const char *s);

void __mvm_breakable(mvm_post_graph)(struct mvm_displayctx *ctx,
				       int state);

void mvm_tcl_init_list(mvm_tcl_listobj_t *tclist);

void mvm_tcl_destroy_list(mvm_tcl_listobj_t *tclist);

void mvm_tcl_set(mvm_tcl_listobj_t *tclist,
		 const char *s);

void mvm_tcl_append(mvm_tcl_listobj_t *tclist,
		    const char *s);

void mvm_tcl_clear(mvm_tcl_listobj_t *tclist);

void mvm_tcl_append_int(mvm_tcl_listobj_t *tclist,
			u_long n);

void mvm_tcl_append_hex(mvm_tcl_listobj_t *tclist,
			u_long n);

void mvm_tcl_append_list(mvm_tcl_listobj_t *tclist,
			 mvm_tcl_listobj_t *tclist2);

const char *mvm_tcl_value(mvm_tcl_listobj_t *tclist);

void mvm_tcl_build_pendq(mvm_tcl_listobj_t *tclist,
			 struct xnsynch *synch);

#ifdef XENO_INTR_MODULE

static inline int xnarch_hook_irq (unsigned irq,
				   void (*handler)(unsigned irq,
						   void *cookie),
				   void *cookie) {

    return mvm_hook_irq(irq,handler,cookie);
}

static inline int xnarch_release_irq (unsigned irq) {

    return mvm_release_irq(irq);
}

static inline int xnarch_enable_irq (unsigned irq) {

    return mvm_enable_irq(irq);
}

static inline int xnarch_disable_irq (unsigned irq) {

    return mvm_disable_irq(irq);
}

static inline void xnarch_isr_chain_irq (unsigned irq) {
    /* Nop */
}

static inline void xnarch_isr_enable_irq (unsigned irq) {
    /* Nop */
}

#endif /* XENO_INTR_MODULE */

#ifdef XENO_MAIN_MODULE

static inline int xnarch_init (void) {
    return 0;
}

static inline void xnarch_exit (void) {
}

int mvm_init_nanokernel(void);
void mvm_cleanup_nanokernel(void);
int mvm_init_skin(void);
void mvm_cleanup_skin(void);
int mvm_init_application(void);
void mvm_cleanup_application(void);

void mvm_root (void *cookie)

{
    int err;

    err = mvm_init_skin();

    if (err)
	__mvm_breakable(mvm_fatal)("init_skin() failed, err=%x\n",err);

    err = mvm_init_application();

    if (err)
	__mvm_breakable(mvm_fatal)("init_application() failed, err=%x\n",err);

    /* Wait for all RT-threads to finish */
    __mvm_breakable(mvm_join_threads)();

    mvm_cleanup_application();
    mvm_cleanup_skin();
    mvm_cleanup_nanokernel();

    __mvm_breakable(mvm_terminate)(0);
}

int main (int argc, char *argv[])

{
    xnarchtcb_t tcb;
    int err;

    err = mvm_init_nanokernel();

    if (err)
	__mvm_breakable(mvm_fatal)("init_nanokernel() failed, err=%x\n",err);

    mvm_init(argc,argv);

    tcb.entry = &mvm_root;
    tcb.cookie = NULL;
    tcb.kthread = NULL;
    tcb.vmthread = NULL;
    tcb.imask = 0;
    tcb.stacksize = 0;
    tcb.stackbase = NULL;

    return mvm_run(&tcb,(void *)&mvm_root);
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

#ifdef XENO_POD_MODULE

static inline void xnarch_start_timer (unsigned long nstick,
				       void (*tickhandler)(void)) {
    mvm_start_timer(nstick,tickhandler);
}

static inline void xnarch_stop_timer (void) {
    mvm_stop_timer();
}

static inline void xnarch_leave_root(xnarchtcb_t *rootcb) {
    /* Empty */
}

static inline void xnarch_enter_root(xnarchtcb_t *rootcb) {
    /* Empty */
}

static inline void xnarch_switch_to (xnarchtcb_t *out_tcb,
				     xnarchtcb_t *in_tcb) {

    __mvm_breakable(mvm_switch_threads)(out_tcb->vmthread,in_tcb->vmthread);
}

static inline void xnarch_finalize_and_switch (xnarchtcb_t *dead_tcb,
					       xnarchtcb_t *next_tcb) {

    mvm_finalize_switch_threads(dead_tcb->vmthread,next_tcb->vmthread);
}

static inline void xnarch_finalize_no_switch (xnarchtcb_t *dead_tcb) {

    if (dead_tcb->vmthread)	/* Might be unstarted. */
	mvm_finalize_thread(dead_tcb->vmthread);
}

static inline void xnarch_save_fpu(xnarchtcb_t *tcb) {
    /* Nop */
}

static inline void xnarch_restore_fpu(xnarchtcb_t *tcb) {
    /* Nop */
}

static inline void xnarch_init_root_tcb (xnarchtcb_t *tcb,
					 struct xnthread *thread,
					 const char *name) {
    tcb->vmthread = mvm_thread_self();
}

static inline void xnarch_init_tcb (xnarchtcb_t *tcb,
				    void *adcookie) { /* <= UNUSED */
    tcb->vmthread = NULL;
}

static inline void xnarch_init_thread (xnarchtcb_t *tcb,
				       void (*entry)(void *),
				       void *cookie,
				       int imask,
				       struct xnthread *thread,
				       char *name)
{
    tcb->imask = imask;
    tcb->kthread = thread;
    tcb->entry = entry;
    tcb->cookie = cookie;

    if (tcb->vmthread)	/* Restarting thread */
	{
	mvm_restart_thread(tcb->vmthread);
	return;
	}

    tcb->vmthread = mvm_spawn_thread(tcb,(void *)entry,name);
}

static inline void xnarch_init_fpu(xnarchtcb_t *tcb) {
    /* Nop */
}

int xnarch_setimask (int imask) {
    return mvm_set_irqmask(imask);
}

#define xnarch_announce_tick() /* Nullified */

#define xnarch_notify_ready() mvm_finalize_init()

#endif /* XENO_POD_MODULE */

static inline unsigned long long xnarch_tsc_to_ns (unsigned long long ts) {
    return ts;
}

static inline unsigned long long xnarch_get_cpu_time (void) {
    return mvm_get_cpu_time();
}

static inline unsigned long long xnarch_get_cpu_tsc (void) {
    return mvm_get_cpu_time();
}

static inline unsigned long xnarch_get_cpu_freq (void) {
    return mvm_get_cpu_freq();
}

static inline void xnarch_halt (const char *emsg) {
    __mvm_breakable(mvm_fatal)("%s",emsg);
}

int xnarch_setimask(int imask);

#ifdef __cplusplus
}
#endif

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
 __mvm_sarray ## tag, \
}

struct MvmDashboard;
struct MvmGraph;

typedef struct mvm_displayctx {

    struct MvmDashboard *dashboard; /* A control board */
    struct MvmGraph *graph;	/* A state diagram */
    mvm_displayctl_t *control;	/* The associated control block */
    void *obj;			/* The rt-iface object */

} mvm_displayctx_t;

#define XNARCH_DECL_DISPLAY_CONTEXT() \
mvm_displayctx_t __mvm_display_context

#define xnarch_init_display_context(obj) \
do { \
(obj)->__mvm_display_context.dashboard = NULL; \
(obj)->__mvm_display_context.graph = NULL; \
} while(0)

#define xnarch_create_display(obj,name,tag) \
do { \
extern mvm_displayctl_t __mvm_displayctl_ ## tag; \
mvm_create_display(&(obj)->__mvm_display_context,&__mvm_displayctl_ ## tag,obj,name); \
} while(0)

#define xnarch_delete_display(obj) \
mvm_delete_display(&(obj)->__mvm_display_context)

#define xnarch_post_graph(obj,state) \
__mvm_breakable(mvm_post_graph)(&(obj)->__mvm_display_context,state)

#define xnarch_post_graph_if(obj,state,cond) \
do \
if (cond) \
__mvm_breakable(mvm_post_graph)(&(obj)->__mvm_display_context,state); \
while(0)

#endif /* !_nslib_include_asm_rtai_xeno_h */
