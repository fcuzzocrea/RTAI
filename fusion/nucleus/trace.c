#include <asm/timex.h>          /* For cpu_khz */
#include <linux/kernel_stat.h>  /* For kstat_irqs */

#include <nucleus/queue.h>
#include <nucleus/heap.h>
#include <nucleus/module.h>
#include <nucleus/pod.h>

#include <nucleus/trace.h>

static xnheap_t rtai_trace_heap;
static xnqueue_t rtai_trace_queue;
static char rtai_trace_heap_mem[32768];
static spinlock_t rtai_trace_lock = SPIN_LOCK_UNLOCKED;
static unsigned long long rtai_trace_start;

/* TODO :
   - configurable "log full" behaviour: stop or override (stops, for the moment);
   - rwlock instead of spinlock to access the traces ;
   - configurable heap memory size ;
   - better context support, see if we are in an interrupt context (use
     inesting for the nucleus), record Adeos domain.
*/

typedef enum rtai_trace_thread_type {
    RTAI_TRACE_THR_KRT,         /* Kernel space real-time */
    RTAI_TRACE_THR_SHADOW,      /* Userspace hard real-time */
    RTAI_TRACE_THR_RELAXED,     /* Userspace real-time running Linux kernel
                                   code. */
    RTAI_TRACE_THR_LINUX,       /* Linux thread/process */
} rtai_trace_thr_type_t;

typedef struct rtai_trace {
    unsigned long long stamp;
    unsigned cpu;
    char thr_name[XNOBJECT_NAME_LEN];
    rtai_trace_thr_type_t thr_type;
    pid_t thr_pid;
    const char *file;
    const char *function;
    int line;

    xnholder_t link;
    char msg [0];
} rtai_trace_t;

#define link2trace(laddr) \
    (rtai_trace_t *) ((char *)(laddr)-offsetof(rtai_trace_t, link))

MODULE_AUTHOR("gilles.chanteperdrix@laposte.net");
MODULE_DESCRIPTION("RTAI tracing facility");
MODULE_LICENSE("GPL");

static inline const char *rtai_trace_basename(const char *filename)
{
    const char *base = filename;
    int previous_was_slash = 0;

    for (;;)
        {
        switch (*filename)
            {
            case '\0':
                if(*base == '/')
                    base = filename;
                return base;
            case '/':
                previous_was_slash=1;
                break;
            default:
                if (previous_was_slash)
                    {
                    previous_was_slash = 0;
                    base = filename;
                    }
            }
        ++filename;
        }
}

static const char *rtai_trace_thr_type2str[] = {
    [RTAI_TRACE_THR_KRT]     = "RT Kernel",
    [RTAI_TRACE_THR_SHADOW]  = "RT User",
    [RTAI_TRACE_THR_RELAXED] = "RT Relax",
    [RTAI_TRACE_THR_LINUX]   = "Linux",
};

void rtai_trace_dump(void)
{
    xnholder_t *holder;
    unsigned count, i;

    count = countq(&rtai_trace_queue);
    printk("Traces: %u events\n", count);
    for(holder = getheadq(&rtai_trace_queue), i = 0; holder && i < count;
        holder = nextq(&rtai_trace_queue, holder), ++i) {
        rtai_trace_t *trace = link2trace(holder);
        unsigned long hrs, mins, secs, nsecs;
        unsigned long long diff_nsecs;

        diff_nsecs = xnarch_tsc_to_ns(trace->stamp - rtai_trace_start);
        secs = (unsigned long) xnarch_ulldiv(diff_nsecs, 1000000000, &nsecs);
        mins = secs / 60;
        hrs = mins / 60;
        secs %= 60;
        mins %= 60;

        printk("#%u/%u %02lu:%02lu:%02lu.%09lu CPU#%d %s(%s, pid=%d) %s: %d: %s:"
               " %s\n",
               i+1,
               count,
               hrs, mins, secs, nsecs,
               trace->cpu,
               trace->thr_name,
               rtai_trace_thr_type2str[trace->thr_type],
               trace->thr_pid,
               rtai_trace_basename(trace->file),
               trace->line,
               trace->function,
               trace->msg);
    }
}


