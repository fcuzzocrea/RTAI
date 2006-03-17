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
#include <stdlib.h>
#include <rtai_netrpc.h>
#include <rtai_msg.h>
#include <rtai_mbx.h>
#include <string.h>
#include "rtmain.h"

#define MAX_RTAI_SCOPES               1000
#define MBX_RTAI_SCOPE_SIZE           5000

struct scope{
  int nch;
  char scopeName[20];
  MBX * mbx;
};

extern char *TargetMbxID;

void * out_rtai_scope_init(int nch,char * sName)
{
  struct scope * scp = (struct scope *) malloc(sizeof(struct scope));
  scp->nch=nch;
  strcpy(scp->scopeName,sName);
  char name[7];
  int nt=nch + 1;
  rtRegisterScope(sName,nch);
  get_a_name(TargetMbxID,name);

  scp->mbx = (MBX *) RT_typed_named_mbx_init(0,0,name,(5000/(nt*sizeof(float)))*(nt*sizeof(float)),FIFO_Q);
  if(scp->mbx == NULL) {
    fprintf(stderr, "Cannot init mailbox\n");
    exit_on_error();
  }

  return((void *) scp);
}

void out_rtai_scope_output(void * ptr, double * u, double t)
{
  struct scope * scp = (struct scope *) ptr;
  int ntraces=scp->nch;
  struct {
    float t;
    float u[ntraces];
  } data;
  int i;

  data.t=(float) t;
  for (i = 0; i < ntraces; i++) {
    data.u[i] = (float) u[i];
  }
  RT_mbx_send_if(0, 0, scp->mbx, &data, sizeof(data));
}

void out_rtai_scope_end(void * ptr)
{
  struct scope * scp = (struct scope *) ptr;
  RT_named_mbx_delete(0, 0, scp->mbx);
  printf("Scope %s closed\n",scp->scopeName);
  free(scp);
}



