/*
 *   ARTI -- RTAI-compatible Adeos-based Real-Time Interface. Based on
 *   the original RTAI layer for x86. This file provides user-visible
 *   definitions for compatibility purpose with the legacy RTHAL. Must
 *   be included from rtai_hal.h only.
 *
 *   Original RTAI/x86 layer implementation:
 *   Copyright (C) 2000 Paolo Mantegazza,
 *   Copyright (C) 2000 Steve Papacharalambous,
 *   Copyright (C) 2000 Stuart Hughes,
 *   and others.
 *
 *   RTAI/x86 rewrite over Adeos:
 *   Copyright (C) 2002 Philippe Gerum.
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
 */

#ifndef _RTAI_ASM_I386_OLDNAMES_H
#define _RTAI_ASM_I386_OLDNAMES_H

#ifdef __KERNEL__

#define IFLAG                        ARTI_IFLAG
#define hard_cli()                   arti_cli()
#define hard_sti()                   arti_sti()
#define hard_save_flags_and_cli(x)   arti_local_irq_save(x)
#define hard_restore_flags(x)        arti_local_irq_restore(x)
#define hard_save_flags(x)           arti_local_irq_flags(x)
#define hard_cpu_id                  adeos_processor_id
#define this_rt_task                 ptd

struct rt_hal {
    void *ret_from_intr;
    void *__switch_to;
    void *padding_0;
    void (*disint)(void);
    void (*enint)(void);
    unsigned (*getflags)(void);
    void (*setflags)(unsigned flags); 
    unsigned (*getflags_and_cli)(void);
    void *padding_1;
    void *padding_2;
    void *padding_3;
    void *padding_4;
    void *padding_5;
    void *padding_6;
    void (*lxrt_global_cli)(void);
    void (*switch_mem)(struct task_struct *, struct task_struct *, int);
    void *padding_7;
    volatile int *apicmap;
}; /* A partial HAL struct for source compatibility purpose. */

#endif /* __KERNEL__ */

#ifdef __ARTI__

static void arti_compat_hard_cli(void) {
    arti_cli();
}

static void (*rtai_exit_callback)(void);

void arti_compat_exit_trampoline (adevinfo_t *evinfo) {

/* FIXME? Adeos only provides for a single process exit callback
through adeos_catch_event(). If we need more than one routine to be
registered here, we will have to provide a linked list by
ourselves. */

    struct task_struct *task = (struct task_struct *)evinfo->evdata;

    if (task->ptd[0])
	rtai_exit_callback();

    adeos_propagate_event(evinfo);
}

int set_rtai_callback (void (*fun)(void)) {
    rtai_exit_callback = fun;
    return adeos_catch_event(ADEOS_EXIT_PROCESS,&arti_compat_exit_trampoline);
}

void remove_rtai_callback (void (*fun)(void)) {
    adeos_catch_event(ADEOS_EXIT_PROCESS,0);
}

extern void *ret_from_intr;

struct rt_hal rthal = {
    &ret_from_intr,
    &__switch_to,
    0,
    &arti_linux_cli,
    &arti_linux_sti,
    &arti_linux_save_flags,
    &arti_linux_restore_flags,
    &arti_linux_save_flags_and_cli,
    0,
    0,
    0,
    0,
    0,
    0,
    &arti_compat_hard_cli,
    &arti_switch_linux_mm,
    0,
#ifdef CONFIG_SMP
#ifdef CONFIG_MULTIQUAD
    logical_apicid_2_cpu,
#else  /* !CONFIG_MULTIQUAD */
    physical_apicid_2_cpu,
#endif /* CONFIG_MULTIQUAD */
#else /* !CONFIG_SMP */
    0,
#endif /* CONFIG_SMP */
}; /* A partial HAL struct for source compatibility purpose. */

#else /* !__ARTI__ */

#define tuned           arti_tunables
#define NR_RT_CPUS      ARTI_NR_CPUS
#define RT_TIME_END     ARTI_TIME_LIMIT

#define CPU_FREQ        ARTI_CPU_FREQ
#define TIMER_8254_IRQ  ARTI_TIMER_8254_IRQ
#define FREQ_8254       ARTI_FREQ_8254
#define LATENCY_8254    ARTI_LATENCY_8254
#define SETUP_TIME_8254	ARTI_SETUP_TIME_8254

