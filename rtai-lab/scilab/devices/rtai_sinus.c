#include <machine.h>
#include <scicos_block.h>

static void init(scicos_block *block)
{
   block->outptr[0][0]=0.0;
}

static void inout(scicos_block *block)
{
  double w,pi=3.1415927;
  double t = get_scicos_time();

   if (t<block->rpar[4]) block->outptr[0][0]=0.0;
   else {
     w=2*pi*block->rpar[1]*(t-block->rpar[4])-block->rpar[2];
     block->outptr[0][0]=block->rpar[0]*sin(w)+block->rpar[3];
   }
}

static void end(scicos_block *block)
{
   block->outptr[0][0]=0.0;
}



void rtsinus(scicos_block *block,int flag)
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