static void rtai_trace_thr_name_cpy(rtai_trace_t *trace, const char *threadname)
{
    char *last_dest = trace->thr_name+sizeof(trace->thr_name)-1;
    const char *src = threadname;
    char *dest = trace->thr_name;

    while ((*dest++ = *src++) != '\0')
        if (dest == last_dest)
            {
            *dest = '\0';
            break;
            }
}

static void rtai_trace_thr_fill(rtai_trace_t *trace)
{
    struct task_struct *task = NULL;
    xnthread_t *thread = NULL;
    char *threadname;

    if(!nkpod || testbits(nkpod->status, XNPIDLE) || xnpod_root_p())
        {
        task = current;
        trace->thr_type = RTAI_TRACE_THR_LINUX;
            
        if (nkpod && !testbits(nkpod->status, XNPIDLE))
            {
            thread = xnshadow_thread(task);
            
            if (thread)
                trace->thr_type = RTAI_TRACE_THR_RELAXED;
            }
        else
            thread = NULL;
        }
    else if (xnpod_shadow_p())
        {
        thread = xnpod_current_sched()->runthread;
        task = xnthread_archtcb(thread)->user_task;

        trace->thr_type = RTAI_TRACE_THR_SHADOW;
        }
    else 
        {
        thread = xnpod_current_thread();            
        trace->thr_type = RTAI_TRACE_THR_KRT;
        }
            
    threadname = ( thread ? xnthread_name(thread)
                   : ( task ? task->comm : "(null)" ));

    rtai_trace_thr_name_cpy(trace, threadname);
    trace->thr_pid = task ? task->pid : -1;
}

static void rtai_trace(const char *f, int l, const char *fn, const char *fmt, ...)
{
    char trace_buffer[64];
    rtai_trace_t *trace;
    size_t size = 0;
    va_list args;
    spl_t s;

    va_start(args, fmt);
    size = vsnprintf(trace_buffer, sizeof(trace_buffer), fmt, args);
    va_end(args);

    adeos_spin_lock_irqsave(&rtai_trace_lock, s);
    trace = (rtai_trace_t *) xnheap_alloc(&rtai_trace_heap,
                                          size+1+sizeof(rtai_trace_t));
    adeos_spin_unlock_irqrestore(&rtai_trace_lock, s);
    if(!trace)
        return;

    rtai_trace_thr_fill(trace);
    trace->file = f;
    trace->function = fn;
    trace->line = l;
    adeos_hw_tsc(trace->stamp);
    inith(&trace->link);

    if(size < sizeof(trace_buffer))
        memcpy(trace->msg, trace_buffer, size+1);
    else
        {
        va_start(args, fmt);
        vsnprintf(trace->msg, size+1, fmt, args);
        va_end(args);
        }

    adeos_spin_lock_irqsave(&rtai_trace_lock, s);
    trace->cpu = xnarch_current_cpu();
    appendq(&rtai_trace_queue, &trace->link);
    adeos_spin_unlock_irqrestore(&rtai_trace_lock, s);
}

#if defined(CONFIG_PROC_FS)

#include <linux/proc_fs.h>

static struct proc_dir_entry *rtai_trace_proc_entry;

