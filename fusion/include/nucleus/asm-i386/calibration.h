/*
 * Copyright (C) 2001,2002,2003,2004,2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

#ifndef _RTAI_ASM_I386_CALIBRATION_H
#define _RTAI_ASM_I386_CALIBRATION_H

#include <rtai_config.h>
#include <asm/processor.h>

#define __bogomips (current_cpu_data.loops_per_jiffy/(500000/HZ))

static inline unsigned long xnarch_get_sched_latency (void)

{
#if CONFIG_RTAI_HW_SCHED_LATENCY != 0
#define __sched_latency CONFIG_RTAI_HW_SCHED_LATENCY
#else

#if CONFIG_X86_LOCAL_APIC
#define __sched_latency 6800
#else /* !CONFIG_X86_LOCAL_APIC */

/* Use the bogomips formula to identify low-end x86 boards when using
   the 8254 PIT. The following is still grossly experimental and needs
   work (i.e. more specific cases), but the approach is definitely
   saner than previous attempts to guess such value dynamically. */
#define __sched_latency (__bogomips < 200 ? 20500 : \
                         __bogomips < 2000 ? 14100 : \
			 7300)

#endif /* CONFIG_X86_LOCAL_APIC */
#endif /* CONFIG_RTAI_HW_SCHED_LATENCY */

    return __sched_latency;
}

#undef __sched_latency
#undef __bogomips

#endif /* !_RTAI_ASM_I386_CALIBRATION_H */
