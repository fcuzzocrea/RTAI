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
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <rtai_netrpc.h>
#include <rtai_sem.h>

#include "devstruct.h"

extern devStr inpDevStr[];
extern devStr outDevStr[];

void inp_rtai_sem_init(int port,char * sName,char * IP)
{
  long Target_Node;
  long Target_Port=0;
  SEM * sem;
  struct sockaddr_in addr;

  int id=port-1;
  strcpy(inpDevStr[id].IOName,"rtai_sem inp");

  if(!strcmp(IP,"0")) {
    Target_Node = 0;
    Target_Port = 0;
  }
  else {
    inet_aton(IP, &addr.sin_addr);
    Target_Node = addr.sin_addr.s_addr;
    while ((Target_Port = rt_request_port_id(Target_Node,nam2num(sName))) <= 0
	   && Target_Port != -EINVAL);
  }

  sem = RT_typed_named_sem_init(Target_Node,Target_Port,sName, 0, CNT_SEM);

  inpDevStr[port-1].ptr1 = (void *) sem;
  inpDevStr[id].l1 = Target_Node;
  inpDevStr[id].l2 = Target_Port;
}

void inp_rtai_sem_input(int port, double * y, double t)
{
  int ret,id;

  id=port-1;
  SEM *sem = (SEM *) inpDevStr[id].ptr1;
  ret = RT_sem_wait(inpDevStr[id].l1, inpDevStr[id].l2,sem);
  y[0]=0.0;
}

void inp_rtai_sem_update(void)
{
}

void inp_rtai_sem_end(int port)
{
  int id=port-1;
  SEM *sem = (SEM *) inpDevStr[id].ptr1;
  RT_named_sem_delete(inpDevStr[id].l1, inpDevStr[id].l2,sem);
  if(inpDevStr[id].l1){
    rt_release_port(inpDevStr[id].l1, inpDevStr[id].l2);
  }
}

void out_rtai_sem_init(int port,char * sName,char * IP)
{
  long Target_Node;
  long Target_Port=0;
  SEM * sem;
  struct sockaddr_in addr;

  int id=port-1;
  strcpy(outDevStr[id].IOName,"rtai_sem out");

  if(!strcmp(IP,"0")) {
    Target_Node = 0;
    Target_Port = 0;
  }
  else {
    inet_aton(IP, &addr.sin_addr);
    Target_Node = addr.sin_addr.s_addr;
    while ((Target_Port = rt_request_port_id(Target_Node,nam2num(sName))) <= 0
	   && Target_Port != -EINVAL);
  }

  sem = RT_typed_named_sem_init(Target_Node,Target_Port,sName, 0, CNT_SEM);

  outDevStr[port-1].ptr1 = (void *) sem;
  outDevStr[id].l1 = Target_Node;
  outDevStr[id].l2 = Target_Port;
}

void out_rtai_sem_output(int port, double * u,double t)
{
  int ret; 
  int id=port-1;
  SEM *sem = (SEM *) outDevStr[id].ptr1;
  if(*u > 0.0) ret = RT_sem_signal(outDevStr[id].l1, outDevStr[id].l2,sem);
}

void out_rtai_sem_end(int port)
{
  int id=port-1;

  SEM *sem = (SEM *) outDevStr[id].ptr1;
  RT_named_sem_delete(outDevStr[id].l1, outDevStr[id].l2,sem);
  printf("%s closed\n",outDevStr[port-1].IOName);
  if(outDevStr[id].l1){
    rt_release_port(outDevStr[id].l1, outDevStr[id].l2);
  }
}




