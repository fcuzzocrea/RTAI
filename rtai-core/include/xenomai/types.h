/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * As a special exception, the RTAI project gives permission
 * for additional uses of the text contained in its release of
 * Xenomai.
 *
 * The exception is that, if you link the Xenomai libraries with other
 * files to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the Xenomai libraries code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public
 * License.
 *
 * This exception applies only to the code released by the
 * RTAI project under the name Xenomai.  If you copy code from other
 * RTAI project releases into a copy of Xenomai, as the General Public
 * License permits, the exception does not apply to the code that you
 * add in this way.  To avoid misleading anyone as to the status of
 * such modified files, you must delete this exception notice from
 * them.
 *
 * If you write modifications of your own for Xenomai, it is your
 * choice whether to permit this exception to apply to your
 * modifications. If you do not wish that, delete this exception
 * notice.
 */

#ifndef _xenomai_types_h
#define _xenomai_types_h

#include "asm/rtai_xeno.h"

typedef unsigned long xnsigmask_t;

#define XN_OK     0
#define XN_ERROR (-1)

typedef unsigned long long xnticks_t;

typedef unsigned long long xntime_t; /* ns */

#define XN_INFINITE   (0)
#define XN_NONBLOCK   ((xnticks_t)-1)

#define testbits(flags,mask) ((flags) & (mask))
#define setbits(flags,mask)  xnarch_atomic_set_mask(&(flags),mask)
#define clrbits(flags,mask)  xnarch_atomic_clear_mask(&(flags),mask)

typedef atomic_flags_t xnflags_t;

#ifndef NULL
#define NULL 0
#endif

#define XNOBJECT_NAME_LEN 32

#define minval(a,b) ((a) < (b) ? (a) : (b))
#define maxval(a,b) ((a) > (b) ? (a) : (b))

#define XN_NBBY 8 /* Assume a byte is always 8 bits :o> */

#ifdef __cplusplus
extern "C" {
#endif

const char *xnpod_fatal_helper(const char *format, ...);

#ifdef __cplusplus
}
#endif

#define xnpod_fatal(format,args...) \
do { \
   const char *panic = xnpod_fatal_helper(format,##args); \
   xnarch_halt(panic); \
} while (0)

#endif /* !_xenomai_types_h */
