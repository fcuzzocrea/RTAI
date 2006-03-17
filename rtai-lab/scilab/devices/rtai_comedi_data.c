/*
  COPYRIGHT (C) 2003  Roberto Bucher (roberto.bucher@die.supsi.ch)

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

#include "rtmain.h"

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

struct ACOMDev{
  int channel;
  char devName[20];
  unsigned int range;
  unsigned int aref;
  void * dev;
  int subdev;
  double range_min;
  double range_max;
};

void * inp_rtai_comedi_data_init(int nch,char * sName,int Range, int aRef)
{
  struct ACOMDev * comdev = (struct ACOMDev *) malloc(sizeof(struct ACOMDev));
  int len;

  int n_channels;
  char board[50];
  comedi_krange krange;

  comdev->channel=nch;
  sprintf(comdev->devName,"/dev/%s",sName);
  comdev->range=(unsigned int) Range;
  comdev->aref=aRef;


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

  return((void *)comdev);
}

void * out_rtai_comedi_data_init(int nch,char * sName,int Range, int aRef)

{
  struct ACOMDev * comdev = (struct ACOMDev *) malloc(sizeof(struct ACOMDev));
  int len;

  int n_channels;
  char board[50];
  lsampl_t data, maxdata;
  comedi_krange krange;
  double s,u;

  comdev->channel=nch;
  sprintf(comdev->devName,"/dev/%s",sName);
  comdev->range=(unsigned int) Range;
  comdev->aref=aRef;

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
    if ((comdev->subdev = comedi_find_subdevice_by_type(comdev->dev, COMEDI_SUBD_AO, 0)) < 0) {
      fprintf(stderr, "Comedi find_subdevice failed (No analog output)\n");
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
    comdev->subdev = comedi_find_subdevice_by_type(comdev->dev, COMEDI_SUBD_AO, 0);
  }
  if ((n_channels = comedi_get_n_channels(comdev->dev, comdev->subdev)) < 0) {
    printf("Comedi get_n_channels failed for subdevice %d\n", comdev->subdev);
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
  maxdata = comedi_get_maxdata(comdev->dev, comdev->subdev, comdev->channel);
  if ((comedi_get_krange(comdev->dev, comdev->subdev, comdev->channel, comdev->range, &krange)) < 0) {
    fprintf(stderr, "Comedi get range failed for subdevice %d\n", comdev->subdev);
    comedi_unlock(comdev->dev, comdev->subdev);
    comedi_close(comdev->dev);
    exit_on_error();
  }
  ComediDev_InUse[index]++;
  ComediDev_AOInUse[index]++;
  comdev->range_min = (double)(krange.min)*1.e-6;
  comdev->range_max = (double)(krange.max)*1.e-6;
  printf("AO Channel %d - Range : %1.2f [V] - %1.2f [V]\n", comdev->channel, comdev->range_min, comdev->range_max);
  u = 0.;
  s = (u - comdev->range_min)/(comdev->range_max - comdev->range_min)*maxdata;
  data = (lsampl_t)(floor(s+0.5));
  comedi_data_write(comdev->dev, comdev->subdev, comdev->channel, comdev->range, comdev->aref, data);

  return((void *)comdev);
}

void out_rtai_comedi_data_output(void * ptr, double * u,double t)
{ 
  struct ACOMDev * comdev = (struct ACOMDev *) ptr;

  lsampl_t data, maxdata = comedi_get_maxdata(comdev->dev, comdev->subdev, comdev->channel);
  double s;

  s = (*u - comdev->range_min)/(comdev->range_max - comdev->range_min)*maxdata;
  if (s < 0) {
    data = 0;
  } else if (s > maxdata) {
    data = maxdata;
  } else {
    data = (lsampl_t)(floor(s+0.5));
  }
  comedi_data_write(comdev->dev, comdev->subdev, comdev->channel, comdev->range, comdev->aref, data);
}

void inp_rtai_comedi_data_input(void * ptr, double * y, double t)
{
  struct ACOMDev * comdev = (struct ACOMDev *) ptr;

  lsampl_t data, maxdata = comedi_get_maxdata(comdev->dev, comdev->subdev, comdev->channel);
  double x;

  comedi_data_read(comdev->dev, comdev->subdev, comdev->channel, comdev->range, comdev->aref, &data);
  x = data;
  x /= maxdata;
  x *= (comdev->range_max - comdev->range_min);
  x += comdev->range_min;
  *y = x;
}

void inp_rtai_comedi_data_update(void)
{
}

void out_rtai_comedi_data_end(void * ptr)
{
  struct ACOMDev * comdev = (struct ACOMDev *) ptr;

  lsampl_t data, maxdata = comedi_get_maxdata(comdev->dev, comdev->subdev, comdev->channel);
  double s;
  int len;

  len=strlen(comdev->devName);
  int index = comdev->devName[len-1]-'0';

  s = (0.0 - comdev->range_min)/(comdev->range_max - comdev->range_min)*maxdata;
  if (s < 0) {
    data = 0;
  } else if (s > maxdata) {
    data = maxdata;
  } else {
    data = (lsampl_t)(floor(s+0.5));
  }
  comedi_data_write(comdev->dev, comdev->subdev, comdev->channel, comdev->range, comdev->aref, data);

  ComediDev_InUse[index]--;
  ComediDev_AOInUse[index]--;
  if (!ComediDev_AOInUse[index]) {
    comedi_unlock(comdev->dev, comdev->subdev);
  }
  if (!ComediDev_InUse[index]) {
    comedi_close(comdev->dev);
    printf("\nCOMEDI %s closed.\n\n", comdev->devName);
    ComediDev[index] = NULL;
  }
  free(comdev);
}


void inp_rtai_comedi_data_end(void * ptr)
{
  struct ACOMDev * comdev = (struct ACOMDev *) ptr;

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
    printf("\nCOMEDI %s closed.\n\n", comdev->devName);
    ComediDev[index] = NULL;
  }
  free(comdev);
}




