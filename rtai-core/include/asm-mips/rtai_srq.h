/*
COPYRIGHT (C) 2000  Paolo Mantegazza (mantegazza@aero.polimi.it)
COPYRIGHT (C) 2001  Steve Papacharalambous (stevep@lineo.com)

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

/*
 * MIPS port - Steve Papacharalambous (stevep@lineo.com)
 */



#ifndef _RTAI_MIPS_SRQ_H_
#define _RTAI_MIPS_SRQ_H_

#define RTAI_SYS_VECTOR 0xfe000000

// the following function is adapted from Linux MIPS unistd.h 
static inline long long rtai_srq(unsigned long srq, unsigned long whatever)
{
	long long retval;
	unsigned long __rv_0;
	unsigned long __rv_1;

	__asm__ __volatile__
		("move\t$4,%3\n\t"
		 "li\t$2,%2\n\t"
		 "ori\t$2,$2,RTAI_SYS_VECTOR\n\t"
		 "syscall\n\t"
		 "move\t%0,$2\n\t"
		 "move\t%1,$3\n\t"
		: "=r" (__rv_0), "=r" (__rv_1)
		: "r" (srq), "r" (whatever)
		: "$2", "$3", "$4", "$5", "$6", "$7", "$8", \
		  "$9", "$10", "$11", "$12", "$13", "$14", \
		  "$15", "$24");
	((unsigned long *)&retval)[0] = __rv_0;
	((unsigned long *)&retval)[1] = __rv_1;
	return retval;
}  /* End function - rtai_srq */

static inline int rtai_open_srq(unsigned int label)
{
	return (int)rtai_srq(0, label);
}
#endif /* _RTAI_MIPS_SRQ_H_ */

