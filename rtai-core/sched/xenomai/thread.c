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

#include "rtai_config.h"
#include "xenomai/pod.h"
#include "xenomai/synch.h"
#include "xenomai/heap.h"
#include "xenomai/mutex.h"
#include "xenomai/thread.h"
#include "xenomai/module.h"

static void xnthread_timeout_handler (void *cookie)

{
    xnthread_t *thread = (xnthread_t *)cookie;
    setbits(thread->status,XNTIMEO);
    xnpod_resume_thread(thread,XNDELAY);
}

int xnthread_init (xnthread_t *thread,
		   const char *name,
		   int prio,
		   xnflags_t flags,
		   unsigned stacksize,
		   void *adcookie,
		   unsigned magic)
{
    xntimer_init(&thread->timer,&xnthread_timeout_handler,thread);

    xntimer_init(&thread->atimer,NULL,NULL);

    /* Align stack on a word boundary */
    stacksize &= ~(sizeof(int) - 1);

    if (stacksize > 0) 
	{
	thread->tcb.stackbase = (unsigned long *)xnmalloc(stacksize);

	if (!thread->tcb.stackbase)
	    return XNERR_NOMEM;
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
    thread->magic = magic;

    /* These will be filled by xnpod_start_thread() */
    thread->imask = 0;
    thread->imode = 0;
    thread->entry = NULL;
    thread->cookie = 0;
    thread->stime = 0;
    thread->extinfo = NULL;
    thread->adcookie = adcookie;

    if (name)
	{
	const char *rp = name;
	char *wp = thread->name;
	do
	    *wp++ = *rp;
	while (*rp && rp++ - name < XNOBJECT_NAME_LEN);
	}
    else
	*thread->name = '\0';

    inith(&thread->glink);
    inith(&thread->slink);
    initph(&thread->rlink);
    initph(&thread->plink);
    initpq(&thread->claimq,xnpod_get_qdir(nkpod));
    thread->sched = &nkpod->sched;

    xnarch_init_display_context(thread);

    return XN_OK;
}

void xnthread_cleanup_tcb (xnthread_t *thread)

{
    xnarchtcb_t *tcb = xnthread_archtcb(thread);

    /* Does not wreck the TCB, only releases the held resources. */

    if (tcb->stackbase)
	xnfree(tcb->stackbase);

    thread->magic = 0;
}

EXPORT_SYMBOL(xnthread_init);


