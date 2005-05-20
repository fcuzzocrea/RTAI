/*
 * Copyright (C) 2004 Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA
 * 02139, USA; either version 2 of the License, or (at your option)
 * any later version.
 *
 * RTAI/fusion is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <stdarg.h>
#include <nucleus/ltt.h>

void xnltt_log_mark (const char *fmt, ...)

{
    char markbuf[64];	/* Don't eat too much stack space. */
    va_list ap;

    if (xnltt_evtable[rtai_ev_mark].ltt_filter & xnltt_filter) {
	va_start(ap,fmt);
	vsnprintf(markbuf,sizeof(markbuf),fmt,ap);
	va_end(ap);
	ltt_log_std_formatted_event(xnltt_evtable[rtai_ev_mark].ltt_evid,markbuf);
    }
}

int __init xnltt_mount (void)

{
    int ev, evid;

    /* Create all custom LTT events we need. */

    for (ev = 0; xnltt_evtable[ev].ltt_label != NULL; ev++)
        {
	evid = ltt_create_event(xnltt_evtable[ev].ltt_label,
				xnltt_evtable[ev].ltt_format,
				LTT_CUSTOM_EV_FORMAT_TYPE_STR,
				NULL);
	if (evid < 0)
	    {
	    while (--ev >= 0)
	        {
		xnltt_evtable[ev].ltt_evid = -1;
		ltt_destroy_event(xnltt_evtable[ev].ltt_evid);
		}

	    return evid;
	    }

	xnltt_evtable[ev].ltt_evid = evid;
	}

#ifdef CONFIG_RTAI_OPT_FILTER_EVALL
    xnltt_filter = ~rtai_evall;
#else /* !CONFIG_RTAI_OPT_FILTER_EVALL */
#ifdef CONFIG_RTAI_OPT_FILTER_EVIRQ
    xnltt_filter &= ~rtai_evirq;
#endif /* CONFIG_RTAI_OPT_FILTER_EVIRQ */
#ifdef CONFIG_RTAI_OPT_FILTER_EVTHR
    xnltt_filter &= ~rtai_evthr;
#endif /* CONFIG_RTAI_OPT_FILTER_EVTHR */
#ifdef CONFIG_RTAI_OPT_FILTER_EVSYS
    xnltt_filter &= ~rtai_evthr;
#endif /* CONFIG_RTAI_OPT_FILTER_EVSYS */
#endif /* CONFIG_RTAI_OPT_FILTER_EVALL */
   
    return 0;
}

void __exit xnltt_umount (void)

{
    int ev;

    for (ev = 0; xnltt_evtable[ev].ltt_evid != -1; ev++)
	ltt_destroy_event(xnltt_evtable[ev].ltt_evid);
}

struct xnltt_evmap xnltt_evtable[] = {

