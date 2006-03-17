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
#include <stdlib.h>
#include "rtmain.h"

struct extData{
  int nData;
  double * pData;
  int cnt;
};

void * inp_extdata_init(int nch,char * sName)
{
  FILE * fp;
  int i;

  if(nch==0) {
    fprintf(stderr, "Error - Data length is 0!\n");
    exit_on_error();
  }

  struct extData * data;
  data=(struct extData *) malloc(sizeof(struct extData));

  data->pData=(double *) calloc(nch,sizeof(double));
  fp=fopen(sName,"r");
  if(fp!=NULL){
    data->cnt=0;
    for(i=0;i<nch;i++) {
      if(feof(fp)) break;
      fscanf(fp,"%lf",&(data->pData[i]));
    }
    data->nData=nch;
    fclose(fp);
  }
  else{
    fprintf(stderr, "File %s not found!\n",sName);
    data->cnt=-1;
    exit_on_error();
  }

  return((void *) data);
}

void inp_extdata_input(void * ptr, double * y, double t)
{
  struct extData * data = (struct extData *) ptr;
  if(data->nData>=0) {
    y[0]=data->pData[data->cnt];
    data->cnt = data->cnt % data->nData;
  }
  else y[0]=0.0;
}

void inp_extdata_update(void)
{
}

void inp_extdata_end(void * ptr)
{
  struct extData * data = (struct extData *) ptr;
  free(data->pData);
  free(data);
  printf("EXTDATA closed\n");
}



