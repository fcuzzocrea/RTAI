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

#include <math.h>
#include <string.h>
#include <stdio.h>
#include "devstruct.h"

#define PI 2*acos(0)

extern devStr inpDevStr[];

void inp_sinus_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5)
{
    int id=port-1;
    inpDevStr[id].nch=nch;
    strcpy(inpDevStr[id].sName,sName);
    strcpy(inpDevStr[id].IOName,"Sinus input");
    inpDevStr[id].dParam[0]=p1;
    inpDevStr[id].dParam[1]=p2;
    inpDevStr[id].dParam[2]=p3;
    inpDevStr[id].dParam[3]=p4;
    inpDevStr[id].dParam[4]=p5;
}

void inp_sinus_input(int port, double * y, double t)
{
  double angle;
  int id=port-1;
  double w;

  if(t<inpDevStr[id].dParam[4]) y[0]=0.0;
  else {
    w=2*PI*inpDevStr[id].dParam[1];
    angle=(t-inpDevStr[id].dParam[4])*w-inpDevStr[id].dParam[2];
    y[0]=inpDevStr[id].dParam[0]*sin(angle)+inpDevStr[id].dParam[3];
  }
}

void inp_sinus_update(void)
{
}

void inp_sinus_end(int port)
{
  printf("%s closed\n",inpDevStr[port-1].IOName);
}



