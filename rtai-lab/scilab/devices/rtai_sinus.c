/*
COPYRIGHT (C) 2006  Roberto Bucher (roberto.bucher@supsi.ch)

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
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
*/

#include <machine.h>
#include <scicos_block.h>

static void init(scicos_block *block)
{
   block->outptr[0][0]=0.0;
}

static void inout(scicos_block *block)
{
  double w,pi=3.1415927;
  double t = get_scicos_time();

   if (t<block->rpar[4]) block->outptr[0][0]=0.0;
   else {
     w=2*pi*block->rpar[1]*(t-block->rpar[4])-block->rpar[2];
     block->outptr[0][0]=block->rpar[0]*sin(w)+block->rpar[3];
   }
}

static void end(scicos_block *block)
{
   block->outptr[0][0]=0.0;
}



void rtsinus(scicos_block *block,int flag)
{
  if (flag==1){          /* set output */
    inout(block);
  }
  else if (flag==5){     /* termination */ 
    end(block);
  }
  else if (flag ==4){    /* initialisation */
    init(block);
  }
}


