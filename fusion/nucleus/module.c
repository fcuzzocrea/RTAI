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

/*!
 * \defgroup nucleus RTAI/fusion nucleus.
 *
 * A RTOS abstraction layer.
 */

#define XENO_MAIN_MODULE

#include <rtai_config.h>
#include <nucleus/module.h>
#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <nucleus/version.h>
#include <nucleus/trace.h>
#ifdef CONFIG_RTAI_OPT_PIPE
#include <nucleus/pipe.h>
#endif /* CONFIG_RTAI_OPT_PIPE */
#ifdef CONFIG_RTAI_OPT_FUSION
#include <nucleus/fusion.h>
#endif /* CONFIG_RTAI_OPT_FUSION */

MODULE_DESCRIPTION("RTAI/fusion nucleus");
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
#include <linux/ctype.h>

static inline xnticks_t __get_thread_timeout (xnthread_t *thread)

{
    if (!testbits(thread->status,XNDELAY))
	return 0LL;

    return xntimer_get_timeout(&thread->rtimer) ?:
	xntimer_get_timeout(&thread->ptimer);
}

static int system_read_proc (char *page,
			     char **start,
			     off_t off,
			     int count,
			     int *eof,
			     void *data)
{
    const unsigned nr_cpus = xnarch_num_online_cpus();
    unsigned cpu, ready_threads = 0;
    xnthread_t *thread;
    xnholder_t *holder;
    char *p = page;
    char buf[64];
    int len = 0;
    spl_t s;

    p += sprintf(p,"RTAI/fusion nucleus v%s\n",PACKAGE_VERSION);
    p += sprintf(p,"Mounted over Adeos %s\n",ADEOS_VERSION_STRING);

    p += sprintf(p,"\nLatencies: timer=%Lu ns\n",
		 xnarch_tsc_to_ns(nktimerlat));

    xnlock_get_irqsave(&nklock, s);

    if (nkpod != NULL)
	{
	if (testbits(nkpod->status,XNTIMED))
	    {
#if CONFIG_RTAI_HW_APERIODIC_TIMER
	    if (!testbits(nkpod->status,XNTMPER))
		p += sprintf(p,"Aperiodic timer is running\n");
	    else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
		p += sprintf(p,"Periodic timer is running [tickval=%lu us, elapsed=%Lu]\n",
			     xnpod_get_tickval() / 1000,
			     nkpod->jiffies);
	    }
	else
	    p += sprintf(p,"No system timer\n");
	}
    else
	{
	p += sprintf(p,"No active pod\n");
	goto unlock_and_exit;
	}

    for (cpu = 0; cpu < nr_cpus; ++cpu)
        {
        xnsched_t *sched = xnpod_sched_slot(cpu);
        ready_threads += sched->readyq.pqueue.elems;
        }

    p += sprintf(p,"Scheduler status: %d threads, %d ready, %d blocked\n",
                 nkpod->threadq.elems,
                 ready_threads,
                 nkpod->suspendq.elems);
    
    p += sprintf(p,"\n%-3s   %-12s %-4s  %-5s  %-8s\n","CPU", "NAME","PRI",
		 "TIMEOUT", "STATUS");

    for (cpu = 0; cpu < nr_cpus; ++cpu)
        {
        xnsched_t *sched = xnpod_sched_slot(cpu);

        p += sprintf(p,"------------------------------------------\n");

        holder = getheadq(&nkpod->threadq);

        while (holder)
	    {
	    thread = link2thread(holder,glink);
            holder = nextq(&nkpod->threadq,holder);

            if (thread->sched != sched)
                continue;

	    p += sprintf(p,"%3u   %-12s %-4d  %-5Lu  0x%.8lx - %s\n",
                         cpu,
                         thread->name,
                         thread->cprio,
			 __get_thread_timeout(thread),
                         thread->status,
			 xnthread_symbolic_status(thread->status,
                                                  buf,sizeof(buf)));
            }
        }

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock, s);

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

static ssize_t latency_read_proc (char *page,
				  char **start,
				  off_t off,
				  int count,
				  int *eof,
				  void *data)
{
    int len;

    len = sprintf(page,"%Lu\n",xnarch_tsc_to_ns(nkschedlat));
    len -= off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if(len > count) len = count;
    if(len < 0) len = 0;

    return len;
}

static int latency_write_proc (struct file *file,
			       const char __user *buffer,
			       unsigned long count,
			       void *data)
{
    char *end, buf[sizeof("nnnnn\0") + 1];
    long ns;
    int n;

    n = count > sizeof(buf) ? sizeof(buf) : count;

    if (copy_from_user(buf,buffer,n))
	return -EFAULT;

    buf[n] = '\0';
    ns = simple_strtol(buf,&end,0);

    if ((*end != '\0' && !isspace(*end)) || ns < 0)
	return -EINVAL;

    nkschedlat = xnarch_ns_to_tsc(ns);

    return count;
}

static ssize_t version_read_proc (char *page,
				  char **start,
				  off_t off,
				  int count,
				  int *eof,
				  void *data)
{
    int len;

    len = sprintf(page,"%s\n",PACKAGE_VERSION);
    len -= off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if(len > count) len = count;
    if(len < 0) len = 0;

    return len;
}

