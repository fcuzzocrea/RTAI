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
 * An abstract RTOS core.
 */

#define XENO_MAIN_MODULE 1

#include <rtai_config.h>
#include <nucleus/module.h>
#include <nucleus/pod.h>
#include <nucleus/timer.h>
#include <nucleus/heap.h>
#include <nucleus/version.h>
#ifdef CONFIG_RTAI_OPT_PIPE
#include <nucleus/pipe.h>
#endif /* CONFIG_RTAI_OPT_PIPE */
#ifdef CONFIG_RTAI_OPT_FUSION
#include <nucleus/fusion.h>
#endif /* CONFIG_RTAI_OPT_FUSION */
#include <nucleus/ltt.h>
#include <linux/seq_file.h>

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

struct sched_seq_iterator {
    xnticks_t start_time;
    int nentries;
    struct sched_seq_info {
	int cpu;
	pid_t pid;
	const char *name;
	int cprio;
	xnticks_t timeout;
	xnflags_t status;
    } sched_info[1];
};

static void *sched_seq_start(struct seq_file *seq, loff_t *pos)
{
    struct sched_seq_iterator *iter = (struct sched_seq_iterator *)seq->private;

    if (*pos > iter->nentries)
	return NULL;

    if (*pos == 0)
	return SEQ_START_TOKEN;

    return iter->sched_info + *pos - 1;
}

static void *sched_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
    struct sched_seq_iterator *iter = (struct sched_seq_iterator *)seq->private;

    ++*pos;

    if (v == SEQ_START_TOKEN)
	return &iter->sched_info[0];

    if (*pos > iter->nentries)
	return NULL;

    return iter->sched_info + *pos - 1;
}

static void sched_seq_stop(struct seq_file *seq, void *v)
{
}

static int sched_seq_show(struct seq_file *seq, void *v)
{
    char buf[64];

    if (v == SEQ_START_TOKEN)
	seq_printf(seq,"%-3s   %-6s %-24s %-4s  %-8s  %-8s\n",
		   "CPU","PID","NAME","PRI","TIMEOUT","STATUS");
    else
	{
	struct sched_seq_info *p = (struct sched_seq_info *)v;
	seq_printf(seq,"%3u   %-6d %-24s %-4d  %-8Lu  0x%.8lx - %s\n",
		   p->cpu,
		   p->pid,
		   p->name,
		   p->cprio,
		   p->timeout,
		   p->status,
		   xnthread_symbolic_status(p->status,
					    buf,sizeof(buf)));
	}

    return 0;
}

static struct seq_operations sched_op = {
    .start = &sched_seq_start,
    .next = &sched_seq_next,
    .stop = &sched_seq_stop,
    .show = &sched_seq_show
};

static int sched_seq_open(struct inode *inode, struct file *file)
{
    struct sched_seq_iterator *iter;
    struct seq_file *seq;
    xnholder_t *holder;
    int err, count;
    spl_t s;

    if (!nkpod)
	return -ESRCH;

    count = countq(&nkpod->threadq);	/* Cannot be empty (ROOT) */

    iter = kmalloc(sizeof(*iter)
		   + (count - 1) * sizeof(struct sched_seq_info),
		   GFP_KERNEL);
    if (!iter)
	return -ENOMEM;

    err = seq_open(file, &sched_op);

    if (err)
	{
	kfree(iter);
	return err;
	}

    iter->nentries = 0;

    /* Take a snapshot and release the nucleus lock immediately after,
       so that dumping /proc/rtai/sched with lots of entries won't
       cause massive jittery. */

    xnlock_get_irqsave(&nklock,s);

    iter->start_time = nktimer->get_jiffies();

    for (holder = getheadq(&nkpod->threadq);
	 holder && count > 0;
	 holder = nextq(&nkpod->threadq,holder), count--)
	{
	xnthread_t *thread = link2thread(holder,glink);
	int n = iter->nentries++;

	iter->sched_info[n].cpu = xnsched_cpu(thread->sched);
	iter->sched_info[n].pid = xnthread_user_pid(thread);
	iter->sched_info[n].name = thread->name;
	iter->sched_info[n].cprio = thread->cprio;
	iter->sched_info[n].timeout = xnthread_get_timeout(thread,iter->start_time);
	iter->sched_info[n].status = thread->status;
	}
    
    xnlock_put_irqrestore(&nklock,s);

    seq = (struct seq_file *)file->private_data;
    seq->private = iter;

    return 0;
}

static struct file_operations sched_seq_operations = {
    .owner = THIS_MODULE,
    .open = sched_seq_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = seq_release_private,
};

#ifdef CONFIG_RTAI_OPT_STATS

struct stat_seq_iterator {
    int nentries;
    struct stat_seq_info {
	int cpu;
	pid_t pid;
	const char *name;
	unsigned long psw;
	unsigned long ssw;
	unsigned long csw;
	unsigned long pf;
    } stat_info[1];
};

static void *stat_seq_start(struct seq_file *seq, loff_t *pos)
{
    struct stat_seq_iterator *iter = (struct stat_seq_iterator *)seq->private;

    if (*pos > iter->nentries)
	return NULL;

    if (*pos == 0)
	return SEQ_START_TOKEN;

    return iter->stat_info + *pos - 1;
}

