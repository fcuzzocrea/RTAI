#ifndef _RT_MEM_MGR_H_
#define _RT_MEM_MGR_H_
//////////////////////////////////////////////////////////////////////////////
//
//      Copyright (©) 2000 Pierre Cloutier (Poseidon Controls Inc.),
//                         Steve Papacharalambous (Zentropic Computing Inc.),
//                         All rights reserved
//
// Authors:             Pierre Cloutier (pcloutier@poseidoncontrols.com)
//                      Steve Papacharalambous (stevep@zentropix.com)
//
// Original date:       Mon 14 Feb 2000
//
// Id:                  @(#)$Id: rt_mem_mgr.h,v 1.1 2004/06/06 14:12:05 rpm Exp $
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
//
// Dynamic Memory Management for Real Time Linux.
//
///////////////////////////////////////////////////////////////////////////////

// ----------------------------------------------------------------------------

extern void *rt_malloc(unsigned int size);
extern void rt_free(void *addr);
extern void display_chunk(void *addr);
extern int rt_mem_init(void);
extern void rt_mem_end(void);
extern void rt_mmgr_stats(void);

#undef DBG
#ifdef ZDEBUG
#define DBG(fmt, args...)  rtai_print_to_screen("<%s %d> " fmt, __FILE__, __LINE__ ,##args)
#else
#define DBG(fmt, args...)
#endif

// ---------------------------------< eof >------------------------------------
#endif  // _RT_MEM_MGR_H_
