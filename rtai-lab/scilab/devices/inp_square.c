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

static int isUpPeriod(double t,double P,double S)
{
    double val;
    val=t/P;
    val=(val-(int) val)*P;
    if ((1.0*val) < S) return 1;
    else               return 0;
}
 
void inp_square_init(int port,int nch,char * sName,char * sParam,double p1,
                         double p2, double p3, double p4, double p5)
{
    int id=port-1;
    strcpy(inpDevStr[id].IOName,"Square input");
    inpDevStr[id].dParam[0]=p1;
    inpDevStr[id].dParam[1]=p2;
    inpDevStr[id].dParam[2]=p3;
    inpDevStr[id].dParam[3]=p4;
    inpDevStr[id].dParam[4]=p5;

}

void inp_square_input(int port, double * y, double t)
{
    int id=port-1;
    if(t<inpDevStr[id].dParam[4]) y[0]=0.0;
else
    y[0]=inpDevStr[id].dParam[0]*isUpPeriod(t-inpDevStr[id].dParam[4],
                                            inpDevStr[id].dParam[1],
                                            inpDevStr[id].dParam[2])+inpDevStr[id].dParam[3];
}

void inp_square_update(void)
{
}

void inp_square_end(int port)
{
  printf("%s closed\n",inpDevStr[port-1].IOName);
}
