#include <machine.h>
#include <scicos_block.h>

static void init(scicos_block *block)
{
  block->outptr[0][0]=0.0;
}

static void inout(scicos_block *block)
{
  double t=get_scicos_time();
  if (t<block->rpar[1]) block->outptr[0][0]=0.0;
  else                  block->outptr[0][0]=block->rpar[0];
}

static void end(scicos_block *block)
{
  block->outptr[0][0]=0.0;
}



void rt_step(scicos_block *block,int flag)
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


