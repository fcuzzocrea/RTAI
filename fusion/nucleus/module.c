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

/*!
 * \defgroup nucleus Xenomai nucleus.
 *
 * A RTOS abstraction layer.
 *
 */

#define XENO_MAIN_MODULE

#include <nucleus/module.h>
#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <nucleus/version.h>
#include <nucleus/dbridge.h>
#include <nucleus/fusion.h>

MODULE_DESCRIPTION("Xenomai nucleus");
MODULE_AUTHOR("rpm@xenomai.org");
MODULE_LICENSE("GPL");

xnqueue_t xnmod_glink_queue;

void xnmod_alloc_glinks (xnqueue_t *freehq)

{
    xngholder_t *sholder, *eholder;

    sholder = (xngholder_t *)xnheap_alloc(&kheap,sizeof(xngholder_t) * XNMOD_GHOLDER_REALLOC);

    if (!sholder)
	{
	/* If we are running out of memory but still have some free
	   holders, just return silently, hoping that the contention
	   will disappear before we have no other choice than
	   allocating memory eventually. Otherwise, we have to raise a
	   fatal error right now. */

	if (countq(freehq) == 0)
	    xnpod_fatal("cannot allocate generic holders");

	return;
	}

    for (eholder = sholder + XNMOD_GHOLDER_REALLOC;
	 sholder < eholder; sholder++)
	{
	inith(&sholder->glink.plink);
	appendq(freehq,&sholder->glink.plink);
	}
}

#if defined(CONFIG_PROC_FS) && defined(__KERNEL__)

#include <linux/proc_fs.h>

static struct proc_dir_entry *xenomai_proc_entry;

static int xnpod_read_proc (char *page,
			    char **start,
			    off_t off,
			    int count,
			    int *eof,
			    void *data)
{
    int len = 0, muxid, nrxfaces = 0;
    xnthread_t *thread;
    xnholder_t *holder;
    char *p = page;
    spl_t s;
    const unsigned nr_cpus=xnarch_num_online_cpus();
    unsigned cpu, ready_threads = 0;

    p += sprintf(p,"\nRTAI/fusion system v%s\n",PACKAGE_VERSION);
    p += sprintf(p,"Mounted over Adeos %s\n",ADEOS_VERSION_STRING);
    p += sprintf(p,"Registered interface(s): ");

    for (muxid = 0; muxid < XENOMAI_MUX_NR; muxid++)
	if (muxtable[muxid].magic != 0)
	    {
	    p += sprintf(p,"%s%s (%d)",nrxfaces > 0 ? ", " : "",
			    muxtable[muxid].name, 
			    xnarch_atomic_get(&muxtable[muxid].refcnt));
	    nrxfaces++;
	    }

    if (nrxfaces == 0)
	p += sprintf(p,"<none>");

    p += sprintf(p,"\n\nTimer latency: %Lu ns\n",xnarch_tsc_to_ns(nktimerlat));

    if (nkpod != NULL && testbits(nkpod->status,XNTIMED))
	{
	if (testbits(nkpod->status,XNTMPER))
	    p += sprintf(p,"Timer status: periodic [tickval=%lu us, elapsed=%Lu]\n",
			 xnpod_get_tickval() / 1000,
			 nkpod->jiffies);
	else
	    p += sprintf(p,"Timer status: aperiodic.\n");
	}
    else
	p += sprintf(p,"Timer status: inactive.\n");

    p += sprintf(p,"Scheduling latency: %Lu ns\n",xnarch_tsc_to_ns(nkschedlat));

    if (!nkpod)
	{
	p += sprintf(p,"No active pod.\n");
	goto out;
	}

    if (testbits(nkpod->status, XNPINIT))
        {
        p += sprintf(p, "Pod initializing.\n");
        goto out;
        }

    xnlock_get_irqsave(&nklock, s);

    for (cpu = 0; cpu < nr_cpus; ++cpu)
        {
        xnsched_t *sched = xnpod_sched_slot(cpu);
        ready_threads += sched->readyq.pqueue.elems;
        }

    xnlock_put_irqrestore(&nklock, s);

    p += sprintf(p,"\nScheduler status: %d threads, %d ready, %d blocked\n",
                 nkpod->threadq.elems,
                 ready_threads,
                 nkpod->suspendq.elems);
    
    p += sprintf(p,"\n%-3s   %-12s %-4s  %-5s  %-8s\n","CPU", "NAME","PRI",
                     "TIMEOUT", "STATUS");
    for (cpu = 0; cpu < nr_cpus; ++cpu)
        {
        xnsched_t *sched = xnpod_sched_slot(cpu);

        p += sprintf(p,"------------------------------------------\n");

        xnlock_get_irqsave(&nklock, s);

        holder = getheadq(&nkpod->threadq);

        while (holder)
	    {
	    thread = link2thread(holder,glink);
            holder = nextq(&nkpod->threadq,holder);

            if (thread->sched != sched)
                continue;

	    p += sprintf(p,"%3u   %-12s %-4d  %-5Lu  0x%.8lx\n",
                         cpu,
                         thread->name,
                         thread->cprio,
                         (testbits(thread->status,XNDELAY)
                          ? xnthread_timeout(thread)
                          : 0LL ),
                         thread->status);
            }

        xnlock_put_irqrestore(&nklock, s);
        }

 out:

    len = p - page;

    if (len <= off + count)
	*eof = 1;

    *start = page + off;

    len -= off;

    if (len > count)
	len = count;

    if (len < 0)
	len = 0;

    return len;
}

