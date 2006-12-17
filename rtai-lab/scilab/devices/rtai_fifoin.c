#include <machine.h>
#include <scicos_block.h>
#include <rtai_fifos.h>
#include <stdio.h>

static void init(scicos_block *block)
{
  int i;
  int ntraces=block->nout;
  rtf_create(block->ipar[0],block->ipar[1]);
  rtf_reset(block->ipar[0]);
  for(i=0;i<ntraces;i++)
    block->outptr[i][0]=0.0;

}

static void inout(scicos_block *block)
{
  int ntraces=block->nout;
  int count;
  struct {
    double u[ntraces]; 
  } data;
  int i;

  count=rtf_get(block->ipar[0],&data,sizeof(data));
  if(count!=0) {
    for(i=0;i<ntraces;i++)
      block->outptr[i][0]=data.u[i];
  }
}

static void end(scicos_block *block)
{
  rtf_destroy(block->ipar[0]);
  printf("FIFO %d closed\n",block->ipar[0]);
}



void rt_fifoin(scicos_block *block,int flag)
{
  if (flag==1){          /* set output */
    inout(block);
  }
  if (flag==2){          /* get input */
    inout(block);
  }
  else if (flag==5){     /* termination */ 
    end(block);
  }
  else if (flag ==4){    /* initialisation */
    init(block);
  }
}