    [rtai_ev_ienter] = { "RTAI i-enter", "irq=%d", -1, rtai_evirq },
    [rtai_ev_iexit] = { "RTAI i-exit", "irq=%d", -1, rtai_evirq  },
    [rtai_ev_resched] = { "RTAI resched", NULL, -1, rtai_evthr },
    [rtai_ev_smpsched] = { "RTAI smpsched", NULL, -1, rtai_evthr },
    [rtai_ev_fastsched] = { "RTAI fastsched", NULL, -1, rtai_evthr },
    [rtai_ev_switch] = { "RTAI ctxsw", "%s -> %s", -1, rtai_evthr },
    [rtai_ev_fault] = { "RTAI fault", "thread=%s, location=%p, trap=%d", -1, rtai_evall },
    [rtai_ev_callout] = { "RTAI callout", "type=%s, thread=%s", -1, rtai_evall },
    [rtai_ev_finalize] = { "RTAI finalize", "%s -> %s", -1, rtai_evall },
    [rtai_ev_thrinit] = { "RTAI thread init", "thread=%s, flags=0x%x", -1, rtai_evthr },
    [rtai_ev_thrstart] = { "RTAI thread start", "thread=%s", -1, rtai_evthr },
    [rtai_ev_threstart] = { "RTAI thread restart", "thread=%s", -1, rtai_evthr },
    [rtai_ev_thrdelete] = { "RTAI thread delete", "thread=%s", -1, rtai_evthr },
    [rtai_ev_thrsuspend] = { "RTAI thread suspend", "thread=%s, mask=0x%x, timeout=%Lu, wchan=%p", -1, rtai_evthr },
    [rtai_ev_thresume] = { "RTAI thread resume", "thread=%s, mask=0x%x", -1, rtai_evthr },
    [rtai_ev_thrunblock] = { "RTAI thread unblock", "thread=%s, status=0x%x", -1, rtai_evthr },
    [rtai_ev_threnice] = { "RTAI thread renice", "thread=%s, prio=%d", -1, rtai_evthr },
    [rtai_ev_cpumigrate] = { "RTAI CPU migrate", "thread=%s, cpu=%d", -1, rtai_evthr },
    [rtai_ev_sigdispatch] = { "RTAI sigdispatch", "thread=%s, sigpend=0x%x", -1, rtai_evall },
    [rtai_ev_thrboot] = { "RTAI thread begin", "thread=%s", -1, rtai_evthr },
    [rtai_ev_tmtick] = { "RTAI timer tick", "runthread=%s", -1, rtai_evirq },
    [rtai_ev_sleepon] = { "RTAI sleepon", "thread=%s, sync=%p", -1, rtai_evthr },
    [rtai_ev_wakeup1] = { "RTAI wakeup1", "thread=%s, sync=%p", -1, rtai_evthr },
    [rtai_ev_wakeupx] = { "RTAI wakeupx", "thread=%s, sync=%p", -1, rtai_evthr },
    [rtai_ev_syncflush] = { "RTAI syncflush", "sync=%p, reason=0x%x", -1, rtai_evthr },
    [rtai_ev_syncforget] = { "RTAI syncforget", "thread=%s, sync=%p", -1, rtai_evthr },
    [rtai_ev_lohandler] = { "RTAI lohandler", "type=%d, task=%s, pid=%d", -1, rtai_evall },
    [rtai_ev_primarysw] = { "RTAI modsw1", "thread=%s", -1, rtai_evthr },
    [rtai_ev_primary] = { "RTAI modex1", "thread=%s", -1, rtai_evthr },
    [rtai_ev_secondarysw] = { "RTAI modsw2", "thread=%s", -1, rtai_evthr },
    [rtai_ev_secondary] = { "RTAI modex2", "thread=%s", -1, rtai_evthr },
    [rtai_ev_shadowmap] = { "RTAI shadow map", "thread=%s, pid=%d, prio=%d", -1, rtai_evthr },
    [rtai_ev_shadowunmap] = { "RTAI shadow unmap", "thread=%s, pid=%d", -1, rtai_evthr },
    [rtai_ev_shadowstart] = { "RTAI shadow start", "thread=%s", -1, rtai_evthr },
    [rtai_ev_syscall] = { "RTAI syscall", "thread=%s, skin=%d, call=%d", -1, rtai_evsys },
    [rtai_ev_shadowexit] = { "RTAI shadow exit", "thread=%s", -1, rtai_evthr },
    [rtai_ev_thrsetmode] = { "RTAI thread setmode", "thread=%s, clrmask=0x%x, setmask=0x%x", -1, rtai_evthr },
    [rtai_ev_rdrotate] = { "RTAI rotate readyq", "thread=%s, prio=%d", -1, rtai_evthr },
    [rtai_ev_rractivate] = { "RTAI RR on", "quantum=%Lu", -1, rtai_evthr },
    [rtai_ev_rrdeactivate] = { "RTAI RR off", NULL, -1, rtai_evthr },
    [rtai_ev_timeset] = { "RTAI set time", "newtime=%Lu", -1, rtai_evall },
    [rtai_ev_addhook] = { "RTAI add hook", "type=%d, routine=%p", -1, rtai_evall },
    [rtai_ev_remhook] = { "RTAI remove hook", "type=%d, routine=%p", -1, rtai_evall },
    [rtai_ev_thrperiodic] = { "RTAI thread speriod", "thread=%s, idate=%Lu, period=%Lu", -1, rtai_evthr },
    [rtai_ev_thrwait] = { "RTAI thread wperiod", "thread=%s", -1, rtai_evthr },
    [rtai_ev_tmstart] = { "RTAI start timer", "tick=%u ns", -1, rtai_evall },
    [rtai_ev_tmstop] = { "RTAI stop timer", NULL, -1, rtai_evall },
    [rtai_ev_mark] = { "RTAI **mark**", "%s", -1, rtai_evall },
    [rtai_ev_watchdog] = { "RTAI watchdog", "runthread=%s", -1, rtai_evall },
    { NULL, NULL, -1, 0 },
};

int xnltt_filter = rtai_evall;

EXPORT_SYMBOL(xnltt_evtable);
EXPORT_SYMBOL(xnltt_filter);
EXPORT_SYMBOL(xnltt_log_mark);
