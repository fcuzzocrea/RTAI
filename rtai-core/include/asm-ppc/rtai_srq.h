/*
COPYRIGHT (C) 2000  Paolo Mantegazza (mantegazza@aero.polimi.it)

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
*/


#ifndef _RTAI_SRQ_H_
#define _RTAI_SRQ_H_

// the following function is adapted from Linux PPC unistd.h 
static inline long long rtai_srq(unsigned long srq, unsigned long whatever)
{
	long long retval;
	register unsigned long __sc_0 __asm__ ("r0");
	register unsigned long __sc_3 __asm__ ("r3");
	register unsigned long __sc_4 __asm__ ("r4");

	__sc_0 = (__sc_3 = srq) + (__sc_4 = whatever);
	__asm__ __volatile__
		("trap         \n\t"
		: "=&r" (__sc_3), "=&r" (__sc_4)
		: "0"   (__sc_3), "1"   (__sc_4),
	          "r"   (__sc_0)
		: "r0", "r3", "r4" );
	((unsigned long *)&retval)[0] = __sc_3;
	((unsigned long *)&retval)[1] = __sc_4;
	return retval;
}

static inline int rtai_open_srq(unsigned int label)
{
	return (int)rtai_srq(0, label);
}
#endif
