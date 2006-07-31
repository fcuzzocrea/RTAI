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

struct MbxR{
  char mbxName[10];
  MBX * mbx;
  long tNode;
  long tPort;
  double * oldVal;
};

static void init(scicos_block *block)
{
  char str[20];
  struct MbxR * mbx = (struct MbxR *) malloc(sizeof(struct MbxR));
  int nch=block->nout;
  getstr(str,block->ipar,2,block->ipar[0]);
  strcpy(mbx->mbxName,str);
  getstr(str,block->ipar,2+block->ipar[0],block->ipar[1]);

  struct sockaddr_in addr;

  if(!strcmp(str,"0")) {
    mbx->tNode = 0;
    mbx->tPort = 0;
  }
  else {
    inet_aton(str, &addr.sin_addr);
    mbx->tNode = addr.sin_addr.s_addr;
    while ((mbx->tPort = rt_request_port(mbx->tNode)) <= 0
           && mbx->tPort != -EINVAL);
  }

  mbx->mbx = (MBX *) RT_typed_named_mbx_init(mbx->tNode,mbx->tPort,mbx->mbxName,nch*sizeof(double),FIFO_Q);

  if(mbx->mbx == NULL) {
    fprintf(stderr, "Error in getting %s mailbox address\n", mbx->mbxName);
    exit_on_error();
  }
  mbx->oldVal = calloc(nch,sizeof(double));

  *block->work=(void *) mbx;
}

static void inout(scicos_block *block)
{
  struct MbxR * mbx = (struct MbxR *) (*block->work);
  int ntraces = block->nout;
  struct{
    double u[ntraces];
  } data;
  int i;

  if(!RT_mbx_receive(mbx->tNode, mbx->tPort, mbx->mbx, &data, sizeof(data))) {
    for(i=0;i<ntraces;i++){
      mbx->oldVal[i] = data.u[i];
    }
  }
  for(i=0;i<ntraces;i++) block->outptr[i][0] = mbx->oldVal[i];
}

static void end(scicos_block *block)
{
  struct MbxR * mbx = (struct MbxR *) (*block->work);

  RT_named_mbx_delete(mbx->tNode, mbx->tPort,mbx->mbx);
  printf("OVRWR MBX %s closed\n",mbx->mbxName);
  if(mbx->tNode){
    rt_release_port(mbx->tNode,mbx->tPort);
  }
  free(mbx->oldVal);
  free(mbx);
}

void rtai_mbx_rcv(scicos_block *block,int flag)
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


