/**
 *   @file
 *
 *   RTAI/x86 HAL
 *
 *   Copyright &copy; 2000 Paolo Mantegazza <mantegazza@aero.polimi.it>, \n
 *   Copyright &copy; 2000 Steve Papacharalambous <stevep@zentropix.com> (procfs
 *   support), \n
 *   Copyright &copy; 2000 Stuart Hughes <sehughes@zentropix.com> (2.4 port
 *   debugging), \n
 *   and others.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *   @ingroup hal
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/system.h>
#include <asm/hw_irq.h>
#include <asm/smp.h>
#include <asm/io.h>
#include <asm/bitops.h>
#include <asm/atomic.h>
#include <asm/apicdef.h>
#include <asm/delay.h>

#ifdef CONFIG_PROC_FS
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include "rtai_proc_fs.h"
#endif

#ifdef CONFIG_X86_REMOTE_DEBUG
#include <linux/gdb.h>
void (*rtai_gdb_handler)(void) = NULL;
static void rtai_gdb_interrupt(void)
{
    	gdb_interrupt(gdb_irq, NULL, NULL);
}
#endif

#include <rtai.h>
#include <rtai_version.h>
#include <asm/rtai_srq.h>

MODULE_LICENSE("GPL");

/* RTAI mount-unmount functions to be called from the application to       */
/* initialise the real time application interface, i.e. this module, only  */
/* when it is required; so that it can stay asleep when it is not needed   */

// PC: CONFIG_RTAI_MOUNT_ON_LOAD does not work here. I cannot rmmod rtai at
// the end. So I leave the new code in but I revert to the old behavior.
//
//#define CONFIG_RTAI_MOUNT_ON_LOAD
#ifdef CONFIG_RTAI_MOUNT_ON_LOAD
#define rtai_mounted 1
#else
static int rtai_mounted;
#ifdef CONFIG_SMP
static spinlock_t rtai_mount_lock = SPIN_LOCK_UNLOCKED;
#endif
#endif

#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
static int rtai_isr_nesting[NR_RT_CPUS];
static void (*rtai_isr_hook)(int nesting);
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */

int my_cs(void)
{
        int reg;
        __asm__("movl %%cs,%%eax " : "=a" (reg) : );
        return reg;
}

int my_ds(void)
{
        int reg;
        __asm__("movl %%ds,%%eax " : "=a" (reg) : );
        return reg;
}

int my_ss(void)
{
        int reg;
        __asm__("movl %%ss,%%eax " : "=a" (reg) : );
        return reg;
}

#define INTR_ENABLED (1 << IFLAG)

#include <rtai_trace.h>

// proc filesystem additions.
static int rtai_proc_register(void);
static void rtai_proc_unregister(void);
// End of proc filesystem additions.

/* Macros to setup intercept-dispatching of any interrupt, Linux cannot hard */
/* interrupt any more and all those related to IPIs, IOs and reserved traps  */
/* are catched and can be our slaves.                                        */

#define __STR(x) #x
#define STR(x) __STR(x)

// Simple SAVE/RSTR for compilation with the -fomit-frame-pointer option.
// Very simple assembler, support for using the fpu without problem, has been
// taken away for reason of efficiency. To use hard floating point in your 
// interrupt handlers, without any problem, see the fpu support macros 
// available in "rtai.h".

struct fill_t { unsigned long fill[6]; };

#define SAVE_REG(irq) __asm__ __volatile__ (" \
	pushl $"#irq"; cld; pushl %es; pushl %ds; pushl %eax;\n\t \
	pushl %ebp; pushl %ecx; pushl %edx;\n\t \
	movl $" STR(__KERNEL_DS) ",%edx; mov %dx,%ds; mov %dx,%es")

#define RSTR_REG __asm__ __volatile__ (" \
	popl %edx; popl %ecx; testl %eax,%eax; jnz 1f;\n\t \
	popl %ebp; popl %eax; popl %ds; popl %es; addl $4,%esp; iret;\n\t \
	1: cld; pushl %edi; pushl %esi; pushl %edx;\n\t \
	pushl %ecx; pushl %ebx; jmp *"SYMBOL_NAME_STR(rthal))

// Global IO irqs dispatching.
#define GLOBAL_IRQ_FNAME(y) y##_interrupt(void)
#define GLOBAL_IRQ_NAME(y) GLOBAL_IRQ_FNAME(GLOBAL##y)

#ifdef CONFIG_RTAI_TRACE
/****************************************************************************/
/* Trace functions. These functions have to be used rather than insert
the macros as-is. Otherwise the system crashes ... You've been warned. K.Y. */
void trace_true_global_irq_entry(struct fill_t fill, int irq)
{
        TRACE_RTAI_GLOBAL_IRQ_ENTRY(irq,0);
}
void trace_true_global_irq_exit(void)
{
        TRACE_RTAI_GLOBAL_IRQ_EXIT();
}
void trace_true_own_irq_entry(struct fill_t fill, int irq)
{
        TRACE_RTAI_OWN_IRQ_ENTRY(irq);
}
void trace_true_own_irq_exit(void)
{
        TRACE_RTAI_OWN_IRQ_EXIT();
}
void trace_true_trap_entry(int err)
{
        TRACE_RTAI_TRAP_ENTRY(err,0);
}
void trace_true_trap_exit(void)
{
        TRACE_RTAI_TRAP_EXIT();
}
void trace_true_srq_entry(unsigned int srq, unsigned int whatever)
{
        TRACE_RTAI_SRQ_ENTRY(srq);
}
void trace_true_srq_exit(void)
{
        TRACE_RTAI_SRQ_EXIT();
}
/****************************************************************************/
#endif

