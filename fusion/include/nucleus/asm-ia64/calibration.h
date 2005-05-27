/*
 * Copyright &copy; 2004 Philippe Gerum <rpm@xenomai.org>.
 * Copyright &copy; 2004 The HYADES project <http://www.hyades-itea.org>
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

#ifndef _RTAI_ASM_IA64_CALIBRATION_H
#define _RTAI_ASM_IA64_CALIBRATION_H

#include <rtai_config.h>

static inline unsigned long xnarch_get_sched_latency (void)

{
#if CONFIG_RTAI_HW_SCHED_LATENCY != 0
#define __sched_latency CONFIG_RTAI_HW_SCHED_LATENCY
#else /* CONFIG_RTAI_HW_SCHED_LATENCY unspecified */
#ifdef CONFIG_IA64_HP_SIM
#define __sched_latency 1281000
#else /* !CONFIG_IA64_HP_SIM */
#define __sched_latency 6000
#endif /* CONFIG_IA64_HP_SIM */
#endif /* CONFIG_RTAI_HW_SCHED_LATENCY */

    return __sched_latency;
}

#undef __sched_latency

#endif /* !_RTAI_ASM_IA64_CALIBRATION_H */
