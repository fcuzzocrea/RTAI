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

#define XENO_MAIN_MODULE 1

#include <rtai_config.h>
#include <nucleus/module.h>
#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <nucleus/version.h>
#ifdef CONFIG_RTAI_OPT_PIPE
#include <nucleus/pipe.h>
#endif /* CONFIG_RTAI_OPT_PIPE */
#ifdef CONFIG_RTAI_OPT_FUSION
#include <nucleus/fusion.h>
#endif /* CONFIG_RTAI_OPT_FUSION */
#include <nucleus/ltt.h>

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

extern struct proc_dir_entry *rthal_proc_root;

#ifdef CONFIG_RTAI_OPT_FUSION
static struct proc_dir_entry *iface_proc_root;
#endif /* CONFIG_RTAI_OPT_FUSION */

static inline xnticks_t __get_thread_timeout (xnthread_t *thread, xnticks_t now)

{
    xnticks_t diff;

    if (!testbits(thread->status,XNDELAY))
	return 0LL;

    diff = (xntimer_get_date(&thread->rtimer) ? : xntimer_get_date(&thread->ptimer));
    diff -= now;

    return (diff <= 0) ? 1 : diff;
}

static int sched_read_proc (char *page,
			    char **start,
			    off_t off,
			    int count,
			    int *eof,
			    void *data)
{
    xnticks_t now;
    const unsigned nr_cpus = xnarch_num_online_cpus();
    xnthread_t *thread;
    xnholder_t *holder;
    char *p = page;
    char buf[64];
    unsigned cpu;
    int len = 0;
    spl_t s;

    if (!nkpod)
	goto out;

    xnlock_get_irqsave(&nklock, s);

    p += sprintf(p,"%-3s   %-6s %-12s %-4s  %-8s  %-8s\n",
		 "CPU","PID","NAME","PRI","TIMEOUT","STATUS");

#ifdef CONFIG_RTAI_HW_APERIODIC_TIMER
    if (!testbits(nkpod->status,XNTMPER))
        now = xnarch_get_cpu_time();
    else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
        now = nkpod->jiffies;

    for (cpu = 0; cpu < nr_cpus; ++cpu)
        {
        xnsched_t *sched = xnpod_sched_slot(cpu);

        holder = getheadq(&nkpod->threadq);

        while (holder)
	    {
	    thread = link2thread(holder,glink);
            holder = nextq(&nkpod->threadq,holder);

            if (thread->sched != sched)
                continue;

	    p += sprintf(p,"%3u   %-6d %-12s %-4d  %-8Lu  0x%.8lx - %s\n",
                         cpu,
			 !testbits(thread->status,XNROOT) && xnthread_user_task(thread) ?
			 xnthread_user_task(thread)->pid : 0,
                         thread->name,
			 thread->cprio,
			 __get_thread_timeout(thread, now),
			 thread->status,
			 xnthread_symbolic_status(thread->status,
                                                  buf,sizeof(buf)));
            }
        }

    xnlock_put_irqrestore(&nklock, s);

 out:

    len = p - page - off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if (len > count) len = count;
    if (len < 0) len = 0;

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
    char *end, buf[16];
    long ns;
    int n;

    n = count > sizeof(buf) - 1 ? sizeof(buf) - 1 : count;

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

static ssize_t timer_read_proc (char *page,
				char **start,
				off_t off,
				int count,
				int *eof,
				void *data)
{
    xnticks_t jiffies = 0, tickval = 0;
    const char *status = "off";
    int len;

    if (nkpod && testbits(nkpod->status,XNTIMED))
	{
#ifdef CONFIG_RTAI_HW_APERIODIC_TIMER
	if (!testbits(nkpod->status,XNTMPER))
	    {
	    status = "oneshot";
	    tickval = 1;
	    jiffies = xnarch_get_cpu_tsc();
	    }
	else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
	    {
	    status = "periodic";
	    tickval = xnpod_get_tickval();
	    jiffies = nkpod->jiffies;
	    }
	}

    len = sprintf(page,
		  "status=%s:setup=%Lu:tickval=%Lu:jiffies=%Lu\n",
		  status,
		  xnarch_tsc_to_ns(nktimerlat),
		  tickval,
		  jiffies);

    len -= off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if(len > count) len = count;
    if(len < 0) len = 0;

    return len;
}

static struct proc_dir_entry *add_proc_leaf (const char *name,
					     read_proc_t rdproc,
					     write_proc_t wrproc,
					     void *data,
					     struct proc_dir_entry *parent)
{
    int mode = wrproc ? 0644 : 0444;
    struct proc_dir_entry *entry;

    entry = create_proc_entry(name,mode,parent);

    if (!entry)
	return NULL;

    entry->nlink = 1;
    entry->data = data;
    entry->read_proc = rdproc;
    entry->write_proc = wrproc;
    entry->owner = THIS_MODULE;

    return entry;
}

void xnpod_init_proc (void)

{
    if (!rthal_proc_root)
	return;

    add_proc_leaf("sched",
		  &sched_read_proc,
		  NULL,
		  NULL,
		  rthal_proc_root);

    add_proc_leaf("latency",
		  &latency_read_proc,
		  &latency_write_proc,
		  NULL,
		  rthal_proc_root);

    add_proc_leaf("version",
		  &version_read_proc,
		  NULL,
		  NULL,
		  rthal_proc_root);

    add_proc_leaf("timer",
		  &timer_read_proc,
		  NULL,
		  NULL,
		  rthal_proc_root);

#ifdef CONFIG_RTAI_OPT_FUSION
    iface_proc_root = create_proc_entry("interfaces",
					S_IFDIR,
					rthal_proc_root);
#endif /* CONFIG_RTAI_OPT_FUSION */
}

void xnpod_delete_proc (void)

{
#ifdef CONFIG_RTAI_OPT_FUSION
    int muxid;

    for (muxid = 0; muxid < XENOMAI_MUX_NR; muxid++)
	if (muxtable[muxid].proc)
	    remove_proc_entry(muxtable[muxid].name,iface_proc_root);

    remove_proc_entry("interfaces",rthal_proc_root);
#endif /* CONFIG_RTAI_OPT_FUSION */
    remove_proc_entry("timer",rthal_proc_root);
    remove_proc_entry("version",rthal_proc_root);
    remove_proc_entry("latency",rthal_proc_root);
    remove_proc_entry("sched",rthal_proc_root);
}

#ifdef CONFIG_RTAI_OPT_FUSION

static ssize_t iface_read_proc (char *page,
				char **start,
				off_t off,
				int count,
				int *eof,
				void *data)
{
    struct xnskentry *iface = (struct xnskentry *)data;
    int len, refcnt = xnarch_atomic_get(&iface->refcnt);

    len = sprintf(page,"%d\n",refcnt < 0 ? 0 : refcnt);
    len -= off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if(len > count) len = count;
    if(len < 0) len = 0;

    return len;
}

void xnpod_declare_iface_proc (struct xnskentry *iface)

{
    iface->proc = add_proc_leaf(iface->name,
				&iface_read_proc,
				NULL,
				iface,
				iface_proc_root);
}

void xnpod_discard_iface_proc (struct xnskentry *iface)

{
    remove_proc_entry(iface->name,iface_proc_root);
    iface->proc = NULL;
}

#endif /* CONFIG_RTAI_OPT_FUSION */

#endif /* CONFIG_PROC_FS && __KERNEL__ */

int __init __fusion_sys_init (void)

{
    int err = xnarch_init();

    if (err)
	goto fail;

#ifdef __KERNEL__
#ifdef CONFIG_PROC_FS
    xnpod_init_proc();
#endif /* CONFIG_PROC_FS */

#ifdef CONFIG_LTT
    xnltt_mount();
#endif /* CONFIG_LTT */

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
#endif /* __KERNEL__ */

    xnloginfo("fusion core v%s (%s) started.\n",
	      PACKAGE_VERSION,
	      FUSION_VERSION_NAME);

    return 0;

#ifdef __KERNEL__

#ifdef CONFIG_RTAI_OPT_FUSION
 cleanup_heap:

    xnheap_umount();

 cleanup_pipe:

#endif /* CONFIG_RTAI_OPT_FUSION */

#ifdef CONFIG_RTAI_OPT_PIPE
    xnpipe_umount();

 cleanup_arch:

#endif /* CONFIG_RTAI_OPT_PIPE */

#ifdef CONFIG_PROC_FS
    xnpod_delete_proc();
#endif /* CONFIG_PROC_FS */

    xnarch_exit();

#endif /* __KERNEL__ */

 fail:

    xnlogerr("System init failed, code %d.\n",err);

    return err;
}

void __exit __fusion_sys_exit (void)

{
    xnpod_shutdown(XNPOD_NORMAL_EXIT);

    xnarch_exit();

#ifdef __KERNEL__
#ifdef CONFIG_RTAI_OPT_FUSION
    xnfusion_umount();
    xnheap_umount();
#endif /* CONFIG_RTAI_OPT_FUSION */
#ifdef CONFIG_RTAI_OPT_PIPE
    xnpipe_umount();
#endif /* CONFIG_RTAI_OPT_PIPE */
#ifdef CONFIG_LTT
    xnltt_umount();
#endif /* CONFIG_LTT */
#ifdef CONFIG_PROC_FS
    xnpod_delete_proc();
#endif /* CONFIG_PROC_FS */
#endif /* __KERNEL__ */
    xnloginfo("fusion core stopped.\n");
}

EXPORT_SYMBOL(xnmod_glink_queue);
EXPORT_SYMBOL(xnmod_alloc_glinks);

module_init(__fusion_sys_init);
module_exit(__fusion_sys_exit);
