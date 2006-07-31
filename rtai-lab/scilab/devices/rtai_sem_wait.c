#include <machine.h>
#include <scicos_block.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <rtai_netrpc.h>
#include <rtai_sem.h>

struct Semw{
  char semName[20];
  SEM * sem;
  long tNode;
  long tPort;
};

static void init(scicos_block *block)
{
  char str[20];
  struct Semw * sem = (struct Semw *) malloc(sizeof(struct Semw));

  getstr(str,block->ipar,2,block->ipar[0]);
  strcpy(sem->semName,str);
  getstr(str,block->ipar,2+block->ipar[0],block->ipar[1]);

  struct sockaddr_in addr;

  if(!strcmp(str,"0")) {
    sem->tNode = 0;
    sem->tPort = 0;
  }
  else {
    inet_aton(str, &addr.sin_addr);
    sem->tNode = addr.sin_addr.s_addr;
    while ((sem->tPort = rt_request_port(sem->tNode)) <= 0
           && sem->tPort != -EINVAL);
  }

  sem->sem = RT_typed_named_sem_init(sem->tNode,sem->tPort,sem->semName, 0, CNT_SEM);
  if(sem->sem == NULL) {
    fprintf(stderr, "Error in getting %s semaphore address\n", sem->semName);
    exit_on_error();
  }

  *block->work=(void *) sem;
}

static void inout(scicos_block *block)
{
  struct Semw * sem = (struct Semw *) (*block->work);
  int ret;
  ret = RT_sem_wait(sem->tNode, sem->tPort,sem->sem);
  block->outptr[0][0]=1.0;
}

static void end(scicos_block *block)
{
  struct Semw * sem = (struct Semw *) (*block->work);
  RT_named_sem_delete(sem->tNode, sem->tPort,sem->sem);
  if(sem->tNode){
    rt_release_port(sem->tNode, sem->tPort);
  }
  printf("SEM %s closed\n",sem->semName);
  free(sem);
}

void rtai_sem_wait(scicos_block *block,int flag)
{
  if (flag==1){          /* set output */
    inout(block);
  }
  else if (flag==5){     /* termination */ 
    end(block);
  }
  else if (flag ==4){    /* initialisation */
    init(block);
  }
}


