/*
 * Copyright (C) 2005       Paolo Mantegazza  (mantegazza@aero.polimi.it)
 * (RTC specific part with) Giuseppe Quaranta (quaranta@aero.polimi.it)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef INCLUDED_BY_HAL_C

#include <linux/mc146818rtc.h>

//#define TEST_RTC
#define MIN_RTC_FREQ  2
#define MAX_RTC_FREQ  8192
#define RTC_FREQ      MAX_RTC_FREQ

static void rt_broadcast_rtc_interrupt(void)
{
#ifdef CONFIG_SMP
	apic_wait_icr_idle();
	apic_write_around(APIC_ICR, APIC_DM_FIXED | APIC_DEST_ALLINC | RTAI_APIC_TIMER_VECTOR | APIC_DEST_LOGICAL);
#endif
}

static void (*usr_rtc_handler)(void);

#if CONFIG_RTAI_DONT_DISPATCH_CORE_IRQS // && defined(CONFIG_RTAI_RTC_FREQ) && CONFIG_RTAI_RTC_FREQ

int _rtai_rtc_timer_handler(void)
{
	unsigned long cpuid = rtai_cpuid();
	unsigned long sflags;
	RTAI_SCHED_ISR_LOCK();
	HAL_LOCK_LINUX();

	rt_mask_and_ack_irq(RTC_IRQ);
 	CMOS_READ(RTC_INTR_FLAGS);
	rt_enable_irq(RTC_IRQ);
	usr_rtc_handler();

        HAL_UNLOCK_LINUX();
        RTAI_SCHED_ISR_UNLOCK();
#if 1 //LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	if (!test_bit(IPIPE_STALL_FLAG, &hal_root_domain->cpudata[cpuid].status)) {
		rtai_sti();
		hal_fast_flush_pipeline(cpuid);
#if defined(CONFIG_SMP) &&  LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,32)
		__set_bit(IPIPE_STALL_FLAG, ipipe_root_status[rtai_cpuid()]);
#endif
		return 1;
        }
#endif
	return 0;
}

void rtai_rtc_timer_handler (void);
	__asm__ ( \
	"\n" __ALIGN_STR"\n\t" \
        SYMBOL_NAME_STR(rtai_rtc_timer_handler) ":\n\t" \
	"\n .p2align\n" \
	"pushq $0\n" \
	"cld\n" \
	"sub    $0x48,%rsp\n" \
	"mov    %rdi,0x40(%rsp)\n" \
	"mov    %rsi,0x38(%rsp)\n" \
	"mov    %rdx,0x30(%rsp)\n" \
	"mov    %rcx,0x28(%rsp)\n" \
	"mov    %rax,0x20(%rsp)\n" \
	"mov    %r8,0x18(%rsp)\n" \
	"mov    %r9,0x10(%rsp)\n" \
	"mov    %r10,0x8(%rsp)\n" \
	"mov    %r11,(%rsp)\n" \
	"lea    0xffffffffffffffd0(%rsp),%rdi\n" \
	"testl  $0x3,0x88(%rdi)\n" \
	"je 1f\n" \
	"swapgs\n" \
	"1:addl   $0x1,%gs:0x30\n" \
	"mov    %gs:0x38,%rax\n" \
	"cmove  %rax,%rsp\n" \
	"push   %rdi\n" \
        "call "SYMBOL_NAME_STR(_rtai_rtc_timer_handler)"\n\t" \
	"pop    %rdi\n" \
	"cli\n" \
	"subl   $0x1,%gs:0x30\n" \
	"lea    0x30(%rdi),%rsp\n" \
	"mov    %gs:0x18,%rcx\n" \
	"sub    $0x1fd8,%rcx\n" \
	"testl  $0x3,0x58(%rsp)\n" \
	"je 2f\n" \
	"swapgs\n" \
	"2:mov    (%rsp),%r11\n" \
	"mov    0x8(%rsp),%r10\n" \
	"mov    0x10(%rsp),%r9\n" \
	"mov    0x18(%rsp),%r8\n" \
	"mov    0x20(%rsp),%rax\n" \
	"mov    0x28(%rsp),%rcx\n" \
	"mov    0x30(%rsp),%rdx\n" \
	"mov    0x38(%rsp),%rsi\n" \
	"mov    0x40(%rsp),%rdi\n" \
	"add    $0x50,%rsp\n" \
	"iretq");

#if 0
void rtai_rtc_timer_handler (void);
	__asm__ ( \
        "\n" __ALIGN_STR"\n\t" \
        SYMBOL_NAME_STR(rtai_rtc_timer_handler) ":\n\t" \
        "pushl $-1\n\t" \
	"cld\n\t" \
        "pushl %es\n\t" \
        "pushl %ds\n\t" \
        "pushl %eax\n\t" \
        "pushl %ebp\n\t" \
        "pushl %edi\n\t" \
        "pushl %esi\n\t" \
        "pushl %edx\n\t" \
        "pushl %ecx\n\t" \
        "pushl %ebx\n\t" \
	__LXRT_GET_DATASEG(ecx) \
        "movl %ecx, %ds\n\t" \
        "movl %ecx, %es\n\t" \
        "call "SYMBOL_NAME_STR(_rtai_rtc_timer_handler)"\n\t" \
        "testl %eax,%eax\n\t" \
        "jnz  ret_from_intr\n\t" \
        "popl %ebx\n\t" \
        "popl %ecx\n\t" \
        "popl %edx\n\t" \
        "popl %esi\n\t" \
        "popl %edi\n\t" \
        "popl %ebp\n\t" \
        "popl %eax\n\t" \
        "popl %ds\n\t" \
        "popl %es\n\t" \
        "addl $4,%esp\n\t" \
        "iret");
#endif

static struct gate_struct rtai_rtc_timer_sysvec;

#endif /* CONFIG_RTAI_DONT_DISPATCH_CORE_IRQS */

