/**
 * This file is part of the RTAI project.
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org> 
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

#ifndef _RTAI_TYPES_H
#define _RTAI_TYPES_H

#define RTAI_SKIN_MAGIC  0x52544149

#include <nucleus/types.h>

#define RT_TIME_INFINITE XN_INFINITE
#define RT_TIME_NONBLOCK XN_NONBLOCK

#if defined(__KERNEL__) || defined(__RTAI_SIM__)

typedef xnticks_t RTIME;

#define rtai_h2obj_validate(h,m,t) \
((h) && ((t *)(h))->magic == (m) ? ((t *)(h)) : NULL)

#define rtai_h2obj_deleted(h,m,t) \
((h) && ((t *)(h))->magic == ~(m))

#define rtai_mark_deleted(t) ((t)->magic = ~(t)->magic)

#define rtai_handle_error(h,m,t) \
(rtai_h2obj_deleted(h,m,t) ? -EIDRM : -EINVAL)

#else /* !__KERNEL__ && !__RTAI_SIM__ */

typedef unsigned long long RTIME;

#endif /* __KERNEL__ || __RTAI_SIM__ */

typedef u_long rt_handle_t;

#endif /* !_RTAI_TYPES_H */
