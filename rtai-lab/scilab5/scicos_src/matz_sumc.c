/*  Scicos
*
*  Copyright (C) 2015 INRIA - METALAU Project <scicos@inria.fr>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, see <http://www.gnu.org/licenses/>.
*
* See the file ./license.txt
*/
/*--------------------------------------------------------------------------*/ 
#include "scicos_block4.h"
#include "dynlib_scicos_blocks.h"
/*--------------------------------------------------------------------------*/ 
SCICOS_BLOCKS_IMPEXP void matz_sumc(scicos_block *block,int flag)
{
	int j = 0;
	int mu = GetInPortRows(block,1);
	int nu = GetInPortCols(block,1);

	double *ur = GetRealInPortPtrs(block,1);
	double *ui = GetImagInPortPtrs(block,1);
	double *yr = GetRealOutPortPtrs(block,1);
	double *yi = GetImagOutPortPtrs(block,1);

	for(j=0;j<nu;j++)
	{
		double dr = 0.;
		double di = 0.;
		int i = 0;
		for (i=0;i<mu;i++)
		{
			int ij = i+j*mu;
			dr += *(ur+ij);
			di += *(ui+ij);
		}
		*(yr+j)=dr;
		*(yi+j)=di;
	}
}
/*--------------------------------------------------------------------------*/ 
