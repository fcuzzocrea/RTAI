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
extern int pinp_cnt;
extern int pout_cnt;

int inp_rtai_fifo_init(int nch,char * sName,char * sParam,double p1,
		       double p2, double p3, double p4, double p5)
{
  int port=pinp_cnt++;
  inpDevStr[port].nch=nch;
  strcpy(inpDevStr[port].IOName,"rtai_fifo inp");

  return(port);
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
  printf("%s closed\n",inpDevStr[port].IOName);
}

int out_rtai_fifo_init(int nch, int fifon)
{
  int port=pout_cnt++;
  outDevStr[port].nch=nch;
  outDevStr[port].i1=fifon;

  strcpy(outDevStr[port].IOName,"rtai_fifo out");

  rtf_create(fifon,FIFO_SIZE);
  rtf_reset(fifon);

  return(port);
}

void out_rtai_fifo_output(int port, double * u,double t)
{
  int fifo_id=outDevStr[port].i1;
  int ntraces=outDevStr[port].nch;
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
  rtf_destroy(outDevStr[port].i1);
  printf("%s closed\n",outDevStr[port].IOName);
}




