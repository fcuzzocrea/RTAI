#include <machine.h>
#include <scicos_block.h>
#include <stdio.h>
#include <rtai_fifos.h>

static void init(scicos_block *block)
{
  rtf_create(block->ipar[0],block->ipar[1]);
  rtf_reset(block->ipar[0]);
}

static void inout(scicos_block *block)
{
  int ntraces=block->nin;
  struct {
    float t;
    float u[ntraces];
  } data;
  int i;

  data.t=(float) get_scicos_time();
  for (i = 0; i < ntraces; i++) {
    data.u[i] = (float) block->inptr[i][0];
  }
  rtf_put(block->ipar[0],&data, sizeof(data));
}

static void end(scicos_block *block)
{
  rtf_destroy(block->ipar[0]);
  printf("FIFO %d closed\n",block->ipar[0]);
}



void rt_fifoout(scicos_block *block,int flag)
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


