/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Julien Pinon <jpinon@idealx.com>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
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
#include "vrtx/task.h"
#include "vrtx/mb.h"

static xnqueue_t vrtxmbq;

void vrtxmb_init (void)
{
    initq(&vrtxmbq);
}

void vrtxmb_cleanup (void)
{
    vrtxmsg_t *msg_slot;
    xnholder_t *holder;

    while ((holder = getq(&vrtxmbq)) != NULL)
	{
	msg_slot = (vrtxmsg_t *)holder;
	xnsynch_destroy(&msg_slot->synchbase);
	xnfree(msg_slot);
	}
}

char *sc_accept (char **mboxp, int *errp)
{
    char *msg;

    xnmutex_lock(&__imutex);

    msg = *mboxp;

    if (msg == 0)
	{
	*errp = ER_NMP;
	}
    else
	{
	*mboxp = 0;
	*errp = RET_OK;
	}

    xnmutex_unlock(&__imutex);

    return msg;
}

/**
  Manages a hash of xnsynch_t objects, indexed by mailboxes addresses.
  Given a mailbox, returns its synch.
  If the synch is not found, creates one,
*/
xnsynch_t * mb_get_synch_internal(char **mboxp)
{
    xnholder_t *holder;
    vrtxmsg_t *msg_slot;

    xnmutex_lock(&__imutex);
    for (holder = getheadq(&vrtxmbq);
	 holder != NULL; holder = nextq(&vrtxmbq, holder))
	{
	if ( ((vrtxmsg_t *)holder)->mboxp == mboxp)
	    {
	    xnmutex_unlock(&__imutex);
	    return &((vrtxmsg_t *)holder)->synchbase;
	    }
	}

    /* not found */
    msg_slot = (vrtxmsg_t *)xnmalloc(sizeof(*msg_slot));

    inith(&msg_slot->link);
    msg_slot->mboxp = mboxp;
    xnsynch_init(&msg_slot->synchbase ,XNSYNCH_PRIO);

    appendq(&vrtxmbq, &msg_slot->link);

    xnmutex_unlock(&__imutex);

    return &msg_slot->synchbase;
}

char *sc_pend (char **mboxp, long timeout, int *errp)
{
    char *msg;
    xnsynch_t *synchbase;
    vrtxtask_t *task;

    msg = *mboxp;

    xnmutex_lock(&__imutex);

    if (msg == 0)
	{
	synchbase = mb_get_synch_internal(mboxp);
	
	task = vrtx_current_task();
	task->vrtxtcb.TCBSTAT = TBSMBOX;
	if (timeout)
	    task->vrtxtcb.TCBSTAT |= TBSDELAY;

	xnsynch_sleep_on(synchbase,timeout,&__imutex);

	if (xnthread_test_flags(&task->threadbase,XNTIMEO))
	    {
	    xnmutex_unlock(&__imutex);
	    *errp = ER_TMO;
	    return NULL; /* Timeout.*/
	    }
	msg = vrtx_current_task()->waitargs.qmsg;
	}
    else
	{
	*mboxp = 0;
	}

    xnmutex_unlock(&__imutex);

    *errp = RET_OK;

    return msg;
}

void sc_post (char **mboxp, char *msg, int *errp)
{
    xnsynch_t *synchbase;
    xnthread_t *waiter;

    if (msg == 0)
	{
	*errp = ER_ZMW;
	return;
	}

    if (*mboxp != 0)
	{
	*errp = ER_MIU;
	return;
	}

    *errp = RET_OK;

    xnmutex_lock(&__imutex);

    synchbase = mb_get_synch_internal(mboxp);

    /* xnsynch_wakeup_one_sleeper() readies the thread */
    waiter = xnsynch_wakeup_one_sleeper(synchbase);
    
    if (waiter)
	{
	thread2vrtxtask(waiter)->waitargs.qmsg = msg;
	xnpod_schedule(&__imutex);
	}
    else
	*mboxp = msg;

    xnmutex_unlock(&__imutex);
}
