#include <machine.h>
#include <scicos_block.h>
#include <stdio.h>
#include <stdlib.h>

static void init(scicos_block *block)
{
  FILE * fp;
  double * pData;
  char filename[30];
  int i;
  int npts=block->ipar[0];

  if(npts==0) {
    fprintf(stderr, "Error - Data length is 0!\n");
    exit_on_error();
  }

  pData=(double *) calloc(npts,sizeof(double));

  par_getstr(filename,block->ipar,3,block->ipar[2]);
  fp=fopen(filename,"r");
  if(fp!=NULL){
    block->ipar[1]=0;
    for(i=0;i<npts;i++) {
      if(feof(fp)) break;
      fscanf(fp,"%lf",&(pData[i]));
    }
    fclose(fp);
  }
  else{
    fprintf(stderr, "File %s not found!\n",filename);
    exit_on_error();
  }
  *block->work=(void *) pData;
}

static void inout(scicos_block *block)
{
  double * pData=(double *) *block->work;
  block->outptr[0][0]=pData[block->ipar[1]];
  block->ipar[1] = ++(block->ipar[1]) % block->ipar[0];
}

static void end(scicos_block *block)
{
  double * pData=(double *) *block->work;
  block->outptr[0][0]=0.0;
  free(pData);
  printf("EXTDATA closed\n");
}



void rtextdata(scicos_block *block,int flag)
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