static ssize_t iface_read_proc (char *page,
				char **start,
				off_t off,
				int count,
				int *eof,
				void *data)
{
    struct xnskentry *iface = (struct xnskentry *)data;
    int len;

    len = sprintf(page,"%d\n",xnarch_atomic_get(&iface->refcnt));
    len -= off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if(len > count) len = count;
    if(len < 0) len = 0;

    return len;
}

extern struct proc_dir_entry *rthal_proc_root;

void xnpod_init_proc (void)

{
    struct proc_dir_entry *entry;

    if (!rthal_proc_root)
	return;

    entry = create_proc_entry("system",0444,rthal_proc_root);

    if (entry)
	{
	entry->nlink = 1;
	entry->data = NULL;
	entry->read_proc = system_read_proc;
	entry->write_proc = NULL;
	entry->owner = THIS_MODULE;
	}

    entry = create_proc_entry("latency",0644,rthal_proc_root);

    if (entry)
	{
	entry->nlink = 1;
	entry->data = NULL;
	entry->read_proc = &latency_read_proc;
	entry->write_proc = &latency_write_proc;
	entry->owner = THIS_MODULE;
	}

    entry = create_proc_entry("version",0444,rthal_proc_root);

    if (entry)
	{
	entry->nlink = 1;
	entry->data = NULL;
	entry->read_proc = &version_read_proc;
	entry->write_proc = NULL;
	entry->owner = THIS_MODULE;
	}

#ifdef CONFIG_RTAI_OPT_FUSION
    {
    struct proc_dir_entry *ifdir, *ifent[XENOMAI_MUX_NR];
    int n;

    ifdir = create_proc_entry("interfaces",S_IFDIR,rthal_proc_root);

    if (ifdir)
	{
	for (n = 0; n < XENOMAI_MUX_NR; n++)
	    {
	    if (muxtable[n].magic != 0)
		continue;

	    entry = create_proc_entry(muxtable[n].name,0444,ifdir);

	    if (!entry)
		continue;

	    ifent[n] = entry;
	    entry->nlink = 1;
	    entry->data = muxtable + n;
	    entry->read_proc = &iface_read_proc;
	    entry->write_proc = NULL;
	    entry->owner = THIS_MODULE;
	    }
	}
    }
#endif /* CONFIG_RTAI_OPT_FUSION */
}

void xnpod_delete_proc (void)

{
    remove_proc_entry("rtai/interfaces",NULL);
    remove_proc_entry("rtai/version",NULL);
    remove_proc_entry("rtai/latency",NULL);
    remove_proc_entry("rtai/system",NULL);
}

#endif /* CONFIG_PROC_FS */

int __fusion_sys_init (void)

{
    int err;

    err = xnarch_init();

    if (err)
	goto fail;

#ifdef __KERNEL__
    {
#ifdef CONFIG_PROC_FS
    xnpod_init_proc();
#endif /* CONFIG_PROC_FS */

#ifdef CONFIG_RTAI_OPT_PIPE
    err = xnpipe_mount();

    if (err)
	goto cleanup_arch;
#endif /* CONFIG_RTAI_OPT_PIPE */

#ifdef CONFIG_RTAI_OPT_FUSION
    err = xnheap_mount();

    if (err)
	goto cleanup_pipe;

    err = xnfusion_mount();

    if (err)
	goto cleanup_heap;
#endif /* CONFIG_RTAI_OPT_FUSION */
    }
#endif /* __KERNEL__ */

    xnloginfo("RTAI/fusion v%s started.\n",PACKAGE_VERSION);

    return 0;

#ifdef __KERNEL__

#ifdef CONFIG_RTAI_OPT_FUSION
 cleanup_heap:

    xnheap_umount();

 cleanup_pipe:

#endif /* CONFIG_RTAI_OPT_FUSION */

#ifdef CONFIG_RTAI_OPT_PIPE
    xnpipe_umount();
#endif /* CONFIG_RTAI_OPT_PIPE */

 cleanup_arch:
    
#ifdef CONFIG_PROC_FS
    xnpod_delete_proc();
#endif /* CONFIG_PROC_FS */

    xnarch_exit();

#endif /* __KERNEL__ */

 fail:

    xnlogerr("System init failed, code %d.\n",err);

    return err;
}

void __fusion_sys_exit (void)

{
    xnpod_shutdown(XNPOD_NORMAL_EXIT);

    xnarch_exit();

#ifdef __KERNEL__
#ifdef CONFIG_PROC_FS
    xnpod_delete_proc();
#endif /* CONFIG_PROC_FS */
#ifdef CONFIG_RTAI_OPT_FUSION
    xnfusion_umount();
    xnheap_umount();
#endif /* CONFIG_RTAI_OPT_FUSION */
#ifdef CONFIG_RTAI_OPT_PIPE
    xnpipe_umount();
#endif /* CONFIG_RTAI_OPT_PIPE */
#endif /* __KERNEL__ */
    xnloginfo("RTAI/fusion stopped.\n");
}

EXPORT_SYMBOL(xnmod_glink_queue);
EXPORT_SYMBOL(xnmod_alloc_glinks);

#if defined(CONFIG_RTAI_OPT_TRACES) && __KERNEL__
rtai_trace_callback_t *rtai_trace_callback = NULL;
EXPORT_SYMBOL(rtai_trace_callback);
#endif /* CONFIG_RTAI_OPT_TRACES && __KERNEL__ */

module_init(__fusion_sys_init);
module_exit(__fusion_sys_exit);
