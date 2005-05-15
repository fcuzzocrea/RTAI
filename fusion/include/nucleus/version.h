/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _RTAI_NUCLEUS_VERSION_H
#define _RTAI_NUCLEUS_VERSION_H

#include <rtai_config.h>

#define FUSION_VERSION(maj,min,rev)  (((maj)<<16)|((min)<<8)|(rev))

#define FUSION_VERSION_CODE  FUSION_VERSION(CONFIG_RTAI_VERSION_MAJOR, \
					    CONFIG_RTAI_VERSION_MINOR, \
					    CONFIG_RTAI_REVISION_LEVEL)
#define FUSION_VERSION_NAME  "Circles"

#endif /* _RTAI_NUCLEUS_VERSION_H */
