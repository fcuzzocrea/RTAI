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
 */

#ifndef _RTAI_PIPE_H
#define _RTAI_PIPE_H

#include <nucleus/pipe.h>
#include <rtai/types.h>

typedef struct rt_pipe_placeholder {
    rt_handle_t opaque;
} RT_PIPE_PLACEHOLDER;

#ifdef __KERNEL__

#define RTAI_PIPE_MAGIC 0x55550202

typedef xnpipe_mh_t RT_PIPE_MSG;

#define P_NORMAL  XNPIPE_NORMAL
#define P_URGENT  XNPIPE_URGENT

#define P_MSGPTR(msg)  xnpipe_m_data(msg)
#define P_MSGSIZE(msg) xnpipe_m_size(msg)

typedef struct rt_pipe {

    unsigned magic;		/* !< Magic code -- must be first. */

    xnholder_t link;		/* !< Link in flush queue. */

#define link2rtpipe(laddr) \
((RT_PIPE *)(((char *)laddr) - (int)(&((RT_PIPE *)0)->link)))

    int minor;			/* !< Device minor number.  */

    RT_PIPE_MSG *buffer;	/* !< Buffer used in byte stream mode. */

    size_t fillsz;		/* !< Bytes written to the buffer.  */

    u_long flushable;		/* !< Flush request flag. */

    rt_handle_t handle;		/* !< Handle in registry -- zero if unregistered. */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

#ifdef CONFIG_RTAI_OPT_FUSION
    pid_t cpid;			/* !< Creator's pid. */
#endif /* CONFIG_RTAI_OPT_FUSION */

} RT_PIPE;

#else /* !__KERNEL__ */

typedef RT_PIPE_PLACEHOLDER RT_PIPE;

#endif /* __KERNEL__ */

#ifdef __cplusplus
extern "C" {
#endif

/* Public interface. */

int rt_pipe_create(RT_PIPE *pipe,
		   const char *name,
		   int minor);

int rt_pipe_delete(RT_PIPE *pipe);

ssize_t rt_pipe_read(RT_PIPE *pipe,
		     void *buf,
		     size_t size,
		     RTIME timeout);

ssize_t rt_pipe_write(RT_PIPE *pipe,
		      const void *buf,
		      size_t size,
		      int mode);

ssize_t rt_pipe_stream(RT_PIPE *pipe,
		       const void *buf,
		       size_t size);
#ifdef __KERNEL__

ssize_t rt_pipe_receive(RT_PIPE *pipe,
			RT_PIPE_MSG **msg,
			RTIME timeout);

ssize_t rt_pipe_send(RT_PIPE *pipe,
		     RT_PIPE_MSG *msg,
		     size_t size,
		     int mode);

RT_PIPE_MSG *rt_pipe_alloc(size_t size);

int rt_pipe_free(RT_PIPE_MSG *msg);

int __pipe_pkg_init(void);

void __pipe_pkg_cleanup(void);

#else /* !__KERNEL__ */

int rt_pipe_bind(RT_PIPE *pipe,
		 const char *name);

static inline int rt_pipe_unbind (RT_PIPE *pipe)

{
    pipe->opaque = RT_HANDLE_INVALID;
    return 0;
}

#endif /* __KERNEL__ */

#ifdef __cplusplus
}
#endif

#endif /* !_RTAI_PIPE_H */