static int rtai_trace_read_proc (char *page,
                                 char **start,
                                 off_t off,
                                 int count,
                                 int *eof,
                                 void *data)
{
    unsigned queue_count, i, printed;
    xnholder_t *holder;
    int len, total;
    spl_t s;

    /* we return an unsigned long in "start", so that off is the number of the
       first trace event to be printed. */

    printed = 0;
    total = len = 0;

    adeos_spin_lock_irqsave(&rtai_trace_lock, s);
    queue_count = countq(&rtai_trace_queue);

    if (off > queue_count)
        {
        len = 0;
        *eof = 1;
        goto out;
        }
    
    if (off == 0)
        {
        len = scnprintf(page+total, count-total, "Traces: %u events\n",
                       queue_count);
        if (len >= count-total-1)
            goto out;
        total += len;
        ++printed;
        }
    
    for(holder = getheadq(&rtai_trace_queue), i = 0; holder && i < queue_count;
        holder = nextq(&rtai_trace_queue, holder), ++i) {
        rtai_trace_t *trace = link2trace(holder);
        unsigned long hrs, mins, secs, nsecs;
        unsigned long long diff_nsecs;

        if (i+1 < off)          /* Skip the off-1 first events. */
            continue;
        
        diff_nsecs = xnarch_tsc_to_ns(trace->stamp - rtai_trace_start);
        secs = (unsigned long) xnarch_ulldiv(diff_nsecs, 1000000000, &nsecs);
        mins = secs / 60;
        hrs = mins / 60;
        secs %= 60;
        mins %= 60;

        len = scnprintf(page+total, count-total,
                       "#%u/%u %02lu:%02lu:%02lu.%09lu CPU#%d %s(%s, pid=%d)"
                       " %s: %d: %s: %s\n",
                        i+1,
                        queue_count,
                        hrs, mins, secs, nsecs,
                        trace->cpu,
                        trace->thr_name,
                        rtai_trace_thr_type2str[trace->thr_type],
                        trace->thr_pid,
                        rtai_trace_basename(trace->file),
                        trace->line,
                        trace->function,
                        trace->msg);
        if (len >= count-total-1)
            goto out;
        total += len;
        ++printed;
    }

    *eof=1;
    
out:
    adeos_spin_unlock_irqrestore(&rtai_trace_lock, s);

    if (printed == 0) /* printed only one event, which did not fit in "count"
                         characters, truncate it. */
        {
        printed = 1;
        total += len;
        }

    *(unsigned long *) start = printed;

    return total;
}

void rtai_trace_init_proc (void) {

    rtai_trace_proc_entry = create_proc_read_entry("rtai/traces",
						0444,
						NULL,
						&rtai_trace_read_proc,
						NULL);
}

void rtai_trace_delete_proc (void) {

    remove_proc_entry("rtai/traces",NULL);
}
#endif  /* CONFIG_PROC_FS */

#if CONFIG_X86
#  if CONFIG_X86_LOCAL_APIC
#    define RTAI_TRACE_TIMER_IRQ          RTHAL_APIC_TIMER_IPI
#    define linux_timer_irq_count(cpu) (irq_stat[(cpu)].apic_timer_irqs)
#  else /* !CONFIG_X86_LOCAL_APIC */
#    define RTAI_TRACE_TIMER_IRQ          RTHAL_8254_IRQ
#    define linux_timer_irq_count(cpu) (kstat_cpu(cpu).irqs[RTHAL_8254_IRQ])
#  endif /* CONFIG_X86_LOCAL_APIC */
#  define tsc2ms(timestamp)            rthal_ulldiv((timestamp), cpu_khz, NULL)
#endif /* CONFIG_X86 */

#if CONFIG_IA64
#  define RTAI_TRACE_TIMER_IRQ            RTHAL_TIMER_IRQ
#  define linux_timer_irq_count(cpu)   (kstat_cpu(cpu).irqs[RTHAL_TIMER_IRQ])
#  define tsc2ms(timestamp)            rthal_llimd((timestamp), 1000, \
                                                   local_cpu_data->itc_freq)
#endif /* CONFIG_IA64 */

static adomain_t rtai_trace_watchdog_domain;

static void rtai_trace_watchdog (unsigned irq)

