/*
COPYRIGHT (C) 2008 Guillaume MILLET (millet@isir.fr)
                   Julien VITARD    (vitard@isir.fr)

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

#include <machine.h>
#include <scicos_block4.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include <rtai_lxrt.h>
#include <rtai_comedi.h>

extern void *ComediDev[];
extern int ComediDev_InUse[];
extern int ComediDev_CounterInUse[][0];

typedef enum {UP_DOWN, X1, X2, X4=4} Counter_mode;

struct CounterCOMDev
{
  int number;
  char devName[20];
  void * dev;
  unsigned int subdev;
  int a,b,z;
  unsigned int initval;
  unsigned int index;
  Counter_mode cmode;
};

static void init(scicos_block *block)
{
  struct CounterCOMDev * comdev = (struct CounterCOMDev *) malloc(sizeof(struct CounterCOMDev));

  char board[50];
  char sName[15];
  lsampl_t maxdata;
  int i;

  comdev->number=block->ipar[0];
  par_getstr(sName,block->ipar,8,block->ipar[7]);
  sprintf(comdev->devName,"/dev/%s",sName);
  comdev->a=block->ipar[1];
  comdev->b=block->ipar[2];
  comdev->z=block->ipar[3];
  comdev->initval=(unsigned int) block->ipar[4];
  comdev->index=(unsigned int) block->ipar[5];
  comdev->cmode=(unsigned int) block->ipar[6];

  int len = strlen(comdev->devName);
  int index = comdev->devName[len-1]-'0';

  if (!ComediDev[index]) {
    comdev->dev = comedi_open(comdev->devName);
    if (!(comdev->dev)) {
      fprintf(stderr, "Comedi open failed\n");
      exit_on_error();
    }
    rt_comedi_get_board_name(comdev->dev, board);
    printf("COMEDI %s (%s) opened.\n\n", comdev->devName, board);
    ComediDev[index] = comdev->dev;
    if ((comdev->subdev = comedi_find_subdevice_by_type(comdev->dev, COMEDI_SUBD_COUNTER, 0)) < 0) {
      fprintf(stderr, "Comedi find_subdevice failed (No Counter)\n");
      comedi_close(comdev->dev);
      exit_on_error();
    }
    if (comdev->number > 0) {
      unsigned int n_subdevices;
      if ((n_subdevices = comedi_get_n_subdevices(comdev->dev)) < 0) {
      fprintf(stderr, "Comedi get_n_subdevices failed for COMEDI %s\n", comdev->devName);
      comedi_close(comdev->dev);
      exit_on_error();
      }
      unsigned int subd;
      unsigned int subd_type;
      i=0;
      for (subd=comdev->subdev+1;subd<n_subdevices;subd++) {
        if ((subd_type = comedi_get_subdevice_type(comdev->dev,subd)) < 0) {
          fprintf(stderr, "Comedi get_subdevice_type failed for subdevice %d\n", subd);
          comedi_close(comdev->dev);
          exit_on_error();
        }
        if (subd_type == COMEDI_SUBD_COUNTER) {
          i++;
          if (i == comdev->number) {
            comdev->subdev = subd;
            break;
          }
        }
      }
      if (subd == n_subdevices) {
        fprintf(stderr, "Find subdevice failed (No Counter %d)\n",comdev->number);
        comedi_close(comdev->dev);
        exit_on_error();
      }
    }
    if ((comedi_lock(comdev->dev, comdev->subdev)) < 0) {
      fprintf(stderr, "Comedi lock failed for subdevice %d\n", comdev->subdev);
      comedi_close(comdev->dev);
      exit_on_error();
    }
  } else {
    comdev->dev = ComediDev[index];
    comdev->subdev = comedi_find_subdevice_by_type(comdev->dev, COMEDI_SUBD_COUNTER, 0);
    if (comdev->number > 0) {
      unsigned int n_subdevices = comedi_get_n_subdevices(comdev->dev);
      unsigned int subd;
      i=0;
      for (subd=comdev->subdev+1;subd<n_subdevices;subd++) {
        if (comedi_get_subdevice_type(comdev->dev,subd) == COMEDI_SUBD_COUNTER) {
          i++;
          if (i == comdev->number) {
            comdev->subdev = subd;
            break;
          }
        }
      }
      if (subd == n_subdevices) {
        fprintf(stderr, "Find subdevice failed (No Counter %d)\n",comdev->number);
        comedi_close(comdev->dev);
        exit_on_error();
      }
    }
    if ((comedi_lock(comdev->dev, comdev->subdev)) < 0) {
      fprintf(stderr, "Comedi lock failed for subdevice %d\n", comdev->subdev);
      comedi_close(comdev->dev);
      exit_on_error();
    }
  }

  maxdata = comedi_get_maxdata(comdev->dev, comdev->subdev, 0);
  if (comdev->initval > maxdata) {
    fprintf(stderr, "Initial value (%lu) must be < to %lu\n", comdev->initval, maxdata);
    comedi_unlock(comdev->dev, comdev->subdev);
    comedi_close(comdev->dev);
    exit_on_error();
  }

  ComediDev_InUse[index]++;
  ComediDev_CounterInUse[index][comdev->number]++;
  printf("Counter %d - MaxData : %lu - Initial value : %lu - Index enable : %d\nMode ",
          comdev->number, maxdata, comdev->initval,comdev->index);
  if (comdev->cmode==UP_DOWN) {
    printf("UP/DOWN - Channel A on PFI%d - Channel B on ",comdev->a);
    (block->ipar[2]==-1)?printf("P0.%d (GP_UP_DOWN)",6+comdev->number):printf("PFI%d",comdev->b);
  }
  else
    printf("X%d - Channel A on PFI%d - Channel B on PFI%d - Channel Z on PFI%d",
           comdev->cmode,comdev->a,comdev->b,comdev->z);
  printf("\n\n");

  comedi_insn insn;
  lsampl_t data[3];
  memset(&insn, 0, sizeof(comedi_insn));
  insn.insn = INSN_CONFIG;
  insn.subdev = comdev->subdev;
  insn.chanspec = 0;
  insn.data = data;

  insn.n = 1;
  data[0] = INSN_CONFIG_RESET;
  if (comedi_do_insn(comdev->dev, &insn) < 0) {
    fprintf(stderr, "Comedi do_insn failed on instruction %d\n", data[0]);
    comedi_unlock(comdev->dev, comdev->subdev);
    comedi_close(comdev->dev);
    exit_on_error();
  }

  comedi_data_write(comdev->dev, comdev->subdev, 0, 0, 0, comdev->initval);

  unsigned int counter_mode = NI_GPCT_COUNTING_DIRECTION_HW_UP_DOWN_BITS;
  switch(comdev->cmode) {
    case UP_DOWN:
      counter_mode |= NI_GPCT_COUNTING_MODE_NORMAL_BITS;
      break;
    case X1:
      counter_mode |= NI_GPCT_COUNTING_MODE_QUADRATURE_X1_BITS;
      break;
    case X2:
      counter_mode |= NI_GPCT_COUNTING_MODE_QUADRATURE_X2_BITS;
      break;
    case X4:
      counter_mode |= NI_GPCT_COUNTING_MODE_QUADRATURE_X4_BITS;
      break;
    default:
      fprintf(stderr, "Unknown counter mode %d\n", comdev->cmode);
      comedi_unlock(comdev->dev, comdev->subdev);
      comedi_close(comdev->dev);
      exit_on_error();
  }
  if (comdev->index)
    counter_mode |= (NI_GPCT_INDEX_ENABLE_BIT | NI_GPCT_INDEX_PHASE_HIGH_A_HIGH_B_BITS);
  unsigned int config[][4] = {3, INSN_CONFIG_SET_GATE_SRC, 0, NI_GPCT_DISABLED_GATE_SELECT, \
                              3, INSN_CONFIG_SET_GATE_SRC, 1, NI_GPCT_DISABLED_GATE_SELECT, \
                              3, INSN_CONFIG_SET_OTHER_SRC, NI_GPCT_SOURCE_ENCODER_A, NI_GPCT_PFI_OTHER_SELECT(comdev->a), \
                              3, INSN_CONFIG_SET_OTHER_SRC, NI_GPCT_SOURCE_ENCODER_B, NI_GPCT_PFI_OTHER_SELECT(comdev->b), \
                              3, INSN_CONFIG_SET_OTHER_SRC, NI_GPCT_SOURCE_ENCODER_Z, NI_GPCT_PFI_OTHER_SELECT(comdev->z), \
                              2, INSN_CONFIG_SET_COUNTER_MODE, counter_mode, 0, \
                              2, INSN_CONFIG_ARM, NI_GPCT_ARM_IMMEDIATE, 0};
  if (comdev->cmode==UP_DOWN) {
    unsigned int conf_src[] =  {3, INSN_CONFIG_SET_CLOCK_SRC, NI_GPCT_PFI_CLOCK_SRC_BITS(comdev->a),0, \
                                0, 0, 0, 0, 0, 0, 0, 0};
    unsigned int conf_srcB[] = {3, INSN_CONFIG_SET_CLOCK_SRC, NI_GPCT_PFI_CLOCK_SRC_BITS(comdev->a),0, \
                                3, INSN_CONFIG_SET_OTHER_SRC, NI_GPCT_SOURCE_ENCODER_B, NI_GPCT_PFI_OTHER_SELECT(comdev->b), \
                                0, 0, 0, 0};
    for (i=0;i<12;i++)
      config[2+i/4][i%4] = (comdev->b==-1)?conf_src[i]:conf_srcB[i];
  }

  for (i=0;i<sizeof(config)/sizeof(config[0]);i++) {
    if (config[i][0]==0)
      continue;
    insn.n = config[i][0];
    data[0] = config[i][1];
    data[1] = config[i][2];
    data[2] = config[i][3];
    if (comedi_do_insn(comdev->dev, &insn) < 0) {
      fprintf(stderr, "Comedi do_insn failed on instruction %d\n", data[0]);
      comedi_unlock(comdev->dev, comdev->subdev);
      comedi_close(comdev->dev);
      exit_on_error();
    }
  }

  *block->work=(void *)comdev;
}

static void inout(scicos_block *block)
{
  struct CounterCOMDev * comdev = (struct CounterCOMDev *) (*block->work);
  lsampl_t data;
  double *y = block->outptr[0];

  comedi_data_read(comdev->dev, comdev->subdev, 0, 0, 0, &data);

  y[0] = data;
}

static void end(scicos_block *block)
{
  struct CounterCOMDev * comdev = (struct CounterCOMDev *) (*block->work);

  int len = strlen(comdev->devName);
  int index = comdev->devName[len-1]-'0';

  ComediDev_InUse[index]--;
  ComediDev_CounterInUse[index][comdev->number]--;
  if (!ComediDev_CounterInUse[index][comdev->number]) {
    comedi_unlock(comdev->dev, comdev->subdev);
  }
  if (!ComediDev_InUse[index]) {
    comedi_close(comdev->dev);
    printf("\nCOMEDI Counter %s closed.\n\n", comdev->devName);
    ComediDev[index] = NULL;
  }
  free(comdev);
}

void rt_comedi_encoder(scicos_block *block,int flag)
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