/* 
 * NOTE FOR USE IN RTAI_TRIOSS.
 * "rtc_freq" must be a power of 2 & (MIN_RTC_FREQ <= rtc_freq <= MAX_RTC_FREQ).
 * So the best thing to do is to load this module and your skin of choice 
 * setting "rtc_freq" in this module and "rtc_freq" in the skin specific module
 * to the very same power of 2 that best fits your needs.
 */ 

static void rtc_handler(int irq, int rtc_freq)
{
#ifdef TEST_RTC
	static int stp, cnt;
	if (++cnt == rtc_freq) {
		rt_printk("<> IRQ %d, %d: CNT %d <>\n", irq, ++stp, cnt);
		cnt = 0;
	}
#endif
 	CMOS_READ(RTC_INTR_FLAGS);
	rt_enable_irq(RTC_IRQ);
	if (usr_rtc_handler) {
		usr_rtc_handler();
	}
}

void rt_request_rtc(long rtc_freq, void *handler)
{
	int pwr2;

	if (rtc_freq <= 0) {
		rtc_freq = RTC_FREQ;
	}
	if (rtc_freq > MAX_RTC_FREQ) {
		rtc_freq = MAX_RTC_FREQ;
	} else if (rtc_freq < MIN_RTC_FREQ) {
		rtc_freq = MIN_RTC_FREQ;
	}
	pwr2 = 1;
	if (rtc_freq > MIN_RTC_FREQ) {
		while (rtc_freq > (1 << pwr2)) {
			pwr2++;
		}
		if (rtc_freq <= ((3*(1 << (pwr2 - 1)) + 1)>>1)) {
			pwr2--;
		}
	}

	rt_disable_irq(RTC_IRQ);
	rt_release_irq(RTC_IRQ);
	rtai_cli();
	CMOS_WRITE(CMOS_READ(RTC_FREQ_SELECT), RTC_FREQ_SELECT);
	CMOS_WRITE(CMOS_READ(RTC_CONTROL),     RTC_CONTROL);
	CMOS_WRITE(RTC_REF_CLCK_32KHZ | (16 - pwr2),          RTC_FREQ_SELECT);
	CMOS_WRITE((CMOS_READ(RTC_CONTROL) & 0x8F) | RTC_PIE, RTC_CONTROL);
	rtai_sti();
	usr_rtc_handler = handler ? handler : rt_broadcast_rtc_interrupt;
	rt_request_irq(RTC_IRQ, (void *)rtc_handler, (void *)rtc_freq, 0);
	SET_INTR_GATE(ext_irq_vector(RTC_IRQ), rtai_rtc_timer_handler, rtai_rtc_timer_sysvec);
	rt_enable_irq(RTC_IRQ);
	CMOS_READ(RTC_INTR_FLAGS);
	return;
}

void rt_release_rtc(void)
{
	rt_disable_irq(RTC_IRQ);
	usr_rtc_handler = NULL;
	RESET_INTR_GATE(ext_irq_vector(RTC_IRQ), rtai_rtc_timer_sysvec);
	rt_release_irq(RTC_IRQ);
	rtai_cli();
	CMOS_WRITE(CMOS_READ(RTC_FREQ_SELECT), RTC_FREQ_SELECT);
	CMOS_WRITE(CMOS_READ(RTC_CONTROL),     RTC_CONTROL);
	rtai_sti();
	return;
}

EXPORT_SYMBOL(rt_request_rtc);
EXPORT_SYMBOL(rt_release_rtc);

#endif /* INCLUDED_BY_HAL_C */
