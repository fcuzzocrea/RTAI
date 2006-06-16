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
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <rtai_netrpc.h>
#include <rtai_msg.h>
#include <rtai_mbx.h>

#include "rtmain.h"

struct MbxOwS{
  int nch;
  char mbxName[10];
  MBX * mbx;
  long tNode;
  long tPort;
};

void * out_mbx_ovrwr_send_init(int nch,char * sName,char * IP)
{
  struct MbxOwS * mbx = (struct MbxOwS *) malloc(sizeof(struct MbxOwS));
  mbx->nch=nch;
  strcpy(mbx->mbxName,sName);

  struct sockaddr_in addr;

  if(!strcmp(IP,"0")) {
    mbx->tNode = 0;
    mbx->tPort = 0;
  }
  else {
    inet_aton(IP, &addr.sin_addr);
    mbx->tNode = addr.sin_addr.s_addr;
    while ((mbx->tPort = rt_request_port(mbx->tNode)) <= 0 && mbx->tPort != -EINVAL);
  }

  mbx->mbx = (MBX *) RT_typed_named_mbx_init(mbx->tNode,mbx->tPort,sName,nch*sizeof(double),FIFO_Q);

  if(mbx->mbx == NULL) {
    fprintf(stderr, "Error in getting %s mailbox address\n", sName);
    exit_on_error();
  }

  return((void *) mbx);
}

void out_mbx_ovrwr_send_output(void * ptr, double * u,double t)
{
  struct MbxOwS * mbx = (struct MbxOwS *) ptr;
  int ntraces = mbx->nch;
  struct{
    double u[ntraces];
  } data;
  int i;

  for(i=0;i<ntraces;i++){
    data.u[i] = u[i];
  }
  RT_mbx_ovrwr_send(mbx->tNode, mbx->tPort,mbx->mbx,&data,sizeof(data));
}

void out_mbx_ovrwr_send_end(void * ptr)
{
  struct MbxOwS * mbx = (struct MbxOwS *) ptr;

  RT_named_mbx_delete(mbx->tNode, mbx->tPort,mbx->mbx);
  printf("OVRWR MBX %s closed\n",mbx->mbxName);
  if(mbx->tNode){
    rt_release_port(mbx->tNode,mbx->tPort);
  }
  free(mbx);
}




