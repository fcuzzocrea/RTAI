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
extern int pinp_cnt;
extern int pout_cnt;

int inp_rtai_sem_init(char * sName,char * IP)
{
  long Target_Node;
  long Target_Port=0;
  SEM * sem;
  struct sockaddr_in addr;

  int port=pinp_cnt++;
  strcpy(inpDevStr[port].IOName,"rtai_sem inp");

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

  inpDevStr[port].ptr1 = (void *) sem;
  inpDevStr[port].l1 = Target_Node;
  inpDevStr[port].l2 = Target_Port;

  return(port);
}

void inp_rtai_sem_input(int port, double * y, double t)
{
  int ret;

  SEM *sem = (SEM *) inpDevStr[port].ptr1;
  ret = RT_sem_wait(inpDevStr[port].l1, inpDevStr[port].l2,sem);
  y[0]=0.0;
}

void inp_rtai_sem_update(void)
{
}

void inp_rtai_sem_end(int port)
{
  SEM *sem = (SEM *) inpDevStr[port].ptr1;
  RT_named_sem_delete(inpDevStr[port].l1, inpDevStr[port].l2,sem);
  if(inpDevStr[port].l1){
    rt_release_port(inpDevStr[port].l1, inpDevStr[port].l2);
  }
}

int out_rtai_sem_init(char * sName,char * IP)
{
  long Target_Node;
  long Target_Port=0;
  SEM * sem;
  struct sockaddr_in addr;

  int port=pout_cnt++;
  strcpy(outDevStr[port].IOName,"rtai_sem out");

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

  outDevStr[port].ptr1 = (void *) sem;
  outDevStr[port].l1 = Target_Node;
  outDevStr[port].l2 = Target_Port;

  return(port);
}

void out_rtai_sem_output(int port, double * u,double t)
{
  int ret; 
  SEM *sem = (SEM *) outDevStr[port].ptr1;
  if(*u > 0.0) ret = RT_sem_signal(outDevStr[port].l1, outDevStr[port].l2,sem);
}

void out_rtai_sem_end(int port)
{
  SEM *sem = (SEM *) outDevStr[port].ptr1;
  RT_named_sem_delete(outDevStr[port].l1, outDevStr[port].l2,sem);
  printf("%s closed\n",outDevStr[port].IOName);
  if(outDevStr[port].l1){
    rt_release_port(outDevStr[port].l1, outDevStr[port].l2);
  }
}




