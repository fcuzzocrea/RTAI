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
#include <stdlib.h>
#include <rtai_fifos.h>
#include "devstruct.h"

#define FIFO_SIZE   50000

extern devStr inpDevStr[];
extern devStr outDevStr[];

void inp_rtai_fifo_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5)
{
    int id=port-1;
    inpDevStr[id].nch=nch;
    strcpy(inpDevStr[id].IOName,"rtai_fifo inp");
}

void inp_rtai_fifo_input(int port, double * y, double t)
{
/*     *y=XXXX; */
}

void inp_rtai_fifo_update(void)
{
}

void inp_rtai_fifo_end(int port)
{
  printf("%s closed\n",inpDevStr[port-1].IOName);
}

void out_rtai_fifo_init(int port,int nch,char * sName,char * sParam,double p1,
			double p2, double p3, double p4, double p5)
{
  int id=port-1;
  int fifo_id;
  outDevStr[id].nch=nch;
  fifo_id=atoi(sName);
  outDevStr[id].i1=fifo_id;

  strcpy(outDevStr[id].IOName,"rtai_fifo out");

  rtf_create(fifo_id,FIFO_SIZE);
  rtf_reset(fifo_id);
}

void out_rtai_fifo_output(int port, double * u,double t)
{
  int fifo_id=outDevStr[port-1].i1;
  int ntraces=outDevStr[port-1].nch;
  struct {
    float t;
    float u[ntraces];
  } data;
  int i;

  data.t=(float) t;
  for (i = 0; i < ntraces; i++) {
    data.u[i] = (float) u[i];
  }
  rtf_put(fifo_id,&data, sizeof(data));
}

void out_rtai_fifo_end(int port)
{
  rtf_destroy(outDevStr[port-1].i1);
  printf("%s closed\n",outDevStr[port-1].IOName);
}




