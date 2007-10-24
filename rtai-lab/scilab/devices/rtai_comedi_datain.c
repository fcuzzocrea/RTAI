/*
COPYRIGHT (C) 2006  Roberto Bucher (roberto.bucher@supsi.ch)

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
extern int ComediDev_AOInUse[];
extern int ComediDev_AIInUse[];

struct ADCOMDev{
  int channel;
  char devName[20];
  unsigned int range;
  unsigned int aref;
  void * dev;
  int subdev;
  double range_min;
  double range_max;
};

static void init(scicos_block *block)
{
  struct ADCOMDev * comdev = (struct ADCOMDev *) malloc(sizeof(struct ADCOMDev));
  int len;

  int n_channels;
  char board[50];
  char sName[15];
  comedi_krange krange;

  comdev->channel=block->ipar[0];
  par_getstr(sName,block->ipar,4,block->ipar[3]);
  sprintf(comdev->devName,"/dev/%s",sName);
  comdev->range=(unsigned int) block->ipar[1];
  comdev->aref=block->ipar[2];

  len=strlen(comdev->devName);
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
    if ((comdev->subdev = comedi_find_subdevice_by_type(comdev->dev, COMEDI_SUBD_AI, 0)) < 0) {
      fprintf(stderr, "Comedi find_subdevice failed (No analog input)\n");
      comedi_close(comdev->dev);
      exit_on_error();
    }
    if ((comedi_lock(comdev->dev, comdev->subdev)) < 0) {
      fprintf(stderr, "Comedi lock failed for subdevice %d\n", comdev->subdev);
      comedi_close(comdev->dev);
      exit_on_error();
    }
  } else {
    comdev->dev = ComediDev[index];
    comdev->subdev = comedi_find_subdevice_by_type(comdev->dev, COMEDI_SUBD_AI, 0);
  }
  if ((n_channels = comedi_get_n_channels(comdev->dev, comdev->subdev)) < 0) {
    fprintf(stderr, "Comedi get_n_channels failed for subdevice %d\n", comdev->subdev);
    comedi_unlock(comdev->dev, comdev->subdev);
    comedi_close(comdev->dev);
    exit_on_error();
  }
  if (comdev->channel >= n_channels) {
    fprintf(stderr, "Comedi channel not available for subdevice %d\n", comdev->subdev);
    comedi_unlock(comdev->dev, comdev->subdev);
    comedi_close(comdev->dev);
    exit_on_error();
  }
  if ((comedi_get_krange(comdev->dev, comdev->subdev, comdev->channel, comdev->range, &krange)) < 0) {
    fprintf(stderr, "Comedi get range failed for subdevice %d\n", comdev->subdev);
    comedi_unlock(comdev->dev, comdev->subdev);
    comedi_close(comdev->dev);
    exit_on_error();
  }
  ComediDev_InUse[index]++;
  ComediDev_AIInUse[index]++;
  comdev->range_min = (double)(krange.min)*1.e-6;
  comdev->range_max = (double)(krange.max)*1.e-6;
  printf("AI Channel %d - Range : %1.2f [V] - %1.2f [V]\n", comdev->channel, comdev->range_min, comdev->range_max);

  *block->work=(void *)comdev;
}

static void inout(scicos_block *block)
{
  struct ADCOMDev * comdev = (struct ADCOMDev *) (*block->work);

  lsampl_t data, maxdata = comedi_get_maxdata(comdev->dev, comdev->subdev, comdev->channel);
  double x;
  double *y = block->outptr[0];

  comedi_data_read(comdev->dev, comdev->subdev, comdev->channel, comdev->range, comdev->aref, &data);
  x = data;
  x /= maxdata;
  x *= (comdev->range_max - comdev->range_min);
  x += comdev->range_min;
  y[0] = x;
}

static void end(scicos_block *block)
{
  struct ADCOMDev * comdev = (struct ADCOMDev *) (*block->work);

  int len;

  len=strlen(comdev->devName);
  int index = comdev->devName[len-1]-'0';

  ComediDev_InUse[index]--;
  ComediDev_AIInUse[index]--;
  if (!ComediDev_AIInUse[index]) {
    comedi_unlock(comdev->dev, comdev->subdev);
  }
  if (!ComediDev_InUse[index]) {
    comedi_close(comdev->dev);
    printf("\nCOMEDI AD %s closed.\n\n", comdev->devName);
    ComediDev[index] = NULL;
  }
  free(comdev);
}

void rt_comedi_datain(scicos_block *block,int flag)
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


