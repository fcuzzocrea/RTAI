/*
 * Copyright (C) 2001,2002,2003,2004,2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
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

#ifndef _RTAI_NUCLEUS_FUSION_H
#define _RTAI_NUCLEUS_FUSION_H

#include <rtai_config.h>

/* Thread priority levels. */
#define FUSION_LOW_PRIO     1
#define FUSION_HIGH_PRIO    99
/* Extra level for IRQ servers in user-space. */
#define FUSION_IRQ_PRIO     (FUSION_HIGH_PRIO + 1)

#define FUSION_MIN_PRIO     FUSION_LOW_PRIO
#define FUSION_MAX_PRIO     FUSION_IRQ_PRIO

#ifdef __KERNEL__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int xnfusion_mount(void);

int xnfusion_umount(void);

int xnfusion_attach(void);

int xnfusion_detach(void);

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* __KERNEL__ */

#endif /* !_RTAI_NUCLEUS_FUSION_H */