void xnpod_init_proc (void) {

    xenomai_proc_entry = create_proc_read_entry("rtai/system",
						0444,
						NULL,
						&xnpod_read_proc,
						NULL);
}

void xnpod_delete_proc (void) {

    remove_proc_entry("rtai/system",NULL);
}

#endif /* CONFIG_PROC_FS */

int __xeno_main_init (void)

{
    int err;

    err = xnarch_init();

    if (!err)
#ifdef __KERNEL__
	{
	int xnpod_calibrate_sched(void);

#ifdef CONFIG_PROC_FS
	xnpod_init_proc();
#endif /* CONFIG_PROC_FS */

	err = xnpod_calibrate_sched();

	if (!err)
	    {
	    err = xnbridge_init();

	    if (!err)
		{
		err = xnfusion_init();

		if (!err)
		    xnprintf("RTAI: RTOS nucleus loaded.\n");
		else
		    xnprintf("RTAI: Fusion layer initialization failed, code %d.\n",err);
		}
	    else
		xnprintf("RTAI: Dbridge initialization failed, code %d.\n",err);
	    }
	else
	    xnprintf("RTAI: Autocalibration procedure failed, code %d.\n",err);
	}
#else /* !__KERNEL__ */
	xnprintf("RTAI: Virtual machine started.\n");
#endif /* __KERNEL__ */
    else
	xnprintf("RTAI: RTOS nucleus initialization failed, code %d.\n",err);

    return err;
}

void __xeno_main_exit (void)

{
    xnpod_shutdown(XNPOD_NORMAL_EXIT);

    xnarch_exit();

#ifdef __KERNEL__
#ifdef CONFIG_PROC_FS
    xnpod_delete_proc();
#endif /* CONFIG_PROC_FS */
    xnfusion_exit();
    xnbridge_exit();
    xnprintf("RTAI: RTOS nucleus unloaded.\n");
#else /* !__KERNEL__ */
    xnprintf("RTAI: Virtual machine stopped.\n");
#endif /* __KERNEL__ */
}

EXPORT_SYMBOL(xnmod_glink_queue);
EXPORT_SYMBOL(xnmod_alloc_glinks);

module_init(__xeno_main_init);
module_exit(__xeno_main_exit);
