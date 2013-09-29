/**
 *   @ingroup hal
 *   @file
 *
 *   Copyright: 2013 Paolo Mantegazza.
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

#ifndef _RTAI_ASM_X86_HAL_H
#define _RTAI_ASM_X86_HAL_H

#define RTAI_BUSY_ALIGN_RET_DELAY  500 // to be CONFIG_RTAI_BUSY_ALIGN_RET_DELAY

#ifdef __i386__
#include "rtai_hal_32.h"
#else
#include "rtai_hal_64.h"
#endif

struct calibration_data {
	unsigned long cpu_freq;
	unsigned long apic_freq;
	int latency;
	int latency_busy_align_ret_delay;
	int setup_time_TIMER_CPUNIT;
	int setup_time_TIMER_UNIT;
	int timers_tol[RTAI_NR_CPUS];
};

int rtai_calibrate_hard_timer(void);

#endif /* !_RTAI_ASM_X86_HAL_H */