#define FREQ_APIC       ARTI_FREQ_APIC
#define LATENCY_APIC    ARTI_LATENCY_APIC
#define SETUP_TIME_APIC ARTI_SETUP_TIME_APIC

#define CALIBRATED_APIC_FREQ  ARTI_CALIBRATED_APIC_FREQ
#define CALIBRATED_CPU_FREQ   ARTI_CALIBRATED_CPU_FREQ

#ifdef __KERNEL__

/* For compatibility reasons with RTHAL, IPIs numbers are indices into
   the __ipi2vec[] array, instead of actual interrupt numbers. */

#define RTAI_1_VECTOR  RTAI_APIC1_VECTOR
#define RTAI_1_IPI     6
#define RTAI_2_VECTOR  RTAI_APIC2_VECTOR
#define RTAI_2_IPI     7
#define RTAI_3_VECTOR  RTAI_APIC3_VECTOR
#define RTAI_3_IPI     8
#define RTAI_4_VECTOR  RTAI_APIC4_VECTOR
#define RTAI_4_IPI     9

#define broadcast_to_local_timers arti_broadcast_to_timers
#define rtai_signal_handler       arti_signal_handler
#define rt_set_rtai_trap_handler  rt_set_trap_handler
#define rt_mount_rtai             rt_mount
#define rt_umount_rtai            rt_umount
#define calibrate_8254            arti_calibrate_8254

static inline int rt_request_global_irq_ext(unsigned irq,
					    void (*handler)(void),
					    unsigned long cookie) {

    return rt_request_irq(irq,(void (*)(unsigned,void *))handler,(void *)cookie);
}

static inline int rt_request_global_irq(unsigned irq,
					void (*handler)(void)) {

    return rt_request_irq(irq,(void (*)(unsigned,void *))handler,0);
}

static inline void rt_set_global_irq_ext(unsigned irq,
					 int ext,
					 unsigned long cookie) {

    rt_set_irq_cookie(irq,(void *)cookie);
}

static inline int rt_free_global_irq(unsigned irq) {

    return rt_release_irq(irq);
}

#undef  rdtsc
#define rdtsc()     arti_rdtsc()
#define rd_CPU_ts() rdtsc()

#define ulldiv(a,b,c)  arti_ulldiv(a,b,c)
#define imuldiv(a,b,c) arti_imuldiv(a,b,c)
#define llimd(a,b,c)   arti_llimd(a,b,c)

#define locked_cpus                        (&arti_cpu_lock)

#define lxrt_hrt_flags        arti_cpu_lxrt
#define rtai_print_to_screen  rt_printk

#define RTAI_NR_TRAPS         ADEOS_NR_FAULTS

#ifdef ARTI_EMULATE_TSC

#define DECLR_8254_TSC_EMULATION \
extern void *kd_mksound; \
static void *linux_mksound; \
static void rtai_mksound(void) { }

#define TICK_8254_TSC_EMULATION  rdtsc()

#define SETUP_8254_TSC_EMULATION \
	do { \
		linux_mksound = kd_mksound; \
		kd_mksound = rtai_mksound; \
		rt_setup_8254_tsc(); \
	} while (0)

#define CLEAR_8254_TSC_EMULATION \
	do { \
		if (linux_mksound) { \
			kd_mksound = linux_mksound; \
		} \
	} while (0)

#else /* ARTI_EMULATE_TSC */

#define DECLR_8254_TSC_EMULATION
#define TICK_8254_TSC_EMULATION
#define SETUP_8254_TSC_EMULATION
#define CLEAR_8254_TSC_EMULATION

#endif /* !ARTI_EMULATE_TSC */

#ifndef __cplusplus

#include <linux/irq.h>
#include <asm/desc.h>

extern struct desc_struct idt_table[];

extern struct rt_hal rthal;

static inline struct desc_struct rt_set_full_intr_vect (unsigned vector,
							int type,
							int dpl,
							void (*handler)(void)) {
    struct desc_struct e = idt_table[vector];
    idt_table[vector].a = (__KERNEL_CS << 16) | ((unsigned)handler & 0x0000FFFF);
    idt_table[vector].b = ((unsigned)handler & 0xFFFF0000) | (0x8000 + (dpl << 13) + (type << 8));
    return e;
}

