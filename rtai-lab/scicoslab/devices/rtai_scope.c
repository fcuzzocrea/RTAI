/*
COPYRIGHT (C) 2006  Roberto Bucher (roberto.bucher@supsi.ch)

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

#include <machine.h>
#include <scicos_block4.h>
#include <stdio.h>
#include <stdlib.h>
#include <rtai_netrpc.h>
#include <rtai_msg.h>
#include <rtai_mbx.h>
#include <string.h>
#include "rtmain.h"

#define MAX_RTAI_SCOPES               1000
#define MBX_RTAI_SCOPE_SIZE           5000

extern char *TargetMbxID;

void par_getstr(char * str, int par[], int init, int len);

static void init(scicos_block *block)
{
  char scopeName[20];
  char name[7];
  int nch = GetNin(block);
  int * ipar = GetIparPtrs(block);
  int nt = nch+1;
  MBX *mbx;

  par_getstr(scopeName,ipar,1,ipar[0]);
  rtRegisterScope(scopeName,nch);
  get_a_name(TargetMbxID,name);

  mbx = (MBX *) RT_typed_named_mbx_init(0,0,name,(MBX_RTAI_SCOPE_SIZE/(nt*sizeof(float)))*(nt*sizeof(float)),FIFO_Q);
  if(mbx == NULL) {
    fprintf(stderr, "Cannot init mailbox\n");
    exit_on_error();
  }

  *(block->work) = mbx;
}

static void inout(scicos_block *block)
{
  double *u;

  MBX * mbx = *(block->work);
  int ntraces=GetNin(block);
  struct {
    float t;
    float u[ntraces];
  } data;
  int i;

  double t=get_scicos_time();
  data.t=(float) t;
  for (i = 0; i < ntraces; i++) {
    u = block->inptr[i];
    data.u[i] = (float) u[0];
  }
  RT_mbx_send_if(0, 0, mbx, &data, sizeof(data));
}

static void end(scicos_block *block)
{
  char scopeName[10];
  int * ipar = GetIparPtrs(block);
  MBX * mbx = *(block->work);
  RT_named_mbx_delete(0, 0, mbx);
  par_getstr(scopeName,ipar,1,ipar[0]);
  printf("Scope %s closed\n",scopeName);
}

void rtscope(scicos_block *block,int flag)
{
  if (flag==1){          
    inout(block);
  }
  else if (flag==5){     /* termination */ 
    end(block);
  }
  else if (flag ==4){    /* initialisation */
    init(block);
  }
}


