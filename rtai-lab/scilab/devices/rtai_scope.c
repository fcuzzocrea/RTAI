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
#include <rtai_netrpc.h>
#include <rtai_msg.h>
#include <rtai_mbx.h>
#include <string.h>
#include "rtmain.h"
#include "devstruct.h"

#define MAX_RTAI_SCOPES               1000
#define MBX_RTAI_SCOPE_SIZE           5000

extern char *TargetMbxID;
extern devStr outDevStr[];

void out_rtai_scope_init(int port,int nch,char * sName,char * sParam,double p1,
			 double p2, double p3, double p4, double p5)
{
    MBX * mbx;
    outDevStr[port-1].nch=nch;
    strcpy(outDevStr[port-1].IOName,"Scope");
    char name[7];
    int nt=nch + 1;
    rtRegisterScope(sName,nch);
    get_a_name(TargetMbxID,name);

    mbx = (MBX *) RT_typed_named_mbx_init(0,0,name,(5000/(nt*sizeof(float)))*(nt*sizeof(float)),FIFO_Q);
    if(mbx == NULL) {
      fprintf(stderr, "Cannot init mailbox\n");
      exit_on_error();
    }
    outDevStr[port-1].ptr1 = (void *) mbx;
}

void out_rtai_scope_output(int port, double * u, double t)
{
    MBX *mbx = (MBX *) outDevStr[port-1].ptr1;
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
    RT_mbx_send_if(0, 0, mbx, &data, sizeof(data));
}

void out_rtai_scope_end(int port)
{
  MBX *mbx = (MBX *) outDevStr[port-1].ptr1;
  RT_named_mbx_delete(0, 0, mbx);
  printf("%s closed\n",outDevStr[port-1].IOName);
}



