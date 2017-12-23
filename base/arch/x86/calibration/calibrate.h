/*
 * Copyright (C) 1999-2017 Paolo Mantegazza <mantegazza@aero.polimi.it>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define CALSRQ        0xcacca

#define CAL_8254      1
#define KLATENCY      2
#define KTHREADS      3
#define END_KLATENCY  4
#define FREQ_CAL      5
#define END_FREQ_CAL  6
#define BUS_CHECK     7
#define END_BUS_CHECK 8
#define GET_PARAMS    9

#define PARPORT       0x370

#define MAXARGS       4
#define STACKSIZE     5000
#define FIFOBUFSIZE   1000
#define INILOOPS      100

struct params_t { unsigned long 
	mp,
	setup_time_8254, 
	latency_8254,  
	freq_apic,
	latency_apic,
	setup_time_apic,
	calibrated_apic_freq,
	cpu_freq,
	calibrated_cpu_freq,
	clock_tick_rate,
	latch;
};

struct times_t { 
	unsigned long long cpu_time;
	unsigned long apic_time;
	int intrs;
};
