/*
 *   ARTI -- RTAI-compatible Adeos-based Real-Time Interface. Based on
 *   the original RTAI layer for x86.
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

#ifndef _RTAI_ASM_I386_VECTORS_H
#define _RTAI_ASM_I386_VECTORS_H

#include <rtai_config.h>

/* On Linux x86, Adeos reserves vectors from 0xdf-0xee for domain
   usage. */

#define RTAI_APIC1_VECTOR  0xe1
#define RTAI_APIC2_VECTOR  0xe9

#define RTAI_APIC1_IPI     193
#define RTAI_APIC2_IPI     201

#define RTAI_SYS_VECTOR    0xe2
#define RTAI_LXRT_VECTOR   0xe3
#define RTAI_SHM_VECTOR    0xe4

#define __rtai_stringize0(_s_) #_s_
#define __rtai_stringize(_s_)  __rtai_stringize0(_s_)
#define __rtai_trap_call(_t_)  _t_
#define __rtai_do_trap0(_t_)   __rtai_stringize(int $ _t_)
#define __rtai_do_trap(_t_)    __rtai_do_trap0(__rtai_trap_call(_t_))

#define RTAI_DO_TRAP(v, r, a1, a2)  do { __asm__ __volatile__ ( __rtai_do_trap(v): : "a" (a1), "c" (a2), "d" (&r)); } while (0)

#endif /* !_RTAI_ASM_I386_VECTORS_H */
