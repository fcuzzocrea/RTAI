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
#include "peak.h"

extern devStr inpDevStr[];
extern devStr outDevStr[];

void inp_pcan_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5)
{
    int id=port-1;
    strcpy(inpDevStr[id].IOName,"pcan input");
    sscanf(sName,"%x",&(inpDevStr[id].nch));
    inpDevStr[id].dParam[0]=p1;
    inpDevStr[id].dParam[1]=p2;
    init_peak(inpDevStr[id].nch,p1,p2);
}

void out_pcan_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5)
{
    int id=port-1;
    strcpy(outDevStr[id].IOName,"pcan output");
    sscanf(sName,"%x",&(outDevStr[id].nch));
    outDevStr[id].dParam[0]=p1;
    outDevStr[id].dParam[1]=p2;
    init_peak(outDevStr[id].nch,p1,p2);
}

void out_pcan_output(int port, double * u,double t)
{
    int pcan_id = outDevStr[port-1].nch;
    write_peak(pcan_id,*u);
}

void inp_pcan_input(int port, double * y, double t)
{
    int pcan_id = inpDevStr[port-1].nch;
    *y=read_peak(pcan_id);
}

void inp_pcan_update(void)
{
}

void out_pcan_end(int port)
{
  int pcan_id = outDevStr[port-1].nch;
  end_peak(pcan_id);
  printf("%s closed\n",outDevStr[port-1].IOName);
}

void inp_pcan_end(int port)
{
  int pcan_id = inpDevStr[port-1].nch;
  end_peak(pcan_id);
  printf("%s closed\n",inpDevStr[port-1].IOName);
}