#ifdef CONFIG_RTAI_TRACE
#define TRACE_ASM_CALL(x) __asm__ __volatile__ ("call " #x )
#define TRACE_ASM_CALL_SAVE_EAX(x) \
	__asm__ __volatile__ ("pushl %eax;\n\tcall " #x ";\n\tpopl %eax;\n\t")
#else
#define TRACE_ASM_CALL(x)
#define TRACE_ASM_CALL_SAVE_EAX(x)
#endif

#define BUILD_GLOBAL_IRQ(irq) static void GLOBAL_IRQ_NAME(irq) \
{ \
	SAVE_REG(irq); \
	TRACE_ASM_CALL(trace_true_global_irq_entry); \
	__asm__ __volatile__ ("call "SYMBOL_NAME_STR(dispatch_global_irq)); \
	TRACE_ASM_CALL_SAVE_EAX(trace_true_global_irq_exit); \
	RSTR_REG; \
}

// Cpu own dispatching.
#define CPU_OWN_IRQ_FNAME(y) y##_interrupt(void)
#define CPU_OWN_IRQ_NAME(y) CPU_OWN_IRQ_FNAME(CPU_OWN##y)

#define BUILD_CPU_OWN_IRQ(irq) static void CPU_OWN_IRQ_NAME(irq) \
{ \
	SAVE_REG(irq); \
	TRACE_ASM_CALL(trace_true_own_irq_entry); \
	__asm__ __volatile__ ("call "SYMBOL_NAME_STR(dispatch_cpu_own_irq)); \
	TRACE_ASM_CALL_SAVE_EAX(trace_true_own_irq_exit); \
	RSTR_REG; \
}

// Reserved traps dispatching - builds a pt_regs structure
#define TRAP_SAVE_REG(vec) __asm__ __volatile__ (" \
        cld; pushl %es; pushl %ds; pushl %eax; pushl %ebp; pushl %edi\n\t \
        pushl %esi; pushl %edx; pushl %ecx; pushl %ebx\n\t \
        movl %esp,%edx; pushl %edx; pushl $"#vec"\n\t \
        movl $" STR(__KERNEL_DS) ",%edx; mov %dx,%ds; mov %dx,%es")

#define TRAP_RSTR_REG(vec) __asm__ __volatile__ (" \
        addl $8,%esp; testl %eax,%eax\n\t \
        popl %ebx; popl %ecx; popl %edx; popl %esi\n\t \
        popl %edi; popl %ebp; movl 8(%esp),%es\n\t \
        movl "SYMBOL_NAME_STR(linux_isr + 4*vec)",%eax\n\t \
        movl %eax,8(%esp); popl %eax; popl %ds\n\t \
        jz 1f; addl $8,%esp; iret; 1: ret")

#define TRAP_RSTR_REG_NOERR(vec) __asm__ __volatile__ (" \
        addl $8,%esp; testl %eax,%eax\n\t \
        popl %ebx; popl %ecx; popl %edx; popl %esi\n\t \
        popl %edi; popl %ebp\n\t \
        movl "SYMBOL_NAME_STR(linux_isr + 4*vec)",%eax\n\t \
        movl %eax,12(%esp); popl %eax; popl %ds; popl %es\n\t \
        jz 1f; addl $4,%esp; iret; 1: ret")

#define TRAP_FNAME(y) TRAP_##y(void)
#define TRAP_NAME(y) TRAP_FNAME(y)

#define BUILD_TRAP(vec) static void TRAP_NAME(vec) \
{ \
        TRAP_SAVE_REG(vec); \
	TRACE_ASM_CALL(trace_true_trap_entry); \
        __asm__ __volatile__ ("call "SYMBOL_NAME_STR(dispatch_trap)); \
	TRACE_ASM_CALL_SAVE_EAX(trace_true_trap_exit); \
        TRAP_RSTR_REG(vec); \
}

#define BUILD_TRAP_NOERR(vec) static void TRAP_NAME(vec) \
{ \
        __asm__ __volatile__ ("pushl $0"); /* pretend err_code */ \
        TRAP_SAVE_REG(vec); \
        __asm__ __volatile__ ("call "SYMBOL_NAME_STR(dispatch_trap)); \
        TRAP_RSTR_REG_NOERR(vec); \
}

// RTAI specific sysreqs dispatching.
static void srqisr(void)
{
	__asm__ __volatile__ (" \
	cld; pushl %es; pushl %ds; pushl %ebp;\n\t \
	pushl %edi; pushl %esi; pushl %ecx;\n\t \
	pushl %ebx; pushl %edx; pushl %eax;\n\t \
	movl $" STR(__KERNEL_DS) ",%ebx; mov %bx,%ds; mov %bx,%es");

	TRACE_ASM_CALL(trace_true_srq_entry);
	__asm__ __volatile__ ("call "SYMBOL_NAME_STR(dispatch_srq));
	TRACE_ASM_CALL_SAVE_EAX(trace_true_srq_exit);

	__asm__ __volatile__ (" \
	addl $8,%esp; popl %ebx; popl %ecx; popl %esi;\n\t \
	popl %edi; popl %ebp; popl %ds; popl %es; iret");
}

/* Setup intercept-dispatching handlers */

BUILD_GLOBAL_IRQ(0)   BUILD_GLOBAL_IRQ(1)   BUILD_GLOBAL_IRQ(2)
BUILD_GLOBAL_IRQ(3)   BUILD_GLOBAL_IRQ(4)   BUILD_GLOBAL_IRQ(5)  
BUILD_GLOBAL_IRQ(6)   BUILD_GLOBAL_IRQ(7)   BUILD_GLOBAL_IRQ(8)
BUILD_GLOBAL_IRQ(9)   BUILD_GLOBAL_IRQ(10)  BUILD_GLOBAL_IRQ(11)
BUILD_GLOBAL_IRQ(12)  BUILD_GLOBAL_IRQ(13)  BUILD_GLOBAL_IRQ(14) 
BUILD_GLOBAL_IRQ(15)  BUILD_GLOBAL_IRQ(16)  BUILD_GLOBAL_IRQ(17) 
BUILD_GLOBAL_IRQ(18)  BUILD_GLOBAL_IRQ(19)  BUILD_GLOBAL_IRQ(20) 
BUILD_GLOBAL_IRQ(21)  BUILD_GLOBAL_IRQ(22)  BUILD_GLOBAL_IRQ(23)
BUILD_GLOBAL_IRQ(24)  BUILD_GLOBAL_IRQ(25)  BUILD_GLOBAL_IRQ(26) 
BUILD_GLOBAL_IRQ(27)  BUILD_GLOBAL_IRQ(28)  BUILD_GLOBAL_IRQ(29) 
BUILD_GLOBAL_IRQ(30)  BUILD_GLOBAL_IRQ(31)

BUILD_CPU_OWN_IRQ(0)  BUILD_CPU_OWN_IRQ(1)  BUILD_CPU_OWN_IRQ(2)
BUILD_CPU_OWN_IRQ(3)  BUILD_CPU_OWN_IRQ(4)  BUILD_CPU_OWN_IRQ(5)
BUILD_CPU_OWN_IRQ(6)  BUILD_CPU_OWN_IRQ(7)  BUILD_CPU_OWN_IRQ(8)
BUILD_CPU_OWN_IRQ(9) 

BUILD_TRAP_NOERR(0)   BUILD_TRAP_NOERR(1)   BUILD_TRAP_NOERR(2)
BUILD_TRAP_NOERR(3)   BUILD_TRAP_NOERR(4)   BUILD_TRAP_NOERR(5)
BUILD_TRAP_NOERR(6)   BUILD_TRAP_NOERR(7)   BUILD_TRAP(8)
BUILD_TRAP_NOERR(9)   BUILD_TRAP(10)        BUILD_TRAP(11)
BUILD_TRAP(12)        BUILD_TRAP(13)        BUILD_TRAP(14)
BUILD_TRAP_NOERR(15)  BUILD_TRAP_NOERR(16)  BUILD_TRAP(17)
BUILD_TRAP_NOERR(18)  BUILD_TRAP_NOERR(19)  BUILD_TRAP(20)
BUILD_TRAP(21)        BUILD_TRAP(22)        BUILD_TRAP(23)
BUILD_TRAP(24)        BUILD_TRAP(25)        BUILD_TRAP(26)
BUILD_TRAP(27)        BUILD_TRAP(28)        BUILD_TRAP(29)
BUILD_TRAP(30)        BUILD_TRAP(31)

/* Some define */

// If a bit of the const below is set the related 8259 controlled irq is just 
// acknowledged, instead of masking and acknowledging. It works and avoids an 
// IO to the slow ISA bus that is worth hundreds of CPU cycles, e.g. almost 
// half of an RTAI scheduler task switching.
static int i8259_irq_type = 0xFFFFFFFF;
MODULE_PARM(i8259_irq_type, "i");

// The three below cannot be > 32 (bits). Even if Linux programmers are selling
// hundreds for them, such a choice is still a long way from its saturation.
// When more will be needed we'll add them in a snap.
#define NR_GLOBAL_IRQS   (NR_IRQS > 32 ? 32 : NR_IRQS)
#define NR_CPU_OWN_IRQS  10
#define NR_TRAPS         RTAI_NR_TRAPS

// Watch out for the macro below, in case Linux developpers use it. No problem
// anyhow, we'll always find one. Let's just remember to take it into account.
#define UNUSED_VECTOR  0x32

// Watch out for the macros below, they must be consistent with what you have 
// in rtai.h.
#define HARD_LOCK_IPI      	RTAI_1_IPI
#define HARD_LOCK_VECTOR      	RTAI_1_VECTOR
#define RTAI_APIC_TIMER_IPI    	RTAI_4_IPI
#define RTAI_APIC_TIMER_VECTOR  RTAI_4_VECTOR
#define APIC_ICOUNT		((FREQ_APIC + HZ/2)/HZ)

// Watch out for the macros below, they must be consistent with the related
// Linux and RTAI vectors used in initializing cpu_own_vector[?] array, a little
// further below. Be carefull to care of them and of the related RTAI IPI
// vectors and numbers defined in rtai.h.
#define INVALIDATE_IPI     0
#define LOCAL_TIMER_IPI    1
#define RESCHEDULE_IPI     2
#define CALL_FUNCTION_IPI  3
#define SPURIOUS_IPI       4
#define APIC_ERROR_IPI     5

#define IRQ_DESC ((irq_desc_t *)rthal.irq_desc)

#define KBRD_IRQ 1

/* Most of our data */

#define FIRST_EXT_VECTOR  FIRST_EXTERNAL_VECTOR
static int global_vector[] = { 
	FIRST_EXT_VECTOR + 0,  FIRST_EXT_VECTOR + 1,  FIRST_EXT_VECTOR + 2,
	FIRST_EXT_VECTOR + 3,  FIRST_EXT_VECTOR + 4,  FIRST_EXT_VECTOR + 5,
	FIRST_EXT_VECTOR + 6,  FIRST_EXT_VECTOR + 7,  FIRST_EXT_VECTOR + 8,
	FIRST_EXT_VECTOR + 9,  FIRST_EXT_VECTOR + 10, FIRST_EXT_VECTOR + 11,
	FIRST_EXT_VECTOR + 12, FIRST_EXT_VECTOR + 13, FIRST_EXT_VECTOR + 14,
	FIRST_EXT_VECTOR + 15, UNUSED_VECTOR,         UNUSED_VECTOR,
	UNUSED_VECTOR,         UNUSED_VECTOR,         UNUSED_VECTOR, 
	UNUSED_VECTOR,         UNUSED_VECTOR,         UNUSED_VECTOR,
	UNUSED_VECTOR,         UNUSED_VECTOR,         UNUSED_VECTOR,
	UNUSED_VECTOR,         UNUSED_VECTOR,         UNUSED_VECTOR,
	UNUSED_VECTOR,         UNUSED_VECTOR };

static void (*global_interrupt[])(void) = {
	GLOBAL0_interrupt,  GLOBAL1_interrupt,  GLOBAL2_interrupt,
	GLOBAL3_interrupt,  GLOBAL4_interrupt,  GLOBAL5_interrupt, 
	GLOBAL6_interrupt,  GLOBAL7_interrupt,  GLOBAL8_interrupt, 
	GLOBAL9_interrupt,  GLOBAL10_interrupt, GLOBAL11_interrupt,
	GLOBAL12_interrupt, GLOBAL13_interrupt, GLOBAL14_interrupt,
	GLOBAL15_interrupt, GLOBAL16_interrupt, GLOBAL17_interrupt,
	GLOBAL18_interrupt, GLOBAL19_interrupt,	GLOBAL20_interrupt,
	GLOBAL21_interrupt, GLOBAL22_interrupt, GLOBAL23_interrupt,
	GLOBAL24_interrupt, GLOBAL25_interrupt, GLOBAL26_interrupt,
	GLOBAL27_interrupt, GLOBAL28_interrupt, GLOBAL29_interrupt,
	GLOBAL30_interrupt, GLOBAL31_interrupt };

static struct global_irq_handling {
	volatile int ext;		
	unsigned long data;
	void (*handler)(void);
} global_irq[NR_GLOBAL_IRQS];

static struct sysrq_t {
	unsigned int label;
	void (*rtai_handler)(void);
	long long (*user_handler)(unsigned int whatever);
} sysrq[NR_GLOBAL_IRQS];

static int cpu_own_vector[NR_CPU_OWN_IRQS] = {
	INVALIDATE_TLB_VECTOR, LOCAL_TIMER_VECTOR,   RESCHEDULE_VECTOR,
	CALL_FUNCTION_VECTOR,  SPURIOUS_APIC_VECTOR, ERROR_APIC_VECTOR,
	RTAI_1_VECTOR,         RTAI_2_VECTOR,        RTAI_3_VECTOR,
	RTAI_4_VECTOR };

static void (*cpu_own_interrupt[NR_CPU_OWN_IRQS])(void) = {
	CPU_OWN0_interrupt, CPU_OWN1_interrupt, CPU_OWN2_interrupt,
	CPU_OWN3_interrupt, CPU_OWN4_interrupt, CPU_OWN5_interrupt,
	CPU_OWN6_interrupt, CPU_OWN7_interrupt, CPU_OWN8_interrupt,
	CPU_OWN9_interrupt };

static struct cpu_own_irq_handling {
	volatile unsigned long dest_status;		
	void (*handler)(void);
} cpu_own_irq[NR_CPU_OWN_IRQS];

static void (*trap_interrupt[NR_TRAPS])(void) = {
        TRAP_0,  TRAP_1,  TRAP_2,  TRAP_3,  TRAP_4,
        TRAP_5,  TRAP_6,  TRAP_7,  TRAP_8,  TRAP_9,
        TRAP_10, TRAP_11, TRAP_12, TRAP_13, TRAP_14,
        TRAP_15, TRAP_16, TRAP_17, TRAP_18, TRAP_19,
        TRAP_20, TRAP_21, TRAP_22, TRAP_23, TRAP_24,
        TRAP_25, TRAP_26, TRAP_27, TRAP_28, TRAP_29,
        TRAP_30, TRAP_31 };

static RT_TRAP_HANDLER rtai_trap_handler[NR_TRAPS];

volatile unsigned long lxrt_hrt_flags;

// The main items to be saved-restored to make Linux our humble slave
static struct rt_hal linux_rthal;
static struct desc_struct linux_idt_table[256];
static void (*linux_isr[256])(void);
static struct hw_interrupt_type *linux_irq_desc_handler[NR_GLOBAL_IRQS];

// Our global and cpu specific control data. We call the latters "cpu_own" while
// in Linux they prefer the term "local".
/* static ???? */ struct global_rt_status global;

// This is defined just to make it available to some other RTAI modules while
// hiding the main global data structure.
volatile unsigned int *locked_cpus = &global.locked_cpus;

#define MAX_IRQS  256
#define MAX_LVEC  (MAX_IRQS + sizeof(unsigned long) - 1)/sizeof(unsigned long)

static struct cpu_own_status {
	volatile unsigned long intr_flag, linux_intr_flag, hvec;
	volatile unsigned long lvec[MAX_LVEC], irqcnt[MAX_IRQS];
	volatile struct task_struct *cur; // Linux task running before RT
} processor[NR_RT_CPUS]; 

#ifdef CONFIG_SMP 
#define VECTRANS(vec) (0xFF - vec)
#else
#define VECTRANS(vec) (vec)
#endif

/* Interrupt descriptor table manipulation. No assembler here. These are    */
/* the base for manipulating Linux interrupt handling without even touching */
/* the kernel.                                                              */

struct desc_struct rt_set_full_intr_vect(unsigned int vector, int type, int dpl, void (*handler)(void))
{
// "dpl" is the descriptor privilege level: 0-highest, 3-lowest.
// "type" is the interrupt type: 14 interrupt (cli), 15 trap (no cli).
struct desc_struct idt_element = rthal.idt_table[vector];

	rthal.idt_table[vector].a = (__KERNEL_CS << 16) | 
					((unsigned int)handler & 0x0000FFFF);
	rthal.idt_table[vector].b = ((unsigned int)handler & 0xFFFF0000) | 
					(0x8000 + (dpl << 13) + (type << 8));
	return idt_element;
}

void rt_reset_full_intr_vect(unsigned int vector, struct desc_struct idt_element)
{
	rthal.idt_table[vector] = idt_element;
	return;
}

// Get the interrupt handler proper.
static inline void *get_intr_handler(unsigned int vector)
{
	return (void *)((rthal.idt_table[vector].b & 0xFFFF0000) | 
			(rthal.idt_table[vector].a & 0x0000FFFF));
}

static inline void set_intr_vect(unsigned int vector, void (*handler)(void))
{
// It should be done as above, in set_full_intr_vect, but we keep what has not
// to be changed as it is already. So we have not to mind of DPL, TYPE and
// __KERNEL_CS. Then let's just change only the offset part of the idt table.

	rthal.idt_table[vector].a = (rthal.idt_table[vector].a & 0xFFFF0000) | 
				           ((unsigned int)handler & 0x0000FFFF);
	rthal.idt_table[vector].b = ((unsigned int)handler & 0xFFFF0000) | 
			               (rthal.idt_table[vector].b & 0x0000FFFF);
}

void *rt_set_intr_handler(unsigned int vector, void (*handler)(void))
{
	void (*saved_handler)(void) = get_intr_handler(vector);
	set_intr_vect(vector, handler);
	return saved_handler;;
}

void rt_reset_intr_handler(unsigned int vector, void (*handler)(void))
{
	set_intr_vect(vector, handler);
}

int rt_get_irq_vec(int irq)
{
	return global_vector[irq];
}

/* A not so few things dependending on being in a UP-SMP configuration */

#ifdef CONFIG_SMP

/**
 * @ingroup hal
 * Send an inter processors message
 *
 * send_ipi_shorthand sends an inter processors message corresponding to @a irq
 * on:
 * - all CPUs if shorthand is equal to @c APIC_DEST_ALLINC;
 * - all but itself if shorthand is equal to @c APIC_DEST_ALLBUT;
 * - itself if shorthand is equal to @c APIC_DEST_SELF.
 *
 * @note Inter processor messages are not identified by an irq number but by the
 * corresponding vector. Such a correspondence is wired internally in RTAI
 * internal tables.
 */
void send_ipi_shorthand(unsigned int shorthand, int irq)
{
	unsigned long flags;
	hard_save_flags_and_cli(flags);
	apic_wait_icr_idle();
	apic_write_around(APIC_ICR, APIC_DEST_LOGICAL | shorthand | cpu_own_vector[irq]);
	hard_restore_flags(flags);
}

/**
 * @ingroup hal
 * Send an inter processors message
 *
 * send_ipi_logical sends an inter processor message to irq on all CPUs defined
 * by @a dest.
 * @param dest is given by an unsigned long corresponding to a bits mask of the
 * CPUs to be sent. It is used for local APICs programmed in flat logical mode,
 * so the max number of allowed CPUs is 8, a constraint that is valid for all
 * functions and data of RTAI. The flat logical mode is set when RTAI is
 * installed by calling rt_mount_rtai(). Linux 2.4.xx needs no more to be
 * reprogrammed has it has adopted the same idea.
 *
 * @note Inter processor messages are not identified by an irq number but by the
 * corresponding vector. Such a correspondence is wired internally in RTAI
 * internal tables.
 */
void send_ipi_logical(unsigned long dest, int irq)
{
	unsigned long flags;
	if ((dest &= cpu_online_map)) {
		hard_save_flags_and_cli(flags);
 		apic_wait_icr_idle();
		apic_write_around(APIC_ICR2, SET_APIC_DEST_FIELD(dest));
		apic_write_around(APIC_ICR, APIC_DEST_LOGICAL | cpu_own_vector[irq]);
		hard_restore_flags(flags);
	}
}

static struct apic_timer_setup_data apic_timer_mode[NR_RT_CPUS];

static inline void setup_periodic_apic(unsigned int count, unsigned int vector)
{
	apic_read(APIC_LVTT);
	apic_write(APIC_LVTT, APIC_LVT_TIMER_PERIODIC | vector);
	apic_read(APIC_TMICT);
	apic_write(APIC_TMICT, count);
}

static inline void setup_oneshot_apic(unsigned int count, unsigned int vector)
{
	apic_read(APIC_LVTT);
	apic_write(APIC_LVTT, vector);
	apic_read(APIC_TMICT);
	apic_write(APIC_TMICT, count);
}

// Hard lock-unlock all cpus except the one that sets the lock; to be used
// to atomically set up critical things affecting both Linux and RTAI.
static long long apic_timers_sync_time;

static void hard_lock_all_handler(void)
{
	int cpuid;
	struct apic_timer_setup_data *p;

	set_bit(cpuid = hard_cpu_id(), &cpu_own_irq[HARD_LOCK_IPI].dest_status);
	rt_spin_lock(&global.hard_lock);
	switch (global.hard_lock_all_service) {
		case 1:
			p = apic_timer_mode + cpuid;
			if (p->mode > 0) {
				while (rdtsc() < apic_timers_sync_time);
				setup_periodic_apic(p->count, RTAI_APIC_TIMER_VECTOR);
				break;
			} else if (!p->mode) {
				while (rdtsc() < apic_timers_sync_time);
				setup_oneshot_apic(p->count, RTAI_APIC_TIMER_VECTOR);
				break;
			}
		case 2:
			setup_oneshot_apic(0, RTAI_APIC_TIMER_VECTOR);
			break;
		case 3:
			setup_periodic_apic(APIC_ICOUNT, LOCAL_TIMER_VECTOR);
			break;
	}
	rt_spin_unlock(&global.hard_lock);
	clear_bit(cpuid, &cpu_own_irq[HARD_LOCK_IPI].dest_status);
}

static inline int hard_lock_all(void)
{
	unsigned long flags;
	flags = rt_global_save_flags_and_cli();
	if (!global.hard_nesting++) {
		global.hard_lock_all_service = 0;
		rt_spin_lock(&global.hard_lock);
		send_ipi_shorthand(APIC_DEST_ALLBUT, HARD_LOCK_IPI);
		while (cpu_own_irq[HARD_LOCK_IPI].dest_status != (cpu_online_map & ~global.locked_cpus));
	}
	return flags;
}

static inline void hard_unlock_all(unsigned long flags)
{
	if (global.hard_nesting > 0) {
		if (!(--global.hard_nesting)) {
			rt_spin_unlock(&global.hard_lock);
			while (cpu_own_irq[HARD_LOCK_IPI].dest_status);
		}
	}
	rt_global_restore_flags(flags);
}

// Broadcast to keep Linux local APIC timers lively when RTAI uses them.
void broadcast_to_local_timers(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags;
	hard_save_flags_and_cli(flags);
        apic_wait_icr_idle();
        apic_write_around(APIC_ICR, APIC_DM_FIXED | APIC_DEST_ALLINC | LOCAL_TIMER_VECTOR);
	hard_restore_flags(flags);
} 

// We used to program these ourselves, down to the hardware. Linux programmers 
// understood they can be usefull, so we are freed from such a burden and use 
// their support directly.

static void trpd_set_affinity(unsigned int, unsigned long);
static unsigned long saved_irq_affinity[NR_GLOBAL_IRQS], irq_affinity_set_by_rtai[NR_GLOBAL_IRQS];

int rt_assign_irq_to_cpu(int irq, unsigned long cpus_mask)
{
	if (smp_num_cpus < 2 || !(cpus_mask = cpus_mask & cpu_online_map) || !(linux_irq_desc_handler[irq]->set_affinity)) {
		return -EINVAL;
	}
	saved_irq_affinity[irq] = rthal.irq_affinity[irq];
	rthal.irq_affinity[irq] = irq_affinity_set_by_rtai[irq] = cpus_mask;
	trpd_set_affinity(irq, cpus_mask);
	return 0;
}

int rt_reset_irq_to_sym_mode(int irq)
{
	if (smp_num_cpus < 2 || !irq_affinity_set_by_rtai[irq] || !saved_irq_affinity[irq] || !(linux_irq_desc_handler[irq]->set_affinity)) {
		return -EINVAL;
	}
	if (rthal.irq_affinity[irq] == irq_affinity_set_by_rtai[irq]) {
		trpd_set_affinity(irq, rthal.irq_affinity[irq] = saved_irq_affinity[irq]);
	}
	return 0;
}

#else

void send_ipi_shorthand(unsigned int shorthand, int irq) { }

void send_ipi_logical(unsigned long dest, int irq) { }

#define setup_periodic_apic(count, vector)

#define setup_oneshot_apic(count, vector)

#define ack_APIC_irq()

static void hard_lock_all_handler(void) { }

static inline unsigned long hard_lock_all(void)
{
	return rt_global_save_flags_and_cli();
}

static inline void hard_unlock_all(unsigned long flags)
{
	rt_global_restore_flags(flags);
}

static void broadcast_to_local_timers(int irq, void *dev_id, struct pt_regs *regs) { }

int rt_assign_irq_to_cpu(int irq, unsigned long cpus_mask) { return 0; }

int rt_reset_irq_to_sym_mode(int irq) { return 0; }

#endif


/* Emulation of Linux interrupt control and interrupt soft delivery.       */

static inline void do_linux_irq(unsigned int vector)
{
	__asm__ __volatile__ ("pushf; push %cs");
	linux_isr[vector]();
}

void rt_do_irq(unsigned int vector)
{
	__asm__ __volatile__ ("pushf; push %cs");
	((void (*)(void))get_intr_handler(vector))();
}

/* Functions to turn Linux interrupt handling from hard to soft. When the    */
/* going gets hard those having it hard get going. No discrimination implied */
/* in the previous statement, we try to keep equal opportunity rights to     */
/* Linux and RTAI, as far as allowed by the prevailing real time activity.   */

#define HVEC_SHIFT  5
#define LVEC_MASK   ((1 << HVEC_SHIFT) - 1)

static void linux_cli(void)
{ 
	processor[hard_cpu_id()].intr_flag = 0;
}

//#define HINT_DIAG_LSTI
//#define HINT_DIAG_LRSTF
//#define HINT_DIAG_TRAPS
//#define HINT_DIAG_ECHO

#ifdef HINT_DIAG_ECHO
#define HINT_DIAG_MSG(x) x
#else
#define HINT_DIAG_MSG(x)
#endif

static void linux_sti(unsigned long noarg, ...)
{
       	struct cpu_own_status *cpu;
       	unsigned long cpuid, hvec, lvec, vec;

#ifdef HINT_DIAG_LSTI
	do {
		unsigned long hflags;
		hard_save_flags(hflags);
		if (!test_bit(IFLAG, &hflags)) {
			processor[hard_cpu_id()].intr_flag = INTR_ENABLED;
			HINT_DIAG_MSG(rt_printk("LINUX STI HAS INTERRUPT DISABLED, EIP = %p.\n", (&noarg)[-1]););
			return;
		}
	} while (0);
#endif

	cpu = processor + (cpuid = hard_cpu_id());
	while (!test_and_set_bit(cpuid, &global.cpu_in_sti)) {
		while ((hvec = cpu->hvec)) {
			clear_bit(hvec = ffnz(hvec), &cpu->hvec);
			while ((lvec = cpu->lvec[hvec])) {
				clear_bit(lvec = ffnz(lvec), &cpu->lvec[hvec]);
				vec = (hvec << HVEC_SHIFT) | lvec;
				while (cpu->irqcnt[vec] > 0) {
					atomic_dec((atomic_t *)&cpu->irqcnt[vec]);
					if ((vec = VECTRANS(vec)) < FIRST_EXT_VECTOR) {
						while (test_bit(vec, &global.pending_srqs) && !test_and_set_bit(vec, &global.activ_srqs)) {
							clear_bit(vec, &global.pending_srqs);
							if (sysrq[vec].rtai_handler) {
								sysrq[vec].rtai_handler();
							}
							clear_bit(vec, &global.activ_srqs);
						}
					} else {
						do_linux_irq(vec);
					}
				}	
				if (cpu->irqcnt[vec] > 0) {
					set_bit(lvec, &cpu->lvec[hvec]);
				}
			}
			if (cpu->lvec[hvec]) {
				set_bit(hvec, &cpu->hvec);
			}
		}
		cpu->intr_flag = INTR_ENABLED;
		clear_bit(cpuid, &global.cpu_in_sti);
		if (!cpu->hvec) {
			return;
		}
	} 
	cpu->intr_flag = INTR_ENABLED;
}

static unsigned int linux_save_flags(void)
{
	return processor[hard_cpu_id()].intr_flag;
}

static void linux_restore_flags(unsigned int flags, ...)
{
	if (flags) {

#ifdef HINT_DIAG_LRSTF
        do {
                unsigned long hflags;
                hard_save_flags(hflags);
                if (!test_bit(IFLAG, &hflags)) {
			processor[hard_cpu_id()].intr_flag = flags ? flags : 0;
                        HINT_DIAG_MSG(rt_printk("LINUX RESTORE FLAGS HAS INTERRUPT DISABLED, EIP = %p.\n", (&flags)[-1]););
			return;
                }
        } while (0);
#endif
		linux_sti(0);
	} else {
		processor[hard_cpu_id()].intr_flag = 0;
	}
}

unsigned int linux_save_flags_and_cli(void)
{
	return xchg(&(processor[hard_cpu_id()].intr_flag), 0);
}

unsigned int linux_save_flags_and_cli_cpuid(int cpuid)  // LXRT specific
{
	return xchg(&(processor[cpuid].intr_flag), 0);
}

void rtai_just_copy_back(unsigned long flags, int cpuid)
{
	processor[cpuid].intr_flag = flags;
}

/* Functions to control Advanced-Programmable Interrupt Controllers (A-PIC). */

// Now Linux has a per PIC spinlock, as it has always been in RTAI. So there is 
// more a need to duplicate them here. Note however that they are not safe since
// interrupts has just soft disabled, so we have to provide the hard cli/sti.
// Moreover we do not want to run Linux_sti uselessly so we clear also the soft
// flag.

static void (*internal_ic_ack_irq[NR_GLOBAL_IRQS]) (unsigned int irq);
static void (*ic_ack_irq[NR_GLOBAL_IRQS]) (unsigned int irq);
static void (*ic_end_irq[NR_GLOBAL_IRQS]) (unsigned int irq);
static void (*linux_end_irq[NR_GLOBAL_IRQS]) (unsigned int irq);

static void do_nothing_picfun(unsigned int irq) { }

unsigned int rt_startup_irq(unsigned int irq)
{
	unsigned long flags, lflags;
	volatile unsigned long *lflagp;
	int retval;
	hard_save_flags_and_cli(flags);
	lflags = xchg(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	retval = linux_irq_desc_handler[irq]->startup(irq);
       	*lflagp = lflags;
	hard_restore_flags(flags);
	return retval;
}

void rt_shutdown_irq(unsigned int irq)
{
	unsigned long flags, lflags;
	volatile unsigned long *lflagp;
	hard_save_flags_and_cli(flags);
	lflags = xchg(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	linux_irq_desc_handler[irq]->shutdown(irq);
       	*lflagp = lflags;
	hard_restore_flags(flags);
}

void rt_enable_irq(unsigned int irq)
{
	unsigned long flags, lflags;
	volatile unsigned long *lflagp;
	hard_save_flags_and_cli(flags);
	lflags = xchg(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	linux_irq_desc_handler[irq]->enable(irq);
       	*lflagp = lflags;
	hard_restore_flags(flags);
}

void rt_disable_irq(unsigned int irq)
{
	unsigned long flags, lflags;
	volatile unsigned long *lflagp;
	hard_save_flags_and_cli(flags);
	lflags = xchg(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	linux_irq_desc_handler[irq]->disable(irq);
       	*lflagp = lflags;
	hard_restore_flags(flags);
}

void rt_mask_and_ack_irq(unsigned int irq)
{
	unsigned long flags, lflags;
	volatile unsigned long *lflagp;
	hard_save_flags_and_cli(flags);
	lflags = xchg(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	ic_ack_irq[irq](irq);
       	*lflagp = lflags;
	hard_restore_flags(flags);
}

void rt_ack_irq(unsigned int irq)
{
	unsigned long flags, lflags;
	volatile unsigned long *lflagp;
	hard_save_flags_and_cli(flags);
	lflags = xchg(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	internal_ic_ack_irq[irq](irq);
       	*lflagp = lflags;
	hard_restore_flags(flags);
}

void rt_unmask_irq(unsigned int irq)
{
	unsigned long flags, lflags;
	volatile unsigned long *lflagp;
	hard_save_flags_and_cli(flags);
	lflags = xchg(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	ic_end_irq[irq](irq);
       	*lflagp = lflags;
	hard_restore_flags(flags);
}

// The functions below are the same as those above, except that we do not need
// to save the hard flags has they have the interrupt bit set for sure.

unsigned int trpd_startup_irq(unsigned int irq)
{
	unsigned long lflags;
	volatile unsigned long *lflagp;
	int retval;
	lflags = xchg(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	hard_cli();
	retval = linux_irq_desc_handler[irq]->startup(irq);
	hard_sti();
       	*lflagp = lflags;
	return retval;
}

void trpd_shutdown_irq(unsigned int irq)
{
	unsigned long lflags;
	volatile unsigned long *lflagp;
	lflags = xchg(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	hard_cli();
	linux_irq_desc_handler[irq]->shutdown(irq);
	hard_sti();
       	*lflagp = lflags;
}

void trpd_enable_irq(unsigned int irq)
{
	unsigned long lflags;
	volatile unsigned long *lflagp;
	lflags = xchg(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	hard_cli();
	linux_irq_desc_handler[irq]->enable(irq);
	hard_sti();
       	*lflagp = lflags;
}

void trpd_disable_irq(unsigned int irq)
{
	unsigned long lflags;
	volatile unsigned long *lflagp;
	lflags = xchg(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	hard_cli();
	linux_irq_desc_handler[irq]->disable(irq);
	hard_sti();
       	*lflagp = lflags;
}

static void trpd_end_irq(unsigned int irq)
{
	unsigned long lflags;
	volatile unsigned long *lflagp;
	lflags = xchg(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	hard_cli();
	linux_end_irq[irq](irq);
	hard_sti();
       	*lflagp = lflags;
}

static void trpd_set_affinity(unsigned int irq, unsigned long mask)
{
	unsigned long lflags;
	volatile unsigned long *lflagp;
	lflags = xchg(lflagp = &(processor[hard_cpu_id()].intr_flag), 0);
	hard_cli();
	linux_irq_desc_handler[irq]->set_affinity(irq, mask);
	hard_sti();
       	*lflagp = lflags;
}

static struct hw_interrupt_type trapped_linux_irq_type = { 
		"RT SPVISD", 
		trpd_startup_irq,
		trpd_shutdown_irq,
		trpd_enable_irq,
		trpd_disable_irq,
		do_nothing_picfun,
		trpd_end_irq,
		trpd_set_affinity };

static struct hw_interrupt_type real_time_irq_type = { 
		"REAL TIME", 
		(unsigned int (*)(unsigned int))do_nothing_picfun,
		do_nothing_picfun,
		do_nothing_picfun,
		do_nothing_picfun,
		do_nothing_picfun,
		do_nothing_picfun,
		(void (*)(unsigned int, unsigned long))do_nothing_picfun };

/* IPI-IO interrupts and traps dispatching.                                 */

#define MAX_PEND_IRQ 10
#define RT_PRINTK(x, y) //rt_printk(x, y)

static int dispatch_global_irq(struct fill_t fill, int irq) __attribute__ ((__unused__));
static int dispatch_global_irq(struct fill_t fill, int irq)
{
	static unsigned long cpuid, kbrdirq = 0;
       	struct cpu_own_status *cpu;
	volatile unsigned long lflags, *lflagp;

	cpu = processor + (cpuid = hard_cpu_id());
	lflags = xchg(lflagp = &cpu->intr_flag, 0);
	if (global_irq[irq].handler) {
		internal_ic_ack_irq[irq](irq);
        	*lflagp = lflags;
#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
		if (rtai_isr_nesting[cpuid]++ == 0 && rtai_isr_hook)
		    rtai_isr_hook(rtai_isr_nesting[cpuid]);
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */
		if (global_irq[irq].ext) {
			if (((int (*)(int, unsigned long))global_irq[irq].handler)(irq, global_irq[irq].data)) {
				hard_cli();
#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
				if (--rtai_isr_nesting[cpuid] == 0 && rtai_isr_hook)
				    rtai_isr_hook(rtai_isr_nesting[cpuid]);
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */
				return 0;
			}
		} else {
			((void (*)(int))global_irq[irq].handler)(irq);
		}
		hard_cli();
		lflags = xchg(lflagp = &processor[cpuid = hard_cpu_id()].intr_flag, 0);
#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
		if (--rtai_isr_nesting[cpuid] == 0 && rtai_isr_hook)
		    rtai_isr_hook(rtai_isr_nesting[cpuid]);
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */
	} else {
		ic_ack_irq[irq](irq);
		if (irq == KBRD_IRQ) {
			kbrdirq = 1;
		} else {
			int vector, shifted;
			vector = VECTRANS(global_vector[irq]);
			if (cpu->irqcnt[vector]++ >= MAX_PEND_IRQ) {
				RT_PRINTK("LINUX EXTERNAL INTERRUPT FLOOD ON VECTOR 0x%x\n", global_vector[irq]);
			}
			shifted = vector >> HVEC_SHIFT;
			set_bit(vector & LVEC_MASK, &cpu->lvec[shifted]);
			set_bit(shifted, &cpu->hvec);
		}
	}
	if (lflags) {
		hard_sti();
		if (test_and_clear_bit(0, &kbrdirq)) {
			do_linux_irq(global_vector[KBRD_IRQ]);
		}
		linux_sti(0);
		return 1;
	}	
        *lflagp = lflags;
	return 0;
}

#define PEND_INVALIDATE_IPI

static int dispatch_cpu_own_irq(struct fill_t fill, unsigned int irq) __attribute__ ((__unused__));

#ifdef CONFIG_SMP

static int dispatch_cpu_own_irq(struct fill_t fill, unsigned int irq)
{
	struct cpu_own_status *cpu;
	int cpuid;
#ifndef PEND_INVALIDATE_IPI
	if (irq != INVALIDATE_IPI)
#endif
	{
		ack_APIC_irq();
	}
	if (cpu_own_irq[irq].handler) {
		cpu_own_irq[irq].handler();
		hard_cli();
		cpu = processor + (cpuid = hard_cpu_id());
	} else {
		cpu = processor + (cpuid = hard_cpu_id());
		if (irq != RESCHEDULE_IPI) {
			int vector, shifted;
			vector = VECTRANS(cpu_own_vector[irq]);
			if (cpu->irqcnt[vector]++ >= MAX_PEND_IRQ) {
				RT_PRINTK("LINUX IPI INTERRUPT FLOOD ON VECTOR 0x%x\n", cpu_own_vector[irq]);
			}
			shifted = vector >> HVEC_SHIFT;
			set_bit(vector & LVEC_MASK, &cpu->lvec[shifted]);
			set_bit(shifted, &cpu->hvec);
		}
	}
	if (cpu->intr_flag) {
		hard_sti();
		linux_sti(0);
		return 1;
	}
	return 0;
}
#else
int dispatch_cpu_own_irq(struct fill_t fill, unsigned int irq) { return 0; }
#endif

void rt_grab_linux_traps(void)
{
	unsigned long flags, i;

	flags = hard_lock_all();
// Note: Set dpl at 3 below to enable int $1 from user space
#if 0	// Original before Ian changed it at Pierre's request
	for (i = 0; i < NR_TRAPS ; i++) {
		if (i != 14)
		rt_set_full_intr_vect(i, 15, 0, trap_interrupt[i]);
        }
#else
	for (i = 0; i < NR_TRAPS; i++) {
#if 0
		if (i == 2) { 	
		    	// Is NMI a trap (no cli), or an interrupt (with cli)? 
			rt_set_full_intr_vect(i, 15/*14*/, 0, trap_interrupt[i]);
		} else if (i >= 3 && i <= 5) {
		    	// These can come from user space, so set DPL to 3
			rt_set_full_intr_vect(i, 15, 3, trap_interrupt[i]);
		} else {
		    	// The rest are regular kernel only traps
			rt_set_full_intr_vect(i, 15, 0, trap_interrupt[i]);
		}
#else
		rt_set_intr_handler(i, trap_interrupt[i]);
#endif
	}
#endif
	hard_unlock_all(flags);
}

void rt_release_linux_traps(void)
{
	unsigned long flags, i;

	flags = hard_lock_all();
	for (i = 0; i < NR_TRAPS ; i++) {
		rthal.idt_table[i] = linux_idt_table[i];
        }
	hard_unlock_all(flags);
}

// Trap vector to signal mapping
static int rtai_signr[NR_TRAPS] = {
    	SIGFPE,         //  0 - Divide error
	SIGTRAP,        //  1 - Debug
	SIGSEGV,        //  2 - NMI (but we ignore these)
	SIGTRAP,        //  3 - Software breakpoint
	SIGSEGV,        //  4 - Overflow
	SIGSEGV,        //  5 - Bounds
	SIGILL,         //  6 - Invalid opcode
	SIGSEGV,        //  7 - Device not available
	SIGSEGV,        //  8 - Double fault
	SIGFPE,         //  9 - Coprocessor segment overrun
	SIGSEGV,        // 10 - Invalid TSS
	SIGBUS,         // 11 - Segment not present
	SIGBUS,         // 12 - Stack segment
	SIGSEGV,        // 13 - General protection fault
	SIGSEGV,        // 14 - Page fault
	0,              // 15 - Spurious interrupt
	SIGFPE,         // 16 - Coprocessor error
	SIGBUS,         // 17 - Alignment check
	SIGSEGV,        // 18 - Reserved
	SIGFPE,         // 19 - XMM fault
	0,0,0,0,0,0,0,0,0,0,0,0
};

// Note that we trap the NMI, so that it does nothing, without touching the
// kernel madnesses, till we'll see how the related story will end in the
// stable releases.

//#define TRAP_DEBUG

static unsigned long traps_in_hard_intr;

static int dispatch_trap(int vec, struct pt_regs *regs)  __attribute__ ((__unused__));
static int dispatch_trap(int vec, struct pt_regs *regs)
{
	if (vec == 2) return 1;	// Ignore it, NMI trap handler is not RTAI safe

#ifdef HINT_DIAG_TRAPS
        do {
		unsigned long flags;
		hard_save_flags_and_cli(flags);
		if (!test_and_set_bit(IFLAG, &flags)) {
			processor[hard_cpu_id()].intr_flag = 0;
                        if (!test_and_set_bit(vec, &traps_in_hard_intr)) {
				HINT_DIAG_MSG(rt_printk("TRAP %d HAS INTERRUPT DISABLED (TRAPS PICTURE %lx).\n", vec, traps_in_hard_intr););
			}
		}
		hard_restore_flags(flags);
	} while (0);
#endif

//	return 0;

	if (test_bit(hard_cpu_id(), &global.used_by_linux) &&
	   !test_bit(hard_cpu_id(), &lxrt_hrt_flags)) {
                return 0;	// Not RTAI, let Linux handle it
        }

#ifdef CONFIG_X86_REMOTE_DEBUG
	if (test_bit(hard_cpu_id(), &global.used_by_gdbstub)) {
	    	return 0;	// Gdbstub is guilty so let him sort it out
	}

	// Check if kernel gdbstub has been activated
	if (linux_debug_hook) {

	    	// Make sure global gdb IRQ wrapper is in place
		if ((rtai_gdb_handler == NULL) &&
	    	    (rt_request_global_irq(gdb_irq, rtai_gdb_interrupt) == 0)) {
		    	rt_enable_irq(gdb_irq);
			rtai_gdb_handler = rtai_gdb_interrupt;
		}

		// Pass trap onto gdbstub
	    	set_bit(hard_cpu_id(), &global.used_by_gdbstub);
		(*linux_debug_hook)(vec, rtai_signr[vec], regs->orig_eax, regs);
		clear_bit(hard_cpu_id(), &global.used_by_gdbstub);

		// Breakpoints will have been handled by gdbstub
		if (vec == 1 || vec == 3) return 1;
	}
#endif

#ifdef TRAP_DEBUG
        rt_printk("RTAI kernel trap %d\n", vec);
        rt_printk("ERR=%08lx ", regs->orig_eax);
        rt_printk(" IP=%08lx ", regs->eip);
        rt_printk(" CS=%08x ",  regs->xcs & 0xffff);
        rt_printk(" FL=%08lx ", regs->eflags);
        rt_printk("\n");
    if (user_mode(regs)) {
	rt_printk("ESP=%08lx ", regs->esp);
	rt_printk(" SS=%08x ",  regs->xss & 0xffff);
    } else {
	rt_printk("ESP=%08lx ", (long)(&regs->esp));
	rt_printk(" SS=%08x ",  my_ss() & 0xffff);
    }
        rt_printk(" DS=%08x ",  regs->xds & 0xffff);
        rt_printk("CPU=%d ",    hard_cpu_id());
        rt_printk("\n");
#endif
	// Check for RTAI trap handler
      	if (rtai_trap_handler[vec]) {
		return rtai_trap_handler[vec](vec, rtai_signr[vec], regs, NULL);
	}
	return 0; 		// Better hope Linux can handle it
}

// For now we set all traps to the same handler as it is easier for the handler
// (usually in the scheduler) to do the decoding. But having individual trap
// handlers could be useful in the future so it's probably a good idea to keep
// the table.

RT_TRAP_HANDLER rt_set_rtai_trap_handler(RT_TRAP_HANDLER handler)
{
        int i;
        RT_TRAP_HANDLER old_handler;

        old_handler = rtai_trap_handler[0];
        for (i = 0; i < NR_TRAPS; i++) {
                rtai_trap_handler[i] = handler;
        }
        return old_handler;
}

long long dispatch_srq(unsigned int srq, unsigned int whatever)
{

	if (srq > 1 && srq < NR_GLOBAL_IRQS && sysrq[srq].user_handler) {
		return sysrq[srq].user_handler(whatever);
	}
	for (srq = 2; srq < NR_GLOBAL_IRQS; srq++) {
		if (sysrq[srq].label == whatever) {
			return (long long)srq;
		}
	}
	return 0;
}

/* Request and free interrupts, system requests and interprocessors messages */
/* Request for regular Linux irqs also included. They are nicely chained to  */
/* Linux, forcing sharing with any already installed handler, so that we can */
/* have an echo from Linux for global handlers. We found that usefull during */
/* debug, but can be nice for a lot of other things, e.g. see the jiffies    */
/* recovery in rtai_sched.c, and the global broadcast to local apic timers.  */

static unsigned long irq_action_flags[NR_GLOBAL_IRQS];
static int chained_to_linux[NR_GLOBAL_IRQS];

/**
 * @ingroup hal
 * Install IT service routine.
 *
 * rt_request_global_irq installs function handler as a real time interrupt
 * service routine for IRQ level @a irq, eventually stealing it to Linux.
 *
 * @param handler is then invoked whenever interrupt number irq occurs.   The
 * installed handler must take care of properly activating any Linux handler
 * using the same irq number it stole, by calling rt_pend_linux_irqr().
 *
 * @retval 0 on success.
 * @retval EINVAL if @a irq is not a valid IRQ number or handler is NULL.
 * @retval EBUSY  if there is already a handler of interrupt @a irq.
 */
int rt_request_global_irq(unsigned int irq, void (*handler)(void))
{
	unsigned long flags;

	if (irq >= NR_GLOBAL_IRQS || !handler) {
		return -EINVAL;
	}
	if (global_irq[irq].handler) {
		return -EBUSY;
	}
	
	flags = hard_lock_all();
	IRQ_DESC[irq].handler = &real_time_irq_type;
	global_irq[irq].handler = handler;
	linux_end_irq[irq] = do_nothing_picfun;
	hard_unlock_all(flags);
//	hard_save_flags(flags);
	return 0;
}

int rt_request_global_irq_ext(unsigned int irq, void (*handler)(void), unsigned long data)
{
	int ret;
	if (!(ret = rt_request_global_irq(irq, handler))) {
		global_irq[irq].ext = 1;
		global_irq[irq].data = data;
		return 0;
	}
	return ret;
}

void rt_set_global_irq_ext(unsigned int irq, int ext, unsigned long data)
{
	global_irq[irq].ext = ext ? 1 : 0;
	global_irq[irq].data = data;
}

/**
 * @ingroup hal
 * Uninstall IT service routine.
 *
 * rt_free_global_irq uninstalls the interrupt service routine, resetting it for
 * Linux if it was previously owned by the kernel.
 *
 * @retval 0 on success.
 * @retval EINVAL if @a irq is not a valid IRQ number.
 */
int rt_free_global_irq(unsigned int irq)
{
	unsigned long flags;

	if (irq >= NR_GLOBAL_IRQS || !global_irq[irq].handler) {
		return -EINVAL;
	}

	flags = hard_lock_all();
	IRQ_DESC[irq].handler = &trapped_linux_irq_type;
	global_irq[irq].handler = 0; 
	global_irq[irq].ext   = 0; 
	linux_end_irq[irq] = ic_end_irq[irq];
	hard_unlock_all(flags);
	return 0;
}

int rt_request_linux_irq(unsigned int irq,
	void (*linux_handler)(int irq, void *dev_id, struct pt_regs *regs), 
	char *linux_handler_id, void *dev_id)
{
	unsigned long flags, lflags;

	if (irq >= NR_GLOBAL_IRQS || !linux_handler) {
		return -EINVAL;
	}
	lflags = linux_save_flags_and_cli_cpuid(hard_cpu_id());
	spin_lock_irqsave(&(IRQ_DESC[irq].lock), flags);
	if (!chained_to_linux[irq]++) {
		if (IRQ_DESC[irq].action) {
			irq_action_flags[irq] = IRQ_DESC[irq].action->flags;
			IRQ_DESC[irq].action->flags |= SA_SHIRQ;
		}
	}
	spin_unlock_irqrestore(&(IRQ_DESC[irq].lock), flags);
	request_irq(irq, linux_handler, SA_SHIRQ, linux_handler_id, dev_id);
	rtai_just_copy_back(lflags, hard_cpu_id());
	return 0;
}

int rt_free_linux_irq(unsigned int irq, void *dev_id)
{
	unsigned long flags, lflags;

	if (irq >= NR_GLOBAL_IRQS || !chained_to_linux[irq]) {
		return -EINVAL;
	}
	lflags = linux_save_flags_and_cli_cpuid(hard_cpu_id());
	free_irq(irq, dev_id);
	spin_lock_irqsave(&(IRQ_DESC[irq].lock), flags);
	if (!(--chained_to_linux[irq]) && IRQ_DESC[irq].action) {
		IRQ_DESC[irq].action->flags = irq_action_flags[irq];
	}
	spin_unlock_irqrestore(&(IRQ_DESC[irq].lock), flags);
	rtai_just_copy_back(lflags, hard_cpu_id());
	return 0;
}

void rt_pend_linux_irq(unsigned int irq)
{
       	struct cpu_own_status *cpu;
	int vector, shifted;

	cpu = processor + hard_cpu_id();
	vector = VECTRANS(global_vector[irq]);
	atomic_inc((atomic_t *)&cpu->irqcnt[vector]);
	shifted = vector >> HVEC_SHIFT;
	set_bit(vector & LVEC_MASK, &cpu->lvec[shifted]);
	set_bit(shifted, &cpu->hvec);
}

static struct desc_struct svdidt;

int rt_request_srq(unsigned int label, void (*rtai_handler)(void), long long (*user_handler)(unsigned int whatever))
{
	unsigned long flags;
	int srq;

	flags = rt_spin_lock_irqsave(&global.data_lock);
	if (!rtai_handler) {
		rt_spin_unlock_irqrestore(flags, &global.data_lock);
		return -EINVAL;
	}
	for (srq = 2; srq < NR_GLOBAL_IRQS; srq++) {
		if (!(sysrq[srq].rtai_handler)) {
			sysrq[srq].rtai_handler = rtai_handler;
			sysrq[srq].label = label;
			if (user_handler) {
				sysrq[srq].user_handler = user_handler;
				if (!svdidt.a && !svdidt.b) {
					svdidt = rt_set_full_intr_vect(RTAI_SYS_VECTOR, 15, 3, srqisr);
				}
			}
			rt_spin_unlock_irqrestore(flags, &global.data_lock);
			return srq;
		}
	}
	rt_spin_unlock_irqrestore(flags, &global.data_lock);
	return -EBUSY;
}

int rt_free_srq(unsigned int srq)
{
	unsigned long flags;

	flags = rt_spin_lock_irqsave(&global.data_lock);
	if (srq < 2 || srq >= NR_GLOBAL_IRQS || !sysrq[srq].rtai_handler) {
		rt_spin_unlock_irqrestore(flags, &global.data_lock);
		return -EINVAL;
	}
	sysrq[srq].rtai_handler = 0; 
	sysrq[srq].user_handler = 0; 
	sysrq[srq].label = 0;
	for (srq = 2; srq < NR_GLOBAL_IRQS; srq++) {
		if (sysrq[srq].user_handler) {
			rt_spin_unlock_irqrestore(flags, &global.data_lock);
			return 0;
		}
	}
	if (svdidt.a || svdidt.b) {
		rt_reset_full_intr_vect(RTAI_SYS_VECTOR, svdidt);
		svdidt.a = svdidt.b = 0;
	}
	rt_spin_unlock_irqrestore(flags, &global.data_lock);
	return 0;
}

void rt_pend_linux_srq(unsigned int srq)
{
	if (!test_and_set_bit(srq, &global.pending_srqs)) { 
       		struct cpu_own_status *cpu;
		int vector, shifted;
		cpu = processor + hard_cpu_id();
		vector = VECTRANS(srq);
		cpu->irqcnt[vector] = 1;
		shifted = vector >> HVEC_SHIFT;
		set_bit(vector & LVEC_MASK, &cpu->lvec[shifted]);
		set_bit(shifted, &cpu->hvec);
	}
	return;
}

static spinlock_t cpu_own_lock = SPIN_LOCK_UNLOCKED;

int rt_request_cpu_own_irq(unsigned int irq, void (*handler)(void))
{
	unsigned long flags;
	if (irq >= NR_CPU_OWN_IRQS || !handler) {
		return -EINVAL;
	}
	flags = rt_spin_lock_irqsave(&cpu_own_lock);
	if (cpu_own_irq[irq].handler) {
		rt_spin_unlock_irqrestore(flags, &cpu_own_lock);
		return -EBUSY;
	}
	cpu_own_irq[irq].dest_status = 0;
	cpu_own_irq[irq].handler = handler;
	rt_spin_unlock_irqrestore(flags, &cpu_own_lock);
	return 0;
}

int rt_free_cpu_own_irq(unsigned int irq)
{
	unsigned long flags;
	flags = rt_spin_lock_irqsave(&cpu_own_lock);
	if (irq >= NR_CPU_OWN_IRQS || !cpu_own_irq[irq].handler) {
		rt_spin_unlock_irqrestore(flags, &cpu_own_lock);
		return -EINVAL;
	}
	cpu_own_irq[irq].dest_status = 0;
	cpu_own_irq[irq].handler = 0;
	rt_spin_unlock_irqrestore(flags, &cpu_own_lock);
	return 0;
}

// Note: we could easily inline the next five functions in rtai.h
int rt_is_lxrt(void)
{
        return test_bit(hard_cpu_id(), &lxrt_hrt_flags);
}

int rt_is_linux(void)
{
        return test_bit(hard_cpu_id(), &global.used_by_linux);
}

void rt_switch_to_linux(int cpuid)
{
	TRACE_RTAI_SWITCHTO_LINUX(cpuid);
	set_bit(cpuid, &global.used_by_linux);
	processor[cpuid].intr_flag = processor[cpuid].linux_intr_flag;
	processor[cpuid].cur = 0 ;

}

struct task_struct *rt_whoislinux(int cpuid)
{
	// Returns current before the last context switch to RTAI.	
	// If current changed while in RTAI mode the box is in big trouble...
	return (struct task_struct *) processor[cpuid].cur;
}

void rt_switch_to_real_time(int cpuid)
{
	TRACE_RTAI_SWITCHTO_RT(cpuid);
	processor[cpuid].cur = current;
	processor[cpuid].linux_intr_flag = xchg(&(processor[hard_cpu_id()].intr_flag), 0);
	clear_bit(cpuid, &global.used_by_linux);
}


// Trivial, but we do things carefully, the blocking part is relatively short,
// should cause no troubles in the transition phase. It is simple, just block
// other processors and grab everything from Linux. To this aim use a dedicated
// HARD_LOCK_IPI and its vector, setting them up without any protection.

void __rt_mount_rtai(void)
{
	static void rt_printk_sysreq_handler(void);
     	unsigned long flags, i;

	cpu_own_irq[HARD_LOCK_IPI].handler = hard_lock_all_handler;
	set_intr_vect(cpu_own_vector[HARD_LOCK_IPI], cpu_own_interrupt[HARD_LOCK_IPI]);
#ifndef PEND_INVALIDATE_IPI
	cpu_own_irq[INVALIDATE_IPI].handler = rthal.smp_invalidate_interrupt;
	set_intr_vect(cpu_own_vector[INVALIDATE_IPI], cpu_own_interrupt[INVALIDATE_IPI]);
#endif

	flags = hard_lock_all();
	rthal.disint           = linux_cli;
	rthal.enint            = (void *)linux_sti;
	rthal.getflags 	       = linux_save_flags;
	rthal.setflags 	       = (void *)linux_restore_flags;
	rthal.getflags_and_cli = linux_save_flags_and_cli;

	for (i = 0; i < NR_GLOBAL_IRQS; i++) {
		IRQ_DESC[i].handler = &trapped_linux_irq_type;
		set_intr_vect(global_vector[i], global_interrupt[i]); 
	}
// We have to decide if we should leave the invalidate interrupt as it is, i.e.
// send directly to its handler without dispatching it the soft way. Previous 
// RTAI experience with 2.2.xx says such a way should be OK. Let's see what 
// happens under 2.4.xx, Linux has changed somewhat the way it menages 
// tlb invalidates and maybe dispatching them the soft way will be safe.
	for (i = 0; i < NR_CPU_OWN_IRQS; i++) {
#ifndef PEND_INVALIDATE_IPI
		if (i != INVALIDATE_IPI)
#endif
		set_intr_vect(cpu_own_vector[i], cpu_own_interrupt[i]);
	}
	hard_unlock_all(flags);

	printk("\n***** RTAI NEWLY MOUNTED (MOUNT COUNT %d) ******\n\n", rtai_mounted);

	sysrq[1].rtai_handler = rt_printk_sysreq_handler;
	rt_grab_linux_traps();

#ifdef CONFIG_X86_REMOTE_DEBUG
// Check if we need to install our own global IRQ wrapper around the gdb
// interrupt handler. This is done so that it is always possible to break
// into RT code from a debugger even if it has gone into an infinite loop.
	if (linux_debug_hook != NULL && rtai_gdb_handler == NULL) {
	    	if (rt_request_global_irq(gdb_irq, rtai_gdb_interrupt) == 0) {
		    	rt_enable_irq(gdb_irq);
			rtai_gdb_handler = rtai_gdb_interrupt;
		}
	}
#endif
}

// To unmount we simply block other processors and copy original data back to
// Linux. The HARD_LOCK_IPI is the last one to be reset.
void __rt_umount_rtai(void)
{
	int i;
	unsigned long flags;

	linux_sti(0);
	flags = hard_lock_all();
	rthal = linux_rthal;
	for (i = 0; i < NR_GLOBAL_IRQS; i++) {
		IRQ_DESC[i].handler = linux_irq_desc_handler[i];
	}
	for (i = 0; i < 256; i++) {
		rthal.idt_table[i] = linux_idt_table[i];
	}
	cpu_own_irq[HARD_LOCK_IPI].handler = 0;
	cpu_own_irq[HARD_LOCK_IPI].dest_status = 0;
#ifndef PEND_INVALIDATE_IPI
	cpu_own_irq[INVALIDATE_IPI].handler = 0;
	cpu_own_irq[INVALIDATE_IPI].dest_status = 0;
#endif
	hard_unlock_all(flags);
#ifdef CONFIG_X86_REMOTE_DEBUG
	rtai_gdb_handler = NULL;
#endif
	printk("\n***** RTAI UNMOUNTED (MOUNT COUNT %d) ******\n\n", rtai_mounted);
}

#ifdef CONFIG_RTAI_MOUNT_ON_LOAD
void rt_mount_rtai(void)  {MOD_INC_USE_COUNT;}
void rt_umount_rtai(void) {MOD_DEC_USE_COUNT;}
#else
/**
 * @ingroup hal
 * Initialize real time application interface
 *
 * rt_mount_rtai initializes the real time application interface, i.e. grabs
 * anything related to the hardware, data or service, pointed by at by the Real
 * Time Hardware Abstraction Layer RTHAL(struct rt_hal rthal;).
 *
 * @note When you do insmod rtai RTAI is not active yet, it needs to be
 * specifically switched on by calling rt_mount_rtai.
 */
void rt_mount_rtai(void)
{
	rt_spin_lock(&rtai_mount_lock);
	rtai_mounted++;
	MOD_INC_USE_COUNT;
	TRACE_RTAI_MOUNT();
	if (rtai_mounted == 1) {
		__rt_mount_rtai();
	}
	rt_spin_unlock(&rtai_mount_lock);
}

/**
 * @ingroup hal
 * Uninitialize real time application interface
 *
 * rt_umount_rtai unmounts the real time application interface resetting Linux
 * to its normal state.
 */
void rt_umount_rtai(void)
{
	rt_spin_lock(&rtai_mount_lock);
	rtai_mounted--;

	TRACE_RTAI_UMOUNT();

	MOD_DEC_USE_COUNT;
	if (!rtai_mounted) {
		__rt_umount_rtai();
	}
	rt_spin_unlock(&rtai_mount_lock);
}
#endif

// Module parameters to allow frequencies to be overriden via insmod
static unsigned long CpuFreq = CALIBRATED_CPU_FREQ;
MODULE_PARM(CpuFreq, "i");
#ifdef CONFIG_SMP
static int ApicFreq = CALIBRATED_APIC_FREQ;
MODULE_PARM(ApicFreq, "i");
#endif

/* module init-cleanup */

// Let's prepare our side without any problem, so that there remain just a few
// things to be done when mounting RTAI. All the zeroings are strictly not 
// required, as mostly related to static data. Done esplicitly for emphasis.
int init_module(void)
{
     	unsigned int i;

	// Passed in CPU frequency overides auto detected Linux value
	if (CpuFreq == 0) {
		extern unsigned long cpu_khz;

		if (cpu_has_tsc) {
		    	CpuFreq = 1000 * cpu_khz;
		} else {
		    	CpuFreq = FREQ_8254;
		}
	}
	tuned.cpu_freq = CpuFreq;

#ifdef CONFIG_SMP
	// Passed in APIC frequency overides auto detected value
	if (smp_num_cpus > 1) {
	    	if (ApicFreq == 0) {
		    	ApicFreq = apic_read(APIC_TMICT) * HZ;
		}
		tuned.apic_freq = ApicFreq;
	}
#endif
	linux_rthal = rthal;
#ifndef CONFIG_RTAI_MOUNT_ON_LOAD
	rtai_mounted = 0;
#endif
       	global.pending_srqs  = 0;
       	global.activ_srqs    = 0;
       	global.cpu_in_sti    = 0;
	global.used_by_linux = ~(0xFFFFFFFF << smp_num_cpus);
	global.locked_cpus   = 0;
	global.hard_nesting  = 0;
      	spin_lock_init(&(global.data_lock));
      	spin_lock_init(&(global.hard_lock));
	for (i = 0; i < NR_RT_CPUS; i++) {
		processor[i].intr_flag       =
		processor[i].linux_intr_flag = INTR_ENABLED;
	}
	for (i = 0; i < NR_CPU_OWN_IRQS; i++) {
		cpu_own_irq[i].dest_status = 0;
		cpu_own_irq[i].handler     = 0;
	}
        for (i = 0; i < NR_TRAPS; i++) {
                rtai_trap_handler[i] = 0;
        }
	for (i = 0; i < 256; i++) {
		linux_idt_table[i] = rthal.idt_table[i];
		linux_isr[i] = get_intr_handler(i);
	}
	for (i = 0; i < NR_GLOBAL_IRQS; i++) {
		global_irq[i].handler = 0;
		global_irq[i].ext  = 0;
		sysrq[i].rtai_handler = 0;
		sysrq[i].user_handler = 0;
		sysrq[i].label        = 0;
		linux_irq_desc_handler[i] = IRQ_DESC[i].handler;

		ic_ack_irq[i] = internal_ic_ack_irq[i] =
		linux_end_irq[i] = ic_end_irq[i] = do_nothing_picfun;
		if (IRQ_DESC[i].handler->typename[0] == 'I') {
			global_vector[i] = rthal.irq_vector[i];
			ic_ack_irq[i] = internal_ic_ack_irq[i] = 
						linux_irq_desc_handler[i]->ack;
			linux_end_irq[i] = ic_end_irq[i] =
						linux_irq_desc_handler[i]->end;
		} else if (IRQ_DESC[i].handler->typename[0] == 'X') {
			ic_ack_irq[i] = linux_irq_desc_handler[i]->ack;
			linux_end_irq[i] = ic_end_irq[i] =
						linux_irq_desc_handler[i]->end;
			internal_ic_ack_irq[i] = i8259_irq_type & (1 << i) ?
					rthal.ack_8259_irq : ic_ack_irq[i];
		}
	}

#ifdef CONFIG_RTAI_MOUNT_ON_LOAD
	__rt_mount_rtai();
#endif

#ifdef CONFIG_PROC_FS
        rtai_proc_register();
#endif

	return 0;
}

static int trashed_local_timers_ipi, used_local_apic;

void cleanup_module(void)
{

#ifdef CONFIG_RTAI_MOUNT_ON_LOAD
	__rt_umount_rtai();
#endif

#ifdef CONFIG_PROC_FS
        rtai_proc_unregister();
#endif

	if (traps_in_hard_intr) {
		printk("***** MASK OF TRAPS CALLED WITH HARD INTR %lx *****\n", traps_in_hard_intr);
	}

	if (used_local_apic) {
		printk("***** TRASHED LOCAL APIC TIMERS IPIs: %d *****\n", trashed_local_timers_ipi);
	}
	udelay(1000000/HZ);
	return;
}


/* ----------------------< proc filesystem section >----------------------*/
#ifdef CONFIG_PROC_FS

struct proc_dir_entry *rtai_proc_root = NULL;

static int rtai_read_rtai(char *page, char **start, off_t off, int count,
                          int *eof, void *data)
{

	PROC_PRINT_VARS;
        int i;

        PROC_PRINT("\nRTAI Real Time Kernel, Version: %s\n\n", RTAI_RELEASE);
        PROC_PRINT("    RTAI mount count: %d\n", rtai_mounted);
#ifdef CONFIG_SMP
        PROC_PRINT("    APIC Frequency: %d\n", tuned.apic_freq);
        PROC_PRINT("    APIC Latency: %d ns\n", LATENCY_APIC);
        PROC_PRINT("    APIC Setup: %d ns\n", SETUP_TIME_APIC);
#endif
        PROC_PRINT("\nGlobal irqs used by RTAI: \n");
        for (i = 0; i < NR_GLOBAL_IRQS; i++) {
          if (global_irq[i].handler) {
            PROC_PRINT("%d ", i);
          }
        }
        PROC_PRINT("\nCpu_Own irqs used by RTAI: \n");
        for (i = 0; i < NR_CPU_OWN_IRQS; i++) {
          if (cpu_own_irq[i].handler) {
            PROC_PRINT("%d ", i);
          }
        }
        PROC_PRINT("\nRTAI sysreqs in use: \n");
        for (i = 0; i < NR_GLOBAL_IRQS; i++) {
          if (sysrq[i].rtai_handler || sysrq[i].user_handler) {
            PROC_PRINT("%d ", i);
          }
        }
        PROC_PRINT("\n\n");

	PROC_PRINT_DONE;
}       /* End function - rtai_read_rtai */

static int rtai_proc_register(void)
{

	struct proc_dir_entry *ent;

        rtai_proc_root = create_proc_entry("rtai", S_IFDIR, 0);
        if (!rtai_proc_root) {
		printk("Unable to initialize /proc/rtai\n");
                return(-1);
        }
	rtai_proc_root->owner = THIS_MODULE;
        ent = create_proc_entry("rtai", S_IFREG|S_IRUGO|S_IWUSR, rtai_proc_root);
        if (!ent) {
		printk("Unable to initialize /proc/rtai/rtai\n");
                return(-1);
        }
	ent->read_proc = rtai_read_rtai;
        return(0);
}       /* End function - rtai_proc_register */


static void rtai_proc_unregister(void)
{
        remove_proc_entry("rtai", rtai_proc_root);
        remove_proc_entry("rtai", 0);
}       /* End function - rtai_proc_unregister */

#endif /* CONFIG_PROC_FS */
/* ------------------< end of proc filesystem section >------------------*/


/********** SOME TIMER FUNCTIONS TO BE LIKELY NEVER PUT ELSWHERE *************/

/* Real time timers. No oneshot, and related timer programming, calibration. */
/* Use the utility module. It is also been decided that this stuff has to    */
/* stay here.                                                                */

struct calibration_data tuned;
struct rt_times rt_times;
struct rt_times rt_smp_times[NR_RT_CPUS];
static int use_local_apic;

// The two functions below are for 486s.

#define COUNTER_2_LATCH 0xFFFE
static int last_8254_counter2; 
static RTIME ts_8254;

RTIME rd_8254_ts(void)
{
	RTIME t;
	unsigned long flags;
	int inc, c2;

	rt_global_save_flags(&flags);
	rt_global_cli();
//	outb(0x80, 0x43);
	outb(0xD8, 0x43);
      	c2 = inb(0x42);
	inc = last_8254_counter2 - (c2 |= (inb(0x42) << 8));
	last_8254_counter2 = c2;
	t = (ts_8254 += (inc > 0 ? inc : inc + COUNTER_2_LATCH));
	rt_global_restore_flags(flags);
	return t;
}

void rt_setup_8254_tsc(void)
{
	unsigned long flags;
	int c;

	flags = hard_lock_all();
	outb_p(0x00, 0x43);
	c = inb_p(0x40);
	c |= inb_p(0x40) << 8;
	outb_p(0xB4, 0x43);
	outb_p(COUNTER_2_LATCH & 0xFF, 0x42);
	outb_p(COUNTER_2_LATCH >> 8, 0x42);
	ts_8254 = c + ((RTIME)LATCH)*jiffies;
	last_8254_counter2 = 0; 
	outb_p((inb_p(0x61) & 0xFD) | 1, 0x61);
	hard_unlock_all(flags);
}

static void trap_trashed_local_timers_ipi(void)
{ 
	trashed_local_timers_ipi++;
	return;
}

int rt_request_timer(void (*handler)(void), unsigned int tick, int apic)
{
#define WAIT_LOCK 5

	int count;
	unsigned long flags;

	TRACE_RTAI_TIMER(TRACE_RTAI_EV_TIMER_REQUEST, handler, tick);

	used_local_apic = use_local_apic = apic;
	flags = hard_lock_all();
	do {
		outb(0x00, 0x43);
		count = inb(0x40);
	} while	((count | (inb(0x40) << 8)) > WAIT_LOCK);
	rt_times.tick_time = rdtsc();
	if (tick > 0) {
		rt_times.linux_tick = use_local_apic ? APIC_ICOUNT : LATCH;
		rt_times.tick_time = ((RTIME)rt_times.linux_tick)*(jiffies + 1);
		rt_times.intr_time = rt_times.tick_time + tick;
		rt_times.linux_time = rt_times.tick_time + rt_times.linux_tick;
		rt_times.periodic_tick = tick;
		if (use_local_apic) {
			global.hard_lock_all_service = 2;
			rt_free_cpu_own_irq(RTAI_APIC_TIMER_IPI);
			rt_request_cpu_own_irq(RTAI_APIC_TIMER_IPI, handler);
//			rt_request_linux_irq(TIMER_8254_IRQ, broadcast_to_local_timers, "broadcast", broadcast_to_local_timers);
			setup_periodic_apic(tick, RTAI_APIC_TIMER_VECTOR);
		} else {
			outb(0x34, 0x43);
			outb(tick & 0xFF, 0x40);
			outb(tick >> 8, 0x40);
			rt_free_global_irq(TIMER_8254_IRQ);
			if (rt_request_global_irq(TIMER_8254_IRQ, handler) < 0) {
				hard_unlock_all(flags);
				return -EINVAL;
			}
		}
	} else {
		rt_times.linux_tick = imuldiv(LATCH, tuned.cpu_freq, FREQ_8254);
//		rt_times.tick_time = rdtsc();
		rt_times.intr_time = rt_times.tick_time + rt_times.linux_tick;
		rt_times.linux_time = rt_times.tick_time + rt_times.linux_tick;
		rt_times.periodic_tick = rt_times.linux_tick;
		if (use_local_apic) {
			global.hard_lock_all_service = 2;
			rt_free_cpu_own_irq(RTAI_APIC_TIMER_IPI);
			rt_request_cpu_own_irq(RTAI_APIC_TIMER_IPI, handler);
//			rt_request_linux_irq(TIMER_8254_IRQ, broadcast_to_local_timers, "broadcast", broadcast_to_local_timers);
			setup_oneshot_apic(APIC_ICOUNT, RTAI_APIC_TIMER_VECTOR);
		} else {
			outb(0x30, 0x43);
			outb(LATCH & 0xFF, 0x40);
			outb(LATCH >> 8, 0x40);
			rt_free_global_irq(TIMER_8254_IRQ);
			if (rt_request_global_irq(TIMER_8254_IRQ, handler) < 0) {
				hard_unlock_all(flags);
				return -EINVAL;
			}
		}
	}
	hard_unlock_all(flags);
	if (use_local_apic) {
		return rt_request_linux_irq(TIMER_8254_IRQ, broadcast_to_local_timers, "broadcast", broadcast_to_local_timers);
	}
//	hard_save_flags(flags);
	return 0;
}

void rt_free_timer(void)
{
	unsigned long flags;

	TRACE_RTAI_TIMER(TRACE_RTAI_EV_TIMER_FREE, 0, 0);

	if (use_local_apic) {
		rt_free_linux_irq(TIMER_8254_IRQ, broadcast_to_local_timers);
	}
	flags = hard_lock_all();
	if (use_local_apic) {
		global.hard_lock_all_service = 3;
		setup_periodic_apic(APIC_ICOUNT, LOCAL_TIMER_VECTOR);
		rt_free_cpu_own_irq(RTAI_APIC_TIMER_IPI);
		rt_request_cpu_own_irq(RTAI_APIC_TIMER_IPI, trap_trashed_local_timers_ipi);
		use_local_apic = 0;
	} else {
		outb(0x34, 0x43);
		outb(LATCH & 0xFF, 0x40);
		outb(LATCH >> 8, 0x40);
		rt_free_global_irq(TIMER_8254_IRQ);
	}
	hard_unlock_all(flags);
}

#ifdef CONFIG_SMP

void rt_request_timer_cpuid(void (*handler)(void), unsigned int tick, int cpuid)
{
#define WAIT_LOCK 5

	int count;
	unsigned long flags;
	used_local_apic = use_local_apic = 1;
	apic_timers_sync_time = 0;
	for (count = 0; count < NR_RT_CPUS; count++) {
		apic_timer_mode[count].mode = apic_timer_mode[count].count = 0;
	}
	flags = hard_lock_all();
	global.hard_lock_all_service = 1;
	do {
		outb(0x00, 0x43);
		count = inb(0x40);
	} while	((count | (inb(0x40) << 8)) > WAIT_LOCK);
	rt_times.tick_time = rdtsc();
	if (tick > 0) {
		rt_times.linux_tick = use_local_apic ? APIC_ICOUNT : LATCH;
		rt_times.tick_time = ((RTIME)rt_times.linux_tick)*(jiffies + 1);
		rt_times.intr_time = rt_times.tick_time + tick;
		rt_times.linux_time = rt_times.tick_time + rt_times.linux_tick;
		rt_times.periodic_tick = tick;
		if (cpuid == hard_cpu_id()) {
			setup_periodic_apic(tick, RTAI_APIC_TIMER_VECTOR);
		} else {
			apic_timer_mode[cpuid].mode = 1;
			apic_timer_mode[cpuid].count = tick;
			setup_oneshot_apic(0, RTAI_APIC_TIMER_VECTOR);
		}
	} else {
		rt_times.linux_tick = imuldiv(LATCH, tuned.cpu_freq, FREQ_8254);
		rt_times.intr_time = rt_times.tick_time + rt_times.linux_tick;
		rt_times.linux_time = rt_times.tick_time + rt_times.linux_tick;
		rt_times.periodic_tick = rt_times.linux_tick;
		if (cpuid == hard_cpu_id()) {
			setup_oneshot_apic(APIC_ICOUNT, RTAI_APIC_TIMER_VECTOR);
		} else {
			apic_timer_mode[cpuid].mode = 0;
			apic_timer_mode[cpuid].count = APIC_ICOUNT;
			setup_oneshot_apic(0, RTAI_APIC_TIMER_VECTOR);
		}
	}
	rt_free_cpu_own_irq(RTAI_APIC_TIMER_IPI);
	rt_request_cpu_own_irq(RTAI_APIC_TIMER_IPI, handler);
	rt_request_linux_irq(TIMER_8254_IRQ, broadcast_to_local_timers, "broadcast", broadcast_to_local_timers);
	hard_unlock_all(flags);
}

void rt_request_apic_timers(void (*handler)(void), struct apic_timer_setup_data *apic_timer_data)
{
#define WAIT_LOCK 5
	int count, cpuid;
	unsigned long flags;
	struct apic_timer_setup_data *p;
	volatile struct rt_times *rt_times;

	TRACE_RTAI_TIMER(TRACE_RTAI_EV_TIMER_REQUEST_APIC, handler, 0);

	flags = hard_lock_all();
	global.hard_lock_all_service = 1;
	do {
		outb(0x00, 0x43);
		count = inb(0x40);
	} while	((count | (inb(0x40) << 8)) > WAIT_LOCK);
	apic_timers_sync_time = rdtsc() + imuldiv(LATCH, tuned.cpu_freq, FREQ_8254);
	for (cpuid = 0; cpuid < NR_RT_CPUS; cpuid++) {
		*(p = apic_timer_mode + cpuid) = apic_timer_data[cpuid];
		rt_times = rt_smp_times + cpuid;
		if (p->mode > 0) {
			p->mode = 1;
			rt_times->linux_tick = APIC_ICOUNT;
			rt_times->tick_time = llimd(apic_timers_sync_time, FREQ_APIC, tuned.cpu_freq);
			rt_times->periodic_tick = 
			p->count = imuldiv(p->count, FREQ_APIC, 1000000000);
		} else {
			p->mode =  0;
			rt_times->linux_tick = imuldiv(LATCH, tuned.cpu_freq, FREQ_8254);
			rt_times->tick_time = apic_timers_sync_time;
			rt_times->periodic_tick = rt_times->linux_tick;
			p->count = APIC_ICOUNT;
		}
		rt_times->intr_time = rt_times->tick_time + rt_times->periodic_tick;
		rt_times->linux_time = rt_times->tick_time + rt_times->linux_tick;
	}
	if ((p = apic_timer_mode + hard_cpu_id())->mode) {
		while (rdtsc() < apic_timers_sync_time);
		setup_periodic_apic(p->count, RTAI_APIC_TIMER_VECTOR);
	} else {
		while (rdtsc() < apic_timers_sync_time);
		setup_oneshot_apic(p->count, RTAI_APIC_TIMER_VECTOR);
	}
	rt_free_cpu_own_irq(RTAI_APIC_TIMER_IPI);
	rt_request_cpu_own_irq(RTAI_APIC_TIMER_IPI, handler);
	rt_request_linux_irq(TIMER_8254_IRQ, broadcast_to_local_timers, "broadcast", broadcast_to_local_timers);
	for (cpuid = 0; cpuid < NR_RT_CPUS; cpuid++) {
		if ((p = apic_timer_data + cpuid)->mode > 0) {
			p->mode = 1;
			p->count = imuldiv(p->count, FREQ_APIC, 1000000000);
		} else {
			p->mode = 0;
			p->count = imuldiv(p->count, tuned.cpu_freq, 1000000000);
		}
	}
	hard_unlock_all(flags);
}

void rt_free_apic_timers(void)
{
	unsigned long flags;

	TRACE_RTAI_TIMER(TRACE_RTAI_EV_TIMER_APIC_FREE, 0, 0);

	rt_free_linux_irq(TIMER_8254_IRQ, broadcast_to_local_timers);
	flags = hard_lock_all();
	global.hard_lock_all_service = 3;
//	rt_free_linux_irq(TIMER_8254_IRQ, broadcast_to_local_timers);
	setup_periodic_apic(APIC_ICOUNT, LOCAL_TIMER_VECTOR);
	rt_free_cpu_own_irq(RTAI_APIC_TIMER_IPI);
	rt_request_cpu_own_irq(RTAI_APIC_TIMER_IPI, trap_trashed_local_timers_ipi);
	hard_unlock_all(flags);
}

#else

void rt_request_timer_cpuid(void (*handler)(void), unsigned int tick, int cpuid)
{
}

void rt_request_apic_timers(void (*handler)(void), struct apic_timer_setup_data *apic_timer_data) { }

#define rt_free_apic_timers() rt_free_timer()

#endif

int calibrate_8254(void)
{
	unsigned long flags, i;
	RTIME t;
	flags = hard_lock_all();
	outb(0x34, 0x43);
	t = rdtsc();
	for (i = 0; i < 10000; i++) { 
		outb(LATCH & 0xFF, 0x40);
		outb(LATCH >> 8, 0x40);
	}
	i = rdtsc() - t;
	hard_unlock_all(flags);
	return imuldiv(i, 100000, CPU_FREQ);
}
/******** END OF SOME TIMER FUNCTIONS TO BE LIKELY NEVER PUT ELSWHERE *********/

// Our printk function, its use should be safe everywhere.
#include <linux/console.h>

int rtai_print_to_screen(const char *format, ...)
{
        static spinlock_t display_lock = SPIN_LOCK_UNLOCKED;
        static char display[25*80];
        unsigned long flags, lflags;
        struct console *c;
        va_list args;
        int len;

	lflags = linux_save_flags_and_cli_cpuid(hard_cpu_id());
        flags = rt_spin_lock_irqsave(&display_lock);
        va_start(args, format);
        len = vsprintf(display, format, args);
        va_end(args);
        c = console_drivers;
        while(c) {
                if ((c->flags & CON_ENABLED) && c->write)
                        c->write(c, display, len);
                c = c->next;
	}
        rt_spin_unlock_irqrestore(flags, &display_lock);
	rtai_just_copy_back(lflags, hard_cpu_id());

	return len;
}

/*
 *  rt_printk.c, hacked from linux/kernel/printk.c.
 *
 * Modified for RT support, David Schleef.
 *
 * Adapted to RTAI, and restyled his way by Paolo Mantegazza. Now it has been
 * taken away from the fifos module and has become an integral part of the basic
 * RTAI module.
 */

#define PRINTK_BUF_LEN	(4096*2) // Some test programs generate much output. PC
#define TEMP_BUF_LEN	(256)

static char rt_printk_buf[PRINTK_BUF_LEN];
static int buf_front, buf_back;

static char buf[TEMP_BUF_LEN];

int rt_printk(const char *fmt, ...)
{
        static spinlock_t display_lock = SPIN_LOCK_UNLOCKED;
	va_list args;
	int len, i;
	unsigned long flags, lflags;

	lflags = linux_save_flags_and_cli_cpuid(hard_cpu_id());
        flags = rt_spin_lock_irqsave(&display_lock);
	va_start(args, fmt);
	len = vsprintf(buf, fmt, args);
	va_end(args);
	if (buf_front + len >= PRINTK_BUF_LEN) {
		i = PRINTK_BUF_LEN - buf_front;
		memcpy(rt_printk_buf + buf_front, buf, i);
		memcpy(rt_printk_buf, buf + i, len - i);
		buf_front = len - i;
	} else {
		memcpy(rt_printk_buf + buf_front, buf, len);
		buf_front += len;
	}
        rt_spin_unlock_irqrestore(flags, &display_lock);
	rtai_just_copy_back(lflags, hard_cpu_id());
	rt_pend_linux_srq(1);

	return len;
}

static void rt_printk_sysreq_handler(void)
{
	int tmp;

	while(1) {
		tmp = buf_front;
		if (buf_back  > tmp) {
			printk("%.*s", PRINTK_BUF_LEN - buf_back, rt_printk_buf + buf_back);
			buf_back = 0;
		}
		if (buf_back == tmp) {
			break;
		}
		printk("%.*s", tmp - buf_back, rt_printk_buf + buf_back);
		buf_back = tmp;
	}
}

void ll2a(long long ll, char *s)
{
#define LOW 0
	unsigned long i, k, ul;
	char a[20];

	if (ll < 0) {
		s[0] = 1;
		ll = -ll;
	} else {
		s[0] = 0;
	}
	i = 0;
	while (ll > 0xFFFFFFFF) {
		ll = ulldiv(ll, 10, &k);
		a[++i] = k + '0';
	}
	ul = ((unsigned long *)&ll)[LOW];
	do {
		ul = (k = ul)/10;
		a[++i] = k - ul*10 + '0';
	} while (ul);
	if (s[0]) {
		k = 1;
		s[0] = '-';
	} else {
		k = 0;
	}
	a[0] = 0;
	while ((s[k++] = a[i--]));
}

void (*rt_set_ihook (void (*hookfn)(int)))(int) {

#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
    return (void (*)(int))xchg(&rtai_isr_hook,hookfn); /* This is atomic */
#else  /* !CONFIG_RTAI_SCHED_ISR_LOCK */
    return NULL;
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */
}
