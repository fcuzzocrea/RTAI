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
#include "devstruct.h"
#include "rtmain.h"

extern devStr inpDevStr[];
extern devStr outDevStr[];

void inp_extdata_init(int port,int nch,char * sName,char * sParam,double p1,
		      double p2, double p3, double p4, double p5)
{
  FILE * fp;
  double * pData;
  int i;
  int id=port-1;

  if(nch==0) {
    fprintf(stderr, "Error - Data length is 0!\n");
    exit_on_error();
  }
  strcpy(inpDevStr[id].IOName,"extdata");
  inpDevStr[id].ptr1=(void *)calloc(nch,sizeof(double));
  pData=(double *) inpDevStr[id].ptr1;
  fp=fopen(sName,"r");
  if(fp!=NULL){
    inpDevStr[id].i1=0;
    for(i=0;i<nch;i++) {
      if(feof(fp)) break;
      fscanf(fp,"%lf",&pData[i]);
    }
    inpDevStr[id].nch=i;
    fclose(fp);
  }
  else{
    fprintf(stderr, "File %s not found!\n",sName);
    inpDevStr[id].i1=-1;
    exit_on_error();
  }
}

void inp_extdata_input(int port, double * y, double t)
{
  int id=port-1;
  int index=inpDevStr[id].i1;
  if(index>=0) {
    double * pData=(double *) inpDevStr[id].ptr1;
    y[0]=pData[index];
    index=(index+1) % inpDevStr[id].nch;
    inpDevStr[id].i1=index;
  }
  else y[0]=0.0;
}

void inp_extdata_update(void)
{
}

void inp_extdata_end(int port)
{
  printf("%s closed\n",inpDevStr[port-1].IOName);
}



