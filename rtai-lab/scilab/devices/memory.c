/*
  COPYRIGHT (C) 2003  Roberto Bucher (roberto.bucher@die.supsi.ch)

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

#include <string.h>
#include <stdio.h>
#include "devstruct.h"

extern devStr inpDevStr[];
extern devStr outDevStr[];

static double memory[10];

void inp_mem_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5)
{
    inpDevStr[port-1].nch=nch;
    strcpy(inpDevStr[port-1].IOName,"memory input");
    memory[nch]=0.0;
}

void out_mem_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5)
{
    outDevStr[port-1].nch=nch;
    strcpy(outDevStr[port-1].IOName,"memory output");
    memory[nch]=0.0;
}

void out_mem_output(int port, double * u,double t)
{ 
    memory[outDevStr[port-1].nch]=*u;
}

void inp_mem_input(int port, double * y, double t)
{
    *y=memory[inpDevStr[port-1].nch];
}

void inp_mem_update(void)
{
}

void out_mem_end(int port)
{
  printf("%s closed\n",outDevStr[port-1].IOName);
}

void inp_mem_end(int port)
{
  printf("%s closed\n",inpDevStr[port-1].IOName);
}