{
    static unsigned long last_timer_irq_count[NR_CPUS] = {[0 ... (NR_CPUS-1)]=0};
    static unsigned long long stall_start[NR_CPUS]= {[0 ... (NR_CPUS-1)] = 0ULL};
    static unsigned long is_stalled[NR_CPUS] = {[0 ... (NR_CPUS-1)] = 0 };
    const unsigned long  stalled_max_time = 2000UL; /* ms */
    unsigned long timer_irq_count, stalled_time;
    unsigned long long timestamp;
    adeos_declare_cpuid;

    adeos_load_cpuid();
    timer_irq_count = linux_timer_irq_count(cpuid);
    
    if (timer_irq_count != last_timer_irq_count[cpuid])
        {
        is_stalled[cpuid] = 0;
        last_timer_irq_count[cpuid] = timer_irq_count;

        goto propagate;
        }

    adeos_hw_tsc(timestamp);

    if (!is_stalled[cpuid])
        {
        is_stalled[cpuid] = 1;
        stall_start[cpuid] = timestamp;

        goto propagate;
        }

    stalled_time = tsc2ms(timestamp - stall_start[cpuid]);
    
    if (stalled_time > stalled_max_time)
        {
        /* Lockup detected. */
        adeos_set_printk_sync(adp_current);
        printk("CPU%d stalled %lu ms.\n", cpuid, stalled_time);
        /* Lock rtai_trace_lock. */
        adeos_spin_lock_disable(&rtai_trace_lock);
        rtai_trace_dump();
        __adeos_dump_state();
        show_stack(NULL,NULL);
        printk("console shuts up ...\n");
        console_silent();
        for(;;)
            safe_halt();
        }

 propagate:
    adeos_propagate_irq(irq);
}

static void rtai_trace_watchdog_domain_entry (int iflag)

{
    if (!iflag)
	goto spin;

    adeos_virtualize_irq(RTAI_TRACE_TIMER_IRQ, &rtai_trace_watchdog, NULL,
                         IPIPE_DYNAMIC_MASK);

    printk(KERN_INFO "Watchdog loaded, timer irq: %d.\n", RTAI_TRACE_TIMER_IRQ);

 spin:

    for (;;)
	adeos_suspend_domain();
}

int rtai_trace_watchdog_init(void)
{
    adattr_t attr;
    
    adeos_init_attr(&attr);
    attr.name = "Watchdog";
    attr.domid = *(int *) "GDTW";
    attr.entry = &rtai_trace_watchdog_domain_entry;
    attr.priority = ADEOS_ROOT_PRI + 150;

    if (adeos_register_domain(&rtai_trace_watchdog_domain,&attr))
	return -EBUSY;

    return 0;
}

void rtai_trace_watchdog_exit(void)
{
    adeos_unregister_domain(&rtai_trace_watchdog_domain);
}

int rtai_trace_init(void)
{
    int rc;
    rc = rtai_trace_watchdog_init();

    if(rc)
        return rc;

    /* Avoid page faults while in RTAI domain. */
    memset(&rtai_trace_heap_mem[0], '\0', sizeof(rtai_trace_heap_mem));
    
    rc = xnheap_init(&rtai_trace_heap, &rtai_trace_heap_mem,
                     sizeof(rtai_trace_heap_mem), 256);
    if(rc)
        return rc;
    
    initq(&rtai_trace_queue);

#ifdef CONFIG_PROC_FS
    rtai_trace_init_proc();
#endif /* CONFIG_PROC_FS */

    adeos_hw_tsc(rtai_trace_start);
    
    rtai_trace_callback = &rtai_trace;

    return 0;
}

void rtai_trace_exit(void)
{
    xnholder_t *holder;
    unsigned count;
    spl_t s;

    rtai_trace_callback = NULL;

#ifdef CONFIG_PROC_FS
    rtai_trace_delete_proc();
#endif /* CONFIG_PROC_FS */

    rtai_trace_watchdog_exit();
    
    adeos_spin_lock_irqsave(&rtai_trace_lock, s);
    rtai_trace_dump();
    
    count = countq(&rtai_trace_queue);
    while((holder = getq(&rtai_trace_queue)) && count--) {
        rtai_trace_t *trace = link2trace(holder);
        xnheap_free(&rtai_trace_heap, trace);
    }
    xnheap_destroy(&rtai_trace_heap, NULL);
    adeos_spin_unlock_irqrestore(&rtai_trace_lock, s);
}

EXPORT_SYMBOL(rtai_trace_dump);

module_init(rtai_trace_init);
module_exit(rtai_trace_exit);
