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

#include <stdio.h>
#include <string.h>
#include "devstruct.h"

extern devStr inpDevStr[];
extern devStr outDevStr[];

void inp_xxx_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5)
{
    int id=port-1;
    inpDevStr[id].nch=nch;
    strcpy(inpDevStr[id].sName,sName);
    strcpy(inpDevStr[id].sParam,sParam);
    strcpy(inpDevStr[id].IOName,"xxx inp");
    inpDevStr[id].dParam[0]=p1;
    inpDevStr[id].dParam[1]=p2;
    inpDevStr[id].dParam[2]=p3;
    inpDevStr[id].dParam[3]=p4;
    inpDevStr[id].dParam[4]=p5;
}

void inp_xxx_input(int port, double * y, double t)
{
/*     *y=XXXX; */
}

void inp_xxx_update(void)
{
}

void inp_xxx_end(int port)
{
  printf("%s closed\n",inpDevStr[port-1].IOName);
}

void out_xxx_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5)
{
    int id=port-1;
    outDevStr[id].nch=nch;
    strcpy(outDevStr[id].sName,sName);
    strcpy(outDevStr[id].sParam,sParam);
    strcpy(outDevStr[id].IOName,"xxx out");
    outDevStr[id].dParam[0]=p1;
    outDevStr[id].dParam[1]=p2;
    outDevStr[id].dParam[2]=p3;
    outDevStr[id].dParam[3]=p4;
    outDevStr[id].dParam[4]=p5;
}

void out_xxx_output(int port, double * u,double t)
{ 
/*     XXXX=*u; */
}

void out_xxx_end(int port)
{
  printf("%s closed\n",outDevStr[port-1].IOName);
}




