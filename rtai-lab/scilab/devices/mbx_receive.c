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
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <rtai_netrpc.h>
#include <rtai_msg.h>
#include <rtai_mbx.h>

#include "devstruct.h"
#include "rtmain.h"

extern devStr inpDevStr[];
extern int pinp_cnt;

int inp_mbx_receive_init(int nch,char * sName,char * IP)
{
  long Target_Node;
  long Target_Port=0;
  MBX * mbx;
  struct sockaddr_in addr;
  int port=pinp_cnt++;

  inpDevStr[port].nch=nch;
  strcpy(inpDevStr[port].sName,sName);
  strcpy(inpDevStr[port].sParam,IP);
  strcpy(inpDevStr[port].IOName,"mbx_receive inp");

  if(!strcmp(IP,"0")) {
    Target_Node = 0;
    Target_Port = 0;
  }
  else {
    inet_aton(IP, &addr.sin_addr);
    Target_Node = addr.sin_addr.s_addr;
    while ((Target_Port = rt_request_port_id(Target_Node,nam2num(sName))) <= 0 && Target_Port != -EINVAL);
  }

  mbx = (MBX *) RT_typed_named_mbx_init(Target_Node,Target_Port,sName,nch*sizeof(double),FIFO_Q);

  if(mbx == NULL) {
    fprintf(stderr, "Error in getting %s mailbox address\n", sName);
    exit_on_error();
  }
  inpDevStr[port].ptr1 = (void *) mbx;
  inpDevStr[port].ptr2 = calloc(nch,sizeof(double));
  inpDevStr[port].l1 = Target_Node;
  inpDevStr[port].l2 = Target_Port;

  return(port);
}

void inp_mbx_receive_input(int port, double * y, double t)
{
  MBX *mbx = (MBX *) inpDevStr[port].ptr1;
  double * old_val = (double *) inpDevStr[port].ptr2;
  int ntraces = inpDevStr[port].nch;
  struct{
    double u[ntraces];
  } data;
  int i;

  if(!RT_mbx_receive(inpDevStr[port].l1, inpDevStr[port].l2, mbx ,&data, sizeof(data))) {
    for(i=0;i<ntraces;i++) {
      old_val[i] = data.u[i];
    }
  }
  for(i=0;i<ntraces;i++) y[i] = old_val[i];
}

void inp_mbx_receive_update(void)
{
}

void inp_mbx_receive_end(int port)
{
  MBX *mbx;

  mbx = (MBX *) inpDevStr[port].ptr1;
  RT_named_mbx_delete(inpDevStr[port].l1, inpDevStr[port].l2,mbx);
  free(inpDevStr[port].ptr2);
  if(inpDevStr[port].l1) rt_release_port(inpDevStr[port].l1, inpDevStr[port].l2);
  printf("%s closed\n",inpDevStr[port].IOName);
}


