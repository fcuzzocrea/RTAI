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

#define FIFO_SIZE   50000

struct Fifo{
  int nch;
  int fifon;
};

void * inp_rtai_fifo_init(int nch,char * sName,char * sParam,double p1,
		       double p2, double p3, double p4, double p5)
{
  return(NULL);
}

void inp_rtai_fifo_input(void * ptr, double * y, double t)
{
  /*     *y=XXXX; */
}

void inp_rtai_fifo_update(void)
{
}

void inp_rtai_fifo_end(void * ptr)
{
}

void * out_rtai_fifo_init(int nch, int fifon)
{
  struct Fifo * fifo = (struct Fifo *) malloc(sizeof(struct Fifo));
  fifo->nch=nch;
  fifo->fifon=fifon;

  rtf_create(fifon,FIFO_SIZE);
  rtf_reset(fifon);

  return((void *) fifo);
}

void out_rtai_fifo_output(void * ptr, double * u,double t)
{
  struct Fifo * fifo = (struct Fifo *) ptr;
  int ntraces=fifo->nch;
  struct {
    float t;
    float u[ntraces];
  } data;
  int i;

  data.t=(float) t;
  for (i = 0; i < ntraces; i++) {
    data.u[i] = (float) u[i];
  }
  rtf_put(fifo->fifon,&data, sizeof(data));
}

void out_rtai_fifo_end(void * ptr)
{
  struct Fifo * fifo = (struct Fifo *) ptr;
  rtf_destroy(fifo->fifon);
  printf("FIFO %d closed\n",fifo->fifon);
  free(fifo);
}




