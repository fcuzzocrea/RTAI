/*
COPYRIGHT (C) 2000  Paolo Mantegazza (mantegazza@aero.polimi.it)

Port to ARM Copyright (c) 2001 Alex Z�pke, SYSGO RTS GmbH (azu@sysgo.de)

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

#define RTAI_SRQ_MAGIC	"0x404404"

static inline long long rtai_srq(unsigned long srq, unsigned long whatever)
{
	long long retval;
	register unsigned long __sc_0 __asm__ ("r0") = srq;
	register unsigned long __sc_1 __asm__ ("r1") = whatever;

	__asm__ __volatile__ (
	"swi\t" RTAI_SRQ_MAGIC "\n\t"
		: "=r" (__sc_0), "=r" (__sc_1)
		: "0" (__sc_0), "1" (__sc_1)
		);
	((unsigned long *)&retval)[0] = __sc_0;
	((unsigned long *)&retval)[1] = __sc_1;
	return retval;
}

static inline int rtai_open_srq(unsigned int label)
{
	return (int)rtai_srq(0, label);
}
#endif
