#include <machine.h>
#include <scicos_block.h>
#include <stdio.h>
#include <stdlib.h>
#include <rtai_netrpc.h>
#include <rtai_msg.h>
#include <rtai_mbx.h>
#include <string.h>
#include "rtmain.h"

#define MAX_RTAI_METERS               1000
#define MBX_RTAI_METER_SIZE           5000

extern char *TargetMeterMbxID;

void par_getstr(char * str, int par[], int init, int len);

static void init(scicos_block *block)
{
  char meterName[10];
  char name[7];
  int nch = block->nin;
  MBX *mbx;

  par_getstr(meterName,block->ipar,1,block->ipar[0]);
  rtRegisterMeter(meterName,nch);
  get_a_name(TargetMeterMbxID,name);

  mbx = (MBX *) RT_typed_named_mbx_init(0,0,name,MBX_RTAI_METER_SIZE/sizeof(float)*sizeof(float),FIFO_Q);
  if(mbx == NULL) {
    fprintf(stderr, "Cannot init mailbox\n");
    exit_on_error();
  }

  *block->work=(void *) mbx;
}

static void inout(scicos_block *block)
{
  MBX * mbx = (MBX *) (*block->work);
  float data;
  data = (float) block->inptr[0][0];
  RT_mbx_send_if(0, 0, mbx, &data, sizeof(data));
}

static void end(scicos_block *block)
{
  char meterName[10];
  MBX * mbx = (MBX *) (*block->work);
  RT_named_mbx_delete(0, 0, mbx);
  par_getstr(meterName,block->ipar,1,block->ipar[0]);
  printf("Meter %s closed\n",meterName);
}

void rtmeter(scicos_block *block,int flag)
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


