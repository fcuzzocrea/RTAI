/*
 * Project: rtai_cpp - RTAI C++ Framework 
 *
 * File: $Id: rtai_wrapper.c,v 1.1 2004/06/06 14:10:05 rpm Exp $
 *
 * Copyright: (C) 2001,2002 Erwin Rol <erwin@muffin.org>
 *
 * Licence:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 */
#include "rtai_wrapper.h"
#include <rtai.h>

void __rt_get_global_lock(void){
	rt_get_global_lock();
}
 
void __rt_release_global_lock(void){
	rt_release_global_lock();
}

int __hard_cpu_id( void ){
	return hard_cpu_id();
}

#ifdef CONFIG_RTAI_TRACE

void __trace_destroy_event( int id ){
    trace_destroy_event( id );
}

int __trace_create_event( const char* name, void* p){
    return trace_create_event( (char *)name, NULL, CUSTOM_EVENT_FORMAT_TYPE_NONE, (char *)p);
}

int __trace_raw_event( int id, int size, void* p){
    return trace_raw_event( id, size, p);
}

#endif /* CONFIG_RTAI_TRACE */