static inline void rt_reset_full_intr_vect(unsigned vector,
					   struct desc_struct e) {
    idt_table[vector] = e;
}

static const int __ipi2vec[] = {
    INVALIDATE_TLB_VECTOR,
    LOCAL_TIMER_VECTOR,
    RESCHEDULE_VECTOR,
    CALL_FUNCTION_VECTOR,
    SPURIOUS_APIC_VECTOR,
    ERROR_APIC_VECTOR,
    -1,			/* RTAI_1_VECTOR: reserved */
    -1,			/* RTAI_2_VECTOR: reserved */
    RTAI_3_VECTOR,
    RTAI_4_VECTOR
};

#define __vec2irq(v) ((v) - FIRST_EXTERNAL_VECTOR)

static const unsigned __ipi2irq[] = {
    __vec2irq(INVALIDATE_TLB_VECTOR),
    __vec2irq(LOCAL_TIMER_VECTOR),
    __vec2irq(RESCHEDULE_VECTOR),
    __vec2irq(CALL_FUNCTION_VECTOR),
    __vec2irq(SPURIOUS_APIC_VECTOR),
    __vec2irq(ERROR_APIC_VECTOR),
    (unsigned)-1,	/* RTAI_1_IPI: reserved */
    (unsigned)-1,	/* RTAI_2_IPI: reserved */
    RTAI_APIC3_IPI,
    RTAI_APIC4_IPI
};

/* cpu_own_irq() routines are RTHAL-specific and emulated here for
   compatibility purposes. */

static inline int rt_request_cpu_own_irq (unsigned ipi, void (*handler)(void)) {

    return ipi < 10 ? rt_request_irq(__ipi2irq[ipi],(rt_irq_handler_t)handler,0) : -EINVAL;
}

static inline int rt_free_cpu_own_irq (unsigned ipi) {

    return ipi < 10 ? rt_release_irq(__ipi2irq[ipi]) : -EINVAL;
}

#ifdef CONFIG_SMP

static inline void send_ipi_shorthand (unsigned shorthand,
				       int irq) {
    unsigned long flags;
    arti_hw_lock(flags);
    apic_wait_icr_idle();
    apic_write_around(APIC_ICR,APIC_DEST_LOGICAL|shorthand|__ipi2vec[irq]);
    arti_hw_unlock(flags);
}

static inline void send_ipi_logical (unsigned long dest,
				     int irq) {
    unsigned long flags;

    if ((dest &= cpu_online_map) != 0)
	{
	arti_hw_lock(flags);
	apic_wait_icr_idle();
	apic_write_around(APIC_ICR2,SET_APIC_DEST_FIELD(dest));
	apic_write_around(APIC_ICR,APIC_DEST_LOGICAL|__ipi2vec[irq]);
	arti_hw_unlock(flags);
	}
}

#endif /* CONFIG_SMP */

static inline void *get_intr_handler (unsigned vector) {

    return (void *)((idt_table[vector].b & 0xFFFF0000) | 
		    (idt_table[vector].a & 0x0000FFFF));
}

static inline void set_intr_vect (unsigned vector,
				  void (*handler)(void)) {

    idt_table[vector].a = (idt_table[vector].a & 0xFFFF0000) | 
	((unsigned)handler & 0x0000FFFF);
    idt_table[vector].b = ((unsigned)handler & 0xFFFF0000) | 
	(idt_table[vector].b & 0x0000FFFF);
}

static inline void *rt_set_intr_handler (unsigned vector,
					 void (*handler)(void)) {

    void (*saved_handler)(void) = get_intr_handler(vector);
    set_intr_vect(vector, handler);
    return saved_handler;
}

static inline void rt_reset_intr_handler (unsigned vector,
					  void (*handler)(void)) {
    set_intr_vect(vector, handler);
}

static inline unsigned long get_cr2 (void) {

    unsigned long address;
    __asm__("movl %%cr2,%0":"=r" (address));
    return address;
}

#endif /* __KERNEL__ */

#endif /* !__cplusplus */

#endif /* __ARTI__ */

#endif /* !_RTAI_ASM_I386_OLDNAMES_H */
