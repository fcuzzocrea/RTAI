/**
 * @file
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

#ifndef _RTAI_PIPE_H
#define _RTAI_PIPE_H

#include <nucleus/dbridge.h>

#ifdef __KERNEL__

#include <rtai/types.h>

#define RTAI_PIPE_MAGIC 0x55550202

typedef xnbridge_mh_t RT_PIPE_MSG;

#define RT_PIPE_NORMAL XNBRIDGE_NORMAL
#define RT_PIPE_URGENT XNBRIDGE_URGENT

#define RT_PIPE_MSGPTR(msg)  xnbridge_m_data(msg)
#define RT_PIPE_MSGSIZE(msg) xnbridge_m_size(msg)

typedef struct rt_pipe {

    unsigned magic;		/* !< Magic code -- must be first. */

    xnholder_t link;		/* !< Link in flush queue. */

#define link2pipe(laddr) \
((RT_PIPE *)(((char *)laddr) - (int)(&((RT_PIPE *)0)->link)))

    int minor;			/* !< Device minor number.  */

    RT_PIPE_MSG *buffer;	/* !< Buffer used in byte stream mode. */

    size_t fillsz;		/* !< Bytes written to the buffer.  */

    u_long flushable;		/* !< Flush request flag. */

} RT_PIPE;

#ifdef __cplusplus
extern "C" {
#endif

int __pipe_pkg_init(void);

void __pipe_pkg_cleanup(void);

/* Public interface. */

int rt_pipe_open(RT_PIPE *pipe,
		 int minor);

int rt_pipe_close(RT_PIPE *pipe);

ssize_t rt_pipe_read(RT_PIPE *pipe,
		     RT_PIPE_MSG **msg,
		     RTIME timeout);

ssize_t rt_pipe_write(RT_PIPE *pipe,
		      RT_PIPE_MSG *msg,
		      size_t size,
		      int flags);

ssize_t rt_pipe_stream(RT_PIPE *pipe,
		       const void *buf,
		       size_t size);

RT_PIPE_MSG *rt_pipe_alloc(size_t size);

int rt_pipe_free(RT_PIPE_MSG *msg);

#ifdef __cplusplus
}
#endif

#else /* !__KERNEL__ */

#endif /* __KERNEL__ */

#endif /* !_RTAI_PIPE_H */
