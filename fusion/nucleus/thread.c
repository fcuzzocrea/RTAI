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

#define XENO_THREAD_MODULE 1

#include <nucleus/pod.h>
#include <nucleus/synch.h>
#include <nucleus/heap.h>
#include <nucleus/thread.h>
#include <nucleus/module.h>

static void xnthread_timeout_handler (void *cookie)

{
    xnthread_t *thread = (xnthread_t *)cookie;
    __setbits(thread->status,XNTIMEO); /* Interrupts are off. */
    xnpod_resume_thread(thread,XNDELAY);
}

static void xnthread_periodic_handler (void *cookie)

{
    xnthread_t *thread = (xnthread_t *)cookie;

    thread->poverrun++;

    if (xnthread_test_flags(thread,XNDELAY)) /* Prevent unwanted round-robin. */
	xnpod_resume_thread(thread,XNDELAY);
}

int xnthread_init (xnthread_t *thread,
		   const char *name,
		   int prio,
		   xnflags_t flags,
		   unsigned stacksize)
{
    xntimer_init(&thread->rtimer,&xnthread_timeout_handler,thread);
    xntimer_set_priority(&thread->rtimer,XNTIMER_HIPRIO);
    xntimer_init(&thread->ptimer,&xnthread_periodic_handler,thread);
    xntimer_set_priority(&thread->ptimer,XNTIMER_HIPRIO);
    thread->poverrun = -1;

    /* Setup the TCB. */

    xnarch_init_tcb(xnthread_archtcb(thread));

    if (!(flags & XNSHADOW) && stacksize > 0)
	{
	/* Align stack on a word boundary */
	stacksize &= ~(sizeof(int) - 1);

	thread->tcb.stackbase = (unsigned long *)xnarch_alloc_stack(stacksize);

	if (!thread->tcb.stackbase)
	    return -ENOMEM;
	}
    else
	thread->tcb.stackbase = NULL;

    thread->tcb.stacksize = stacksize;
    thread->status = flags;
    thread->signals = 0;
    thread->asrmode = 0;
    thread->asrimask = 0;
    thread->asr = XNTHREAD_INVALID_ASR;
    thread->asrlevel = 0;

    thread->iprio = prio;
    thread->bprio = prio;
    thread->cprio = prio;
    thread->rrperiod = XN_INFINITE;
    thread->rrcredit = XN_INFINITE;
    thread->wchan = NULL;
    thread->magic = 0;

#ifdef CONFIG_RTAI_OPT_STATS
    thread->stat.psw = 0;
    thread->stat.ssw = 0;
    thread->stat.csw = 0;
    thread->stat.pf = 0;
#endif /* CONFIG_RTAI_OPT_STATS */

    /* These will be filled by xnpod_start_thread() */
    thread->imask = 0;
    thread->imode = 0;
    thread->entry = NULL;
    thread->cookie = 0;
    thread->stime = 0;
    thread->extinfo = NULL;

    if (name)
	xnobject_copy_name(thread->name,name);
    else
	snprintf(thread->name,sizeof(thread->name),"%p",thread);

    inith(&thread->glink);
    inith(&thread->slink);
    initph(&thread->rlink);
    initph(&thread->plink);
    initpq(&thread->claimq,xnpod_get_qdir(nkpod),xnpod_get_maxprio(nkpod,0));

    xnarch_init_display_context(thread);

    return 0;
}

void xnthread_cleanup_tcb (xnthread_t *thread)

{
    xnarchtcb_t *tcb = xnthread_archtcb(thread);

    /* Does not wreck the TCB, only releases the held resources. */

    if (tcb->stackbase)
	xnarch_free_stack((void *) tcb->stackbase);

    thread->magic = 0;
}

char *xnthread_symbolic_status (xnflags_t status, char *buf, int size)
{
    static const char *labels[] = XNTHREAD_SLABEL_INIT;
    char *wp;
    int pos;

    for (status &= ~(XNTHREAD_SPARES|XNROOT|XNSTARTED), pos = 0, wp = buf;
	 status != 0 && wp - buf < size - 5; /* 3-letters label + SPC + \0 */
	 status >>= 1, pos++)
	if (status & 1)
	    wp += sprintf(wp,"%s ",labels[pos]);

    *wp = '\0';

    return buf;
}
