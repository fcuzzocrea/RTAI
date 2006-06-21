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

#include "rtmain.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <rtai_netrpc.h>
#include <rtai_sem.h>

struct Sem{
  char semName[20];
  SEM * sem;
  long tNode;
  long tPort;
};

void * inp_rtai_sem_init(char * sName,char * IP)
{
  struct Sem * sem = (struct Sem *) malloc(sizeof(struct Sem));
  strcpy(sem->semName,sName);

  struct sockaddr_in addr;

  if(!strcmp(IP,"0")) {
    sem->tNode = 0;
    sem->tPort = 0;
  }
  else {
    inet_aton(IP, &addr.sin_addr);
    sem->tNode = addr.sin_addr.s_addr;
    while ((sem->tPort = rt_request_port(sem->tNode)) <= 0
	   && sem->tPort != -EINVAL);
  }

  sem->sem = RT_typed_named_sem_init(sem->tNode,sem->tPort,sem->semName, 0, CNT_SEM);
  if(sem->sem == NULL) {
    fprintf(stderr, "Error in getting %s semaphore address\n", sem->semName);
    exit_on_error();
  }

  return((void *) sem);
}

void inp_rtai_sem_input(void * ptr, double * y, double t)
{
  struct Sem * sem = (struct Sem *) ptr;
  int ret;

  ret = RT_sem_wait(sem->tNode, sem->tPort,sem->sem);
  y[0]=1.0;
}

void inp_rtai_sem_update(void)
{
}

void inp_rtai_sem_end(void * ptr)
{
  struct Sem * sem = (struct Sem *) ptr;
  RT_named_sem_delete(sem->tNode, sem->tPort,sem->sem);
  if(sem->tNode){
    rt_release_port(sem->tNode, sem->tPort);
  }
  printf("SEM %s closed\n",sem->semName);
  free(sem);
}

void * out_rtai_sem_init(char * sName,char * IP)
{
  struct Sem * sem = (struct Sem *) malloc(sizeof(struct Sem));
  strcpy(sem->semName,sName);
  struct sockaddr_in addr;

  if(!strcmp(IP,"0")) {
    sem->tNode = 0;
    sem->tPort = 0;
  }
  else {
    inet_aton(IP, &addr.sin_addr);
    sem->tNode = addr.sin_addr.s_addr;
    while ((sem->tPort = rt_request_port(sem->tNode)) <= 0
	   && sem->tPort != -EINVAL);
  }

  sem->sem = RT_typed_named_sem_init(sem->tNode,sem->tPort,sem->semName, 0, CNT_SEM);
  if(sem->sem == NULL) {
    fprintf(stderr, "Error in getting %s semaphore address\n", sem->semName);
    exit_on_error();
  }

  return((void *) sem);
}

void out_rtai_sem_output(void * ptr, double * u,double t)
{
  struct Sem * sem = (struct Sem *) ptr;
  int ret; 
  if(*u > 0.0) ret = RT_sem_signal(sem->tNode, sem->tPort,sem->sem);
}

void out_rtai_sem_end(void * ptr)
{
  struct Sem * sem = (struct Sem *) ptr;
  RT_named_sem_delete(sem->tNode, sem->tPort,sem->sem);
  if(sem->tNode){
    rt_release_port(sem->tNode, sem->tPort);
  }
  printf("SEM %s closed\n",sem->semName);
  free(sem);
}




