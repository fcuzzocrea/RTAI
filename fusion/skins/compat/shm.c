/**
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org> 
 * @note Copyright (C) 2005 Nextream France S.A.
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

#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <compat/shm.h>


void *rt_shm_alloc(unsigned long name, int size, int suprt)
{
	void *p;

	/* FIXME(really shared):
	 * 	lookup name in list, and if exists return pointer 
	 * 	and inc usage count.
	 *	If not in list, alloc and add in the list.
	 */
	p = xnheap_alloc(&kheap,size);

	if (p)
		memset(p, 0, size);

	return p;
}


int rt_shm_free(unsigned long name)
{
	/* FIXME (memleak):
	 * 	lookup name in list, and if doesn't exist,
	 * 	return 0. Otherwise, decrement usage count.
	 * 	If usage count is 0, do xnheap_free(&kheap,p).
	 * 	return size freed.
	 */

	return 0;
}


int __shm_pkg_init (void)

{
    return 0;
}

void __shm_pkg_cleanup (void)

{
}


EXPORT_SYMBOL(rt_shm_alloc);
EXPORT_SYMBOL(rt_shm_free);
