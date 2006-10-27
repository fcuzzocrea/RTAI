#include <machine.h>
#include <scicos_block.h>
#include <stdio.h>
#include <stdlib.h>
#include <rtai_netrpc.h>
#include <rtai_msg.h>
#include <rtai_mbx.h>
#include <string.h>
#include "rtmain.h"

#define MAX_RTAI_SCOPES               1000
#define MBX_RTAI_SCOPE_SIZE           5000

extern char *TargetMbxID;

void par_getstr(char * str, int par[], int init, int len);

static void init(scicos_block *block)
{
  char scopeName[10];
  char name[7];
  int nch = block->nin;
  int nt=nch + 1;
  MBX *mbx;

  par_getstr(scopeName,block->ipar,1,block->ipar[0]);
  rtRegisterScope(scopeName,nch);
  get_a_name(TargetMbxID,name);

  mbx = (MBX *) RT_typed_named_mbx_init(0,0,name,(MBX_RTAI_SCOPE_SIZE/(nt*sizeof(float)))*(nt*sizeof(float)),FIFO_Q);
  if(mbx == NULL) {
    fprintf(stderr, "Cannot init mailbox\n");
    exit_on_error();
  }

  *block->work=(void *) mbx;
}

static void inout(scicos_block *block)
{
  MBX * mbx = (MBX *) (*block->work);
  int ntraces=block->nin;
  struct {
    float t;
    float u[ntraces];
  } data;
  int i;

  double t=get_scicos_time();
  data.t=(float) t;
  for (i = 0; i < ntraces; i++) {
    data.u[i] = (float) block->inptr[i][0];
  }
  RT_mbx_send_if(0, 0, mbx, &data, sizeof(data));
}

static void end(scicos_block *block)
{
  char scopeName[10];
  MBX * mbx = (MBX *) (*block->work);
  RT_named_mbx_delete(0, 0, mbx);
  par_getstr(scopeName,block->ipar,1,block->ipar[0]);
  printf("Scope %s closed\n",scopeName);
}

void rtscope(scicos_block *block,int flag)
{
  if (flag==2){          
    inout(block);
  }
  else if (flag==5){     /* termination */ 
    end(block);
  }
  else if (flag ==4){    /* initialisation */
    init(block);
  }
}