static void *stat_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
    struct stat_seq_iterator *iter = (struct stat_seq_iterator *)seq->private;

    ++*pos;

    if (v == SEQ_START_TOKEN)
	return &iter->stat_info[0];

    if (*pos > iter->nentries)
	return NULL;

    return iter->stat_info + *pos - 1;
}

static void stat_seq_stop(struct seq_file *seq, void *v)
{
}

static int stat_seq_show(struct seq_file *seq, void *v)
{
    if (v == SEQ_START_TOKEN)
	seq_printf(seq,"%-3s   %-6s %-24s     %-12s  %-6s   %-6s\n",
		   "CPU","PID","NAME","MODSW","CSW","PF");
    else
	{
	struct stat_seq_info *p = (struct stat_seq_info *)v;
	seq_printf(seq,"%3u   %-6d %-24s %6lu/%-6lu  %6lu  %6lu\n",
		   p->cpu, p->pid, p->name,
		   p->psw, p->ssw, p->csw, p->pf);
	}

    return 0;
}

static struct seq_operations stat_op = {
    .start = &stat_seq_start,
    .next = &stat_seq_next,
    .stop = &stat_seq_stop,
    .show = &stat_seq_show
};

static int stat_seq_open(struct inode *inode, struct file *file)
{
    struct stat_seq_iterator *iter;
    struct seq_file *seq;
    xnholder_t *holder;
    int err, count;

    if (!nkpod)
	return -ESRCH;

    count = countq(&nkpod->threadq);	/* Cannot be empty (ROOT) */

    iter = kmalloc(sizeof(*iter)
		   + (count - 1) * sizeof(struct stat_seq_info),
		   GFP_KERNEL);
    if (!iter)
	return -ENOMEM;

    err = seq_open(file, &stat_op);

    if (err)
	{
	kfree(iter);
	return err;
	}

    iter->nentries = 0;

    /* Take a snapshot and release the nucleus lock immediately after,
       so that dumping /proc/rtai/stat with lots of entries won't
       cause massive jittery. */

    xnlock_get_irqsave(&nklock,s);

    for (holder = getheadq(&nkpod->threadq);
	 holder && count > 0;
	 holder = nextq(&nkpod->threadq,holder), count--)
	{
	xnthread_t *thread = link2thread(holder,glink);
	int n = iter->nentries++;

	iter->stat_info[n].cpu = xnsched_cpu(thread->sched);
	iter->stat_info[n].pid = xnthread_user_pid(thread);
	iter->stat_info[n].name = thread->name;
	iter->stat_info[n].psw = thread->stat.psw;
	iter->stat_info[n].ssw = thread->stat.ssw;
	iter->stat_info[n].csw = thread->stat.csw;
	iter->stat_info[n].pf = thread->stat.pf;
	}
    
    xnlock_put_irqrestore(&nklock,s);

    seq = (struct seq_file *)file->private_data;
    seq->private = iter;

    return 0;
}

static struct file_operations stat_seq_operations = {
    .owner = THIS_MODULE,
    .open = stat_seq_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = seq_release_private,
};

#endif /* CONFIG_RTAI_OPT_STATS */

static int latency_read_proc (char *page,
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

static int version_read_proc (char *page,
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

static int timer_read_proc (char *page,
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
	status = nktimer->get_type();
	tickval = xnpod_get_tickval();
	jiffies = nktimer->get_jiffies();
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

static struct proc_dir_entry *add_proc_seq (const char *name,
					    struct file_operations *seq_ops,
					    void *data,
					    struct proc_dir_entry *parent)
{
    struct proc_dir_entry *entry;

    entry = create_proc_entry(name,0,parent);

    if (!entry)
	return NULL;

    entry->proc_fops = seq_ops;
    entry->data = data;
    entry->owner = THIS_MODULE;

    return entry;
}

void xnpod_init_proc (void)

{
    if (!rthal_proc_root)
	return;

    add_proc_seq("sched",
		 &sched_seq_operations,
		 NULL,
		 rthal_proc_root);

#ifdef CONFIG_RTAI_OPT_STATS
    add_proc_seq("stat",
		 &stat_seq_operations,
		 NULL,
		 rthal_proc_root);
#endif /* CONFIG_RTAI_OPT_STATS */

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
#ifdef CONFIG_RTAI_OPT_STATS
    remove_proc_entry("stat",rthal_proc_root);
#endif /* CONFIG_RTAI_OPT_STATS */
}

#ifdef CONFIG_RTAI_OPT_FUSION

static int iface_read_proc (char *page,
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
    int err;

    nkmsgbuf = xnarch_sysalloc(XNPOD_FATAL_BUFSZ);

    if (!nkmsgbuf)
	{
	err = -ENOMEM;
	goto fail;
	}

    err = xnarch_init();

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

    if (nkmsgbuf)
	xnarch_sysfree(nkmsgbuf,XNPOD_FATAL_BUFSZ);

    xnloginfo("fusion core stopped.\n");
}

EXPORT_SYMBOL(xnmod_glink_queue);
EXPORT_SYMBOL(xnmod_alloc_glinks);

module_init(__fusion_sys_init);
module_exit(__fusion_sys_exit);
