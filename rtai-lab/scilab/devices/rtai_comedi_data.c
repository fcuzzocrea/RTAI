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

#include "devstruct.h"
#include "rtmain.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>

#include <rtai_lxrt.h>
#include <rtai_comedi.h>

extern void *ComediDev[];
extern int ComediDev_InUse[];
extern int ComediDev_AOInUse[];
extern int ComediDev_AIInUse[];

extern devStr inpDevStr[];
extern devStr outDevStr[];
extern int pinp_cnt;
extern int pout_cnt;
 
int inp_rtai_comedi_data_init(int nch,char * sName,int Range, int aRef)
{
  int id;
  void *dev;
  int subdev;
  int len;

  unsigned int channel;
  unsigned int range;
  int n_channels;
  char board[50];
  comedi_krange krange;
  double range_min, range_max;

  int port=pinp_cnt++;

  inpDevStr[port].nch=nch;
  strcpy(inpDevStr[port].IOName,"Comedi data input");
  sprintf(inpDevStr[port].sName,"/dev/%s",sName);
  inpDevStr[port].dParam[0]=(double) Range;
  inpDevStr[port].dParam[1]=(double) aRef;

  channel = nch;
  range = (unsigned int) Range;
  len=strlen(inpDevStr[port].sName);
  int index = inpDevStr[port].sName[len-1]-'0';

  if (!ComediDev[index]) {
    dev = comedi_open(inpDevStr[port].sName);
    if (!dev) {
      fprintf(stderr, "Comedi open failed\n");
      exit_on_error();
    }
    rt_comedi_get_board_name(dev, board);
    printf("COMEDI %s (%s) opened.\n\n", inpDevStr[port].sName, board);
    ComediDev[index] = dev;
    if ((subdev = comedi_find_subdevice_by_type(dev, COMEDI_SUBD_AI, 0)) < 0) {
      fprintf(stderr, "Comedi find_subdevice failed (No analog input)\n");
      comedi_close(dev);
      exit_on_error();
    }
    if ((comedi_lock(dev, subdev)) < 0) {
      fprintf(stderr, "Comedi lock failed for subdevice %d\n", subdev);
      comedi_close(dev);
      exit_on_error();
    }
  } else {
    dev = ComediDev[index];
    subdev = comedi_find_subdevice_by_type(dev, COMEDI_SUBD_AI, 0);
  }
  if ((n_channels = comedi_get_n_channels(dev, subdev)) < 0) {
    fprintf(stderr, "Comedi get_n_channels failed for subdevice %d\n", subdev);
    comedi_unlock(dev, subdev);
    comedi_close(dev);
    exit_on_error();
  }
  if (channel >= n_channels) {
    fprintf(stderr, "Comedi channel not available for subdevice %d\n", subdev);
    comedi_unlock(dev, subdev);
    comedi_close(dev);
    exit_on_error();
  }
  if ((comedi_get_krange(dev, subdev, channel, range, &krange)) < 0) {
    fprintf(stderr, "Comedi get range failed for subdevice %d\n", subdev);
    comedi_unlock(dev, subdev);
    comedi_close(dev);
    exit_on_error();
  }
  ComediDev_InUse[index]++;
  ComediDev_AIInUse[index]++;
  range_min = (double)(krange.min)*1.e-6;
  range_max = (double)(krange.max)*1.e-6;
  printf("AI Channel %d - Range : %1.2f [V] - %1.2f [V]\n", channel, range_min, range_max);

  inpDevStr[port].ptr1 = (void *)dev;
  inpDevStr[port].dParam[2]  = (double) subdev;
  inpDevStr[port].dParam[3]  = range_min;
  inpDevStr[port].dParam[4]  = range_max;

  return(port);
}

int out_rtai_comedi_data_init(int nch,char * sName,int Range, int aRef)

{
  int id;
  void *dev;
  int subdev;
  int len;

  unsigned int channel;
  unsigned int range;
  unsigned int aref;
  int n_channels;
  char board[50];
  lsampl_t data, maxdata;
  comedi_krange krange;
  double range_min, range_max;
  double s,u;
  int port=pout_cnt++;

  outDevStr[port].nch=nch;
  strcpy(outDevStr[port].IOName,"Comedi data output");
  sprintf(outDevStr[port].sName,"/dev/%s",sName);

  outDevStr[port].dParam[0]=(double) Range;
  outDevStr[port].dParam[1]=(double) aRef;

  channel = nch;
  range   = (unsigned int) Range;
  aref    = (unsigned int) aRef-1;

  len=strlen(outDevStr[port].sName);
  int index = outDevStr[port].sName[len-1]-'0';

  if (!ComediDev[index]) {
    dev = comedi_open(outDevStr[port].sName);
    if (!dev) {
      fprintf(stderr, "Comedi open failed\n");
      exit_on_error();
    }
    rt_comedi_get_board_name(dev, board);
    printf("COMEDI %s (%s) opened.\n\n", outDevStr[port].sName, board);
    ComediDev[index] = dev;
    if ((subdev = comedi_find_subdevice_by_type(dev, COMEDI_SUBD_AO, 0)) < 0) {
      fprintf(stderr, "Comedi find_subdevice failed (No analog output)\n");
      comedi_close(dev);
      exit_on_error();
    }
    if ((comedi_lock(dev, subdev)) < 0) {
      fprintf(stderr, "Comedi lock failed for subdevice %d\n", subdev);
      comedi_close(dev);
      exit_on_error();
    }
  } else {
    dev = ComediDev[index];
    subdev = comedi_find_subdevice_by_type(dev, COMEDI_SUBD_AO, 0);
  }
  if ((n_channels = comedi_get_n_channels(dev, subdev)) < 0) {
    printf("Comedi get_n_channels failed for subdevice %d\n", subdev);
    comedi_unlock(dev, subdev);
    comedi_close(dev);
    exit_on_error();
  }
  if (channel >= n_channels) {
    fprintf(stderr, "Comedi channel not available for subdevice %d\n", subdev);
    comedi_unlock(dev, subdev);
    comedi_close(dev);
    exit_on_error();
  }
  maxdata = comedi_get_maxdata(dev, subdev, channel);
  if ((comedi_get_krange(dev, subdev, channel, range, &krange)) < 0) {
    fprintf(stderr, "Comedi get range failed for subdevice %d\n", subdev);
    comedi_unlock(dev, subdev);
    comedi_close(dev);
    exit_on_error();
  }
  ComediDev_InUse[index]++;
  ComediDev_AOInUse[index]++;
  range_min = (double)(krange.min)*1.e-6;
  range_max = (double)(krange.max)*1.e-6;
  printf("AO Channel %d - Range : %1.2f [V] - %1.2f [V]\n", channel, range_min, range_max);
  u = 0.;
  s = (u - range_min)/(range_max - range_min)*maxdata;
  data = (lsampl_t)(floor(s+0.5));
  comedi_data_write(dev, subdev, channel, range, aref, data);

  outDevStr[port].ptr1 = (void *)dev;
  outDevStr[port].dParam[2]  = (double) subdev;
  outDevStr[port].dParam[3]  = range_min;
  outDevStr[port].dParam[4]  = range_max;

  return(port);
}

void out_rtai_comedi_data_output(int port, double * u,double t)
{ 
  unsigned int channel = (unsigned int) outDevStr[port].nch;
  unsigned int range   = (unsigned int) outDevStr[port].dParam[0];
  unsigned int aref    = (unsigned int) outDevStr[port].dParam[1];
  void *dev        = (void *) outDevStr[port].ptr1;
  int subdev       = (int) outDevStr[port].dParam[2];
  double range_min = outDevStr[port].dParam[3];
  double range_max = outDevStr[port].dParam[4];
  lsampl_t data, maxdata = comedi_get_maxdata(dev, subdev, channel);
  double s;

  s = (*u - range_min)/(range_max - range_min)*maxdata;
  if (s < 0) {
    data = 0;
  } else if (s > maxdata) {
    data = maxdata;
  } else {
    data = (lsampl_t)(floor(s+0.5));
  }
  comedi_data_write(dev, subdev, channel, range, aref, data);
}

void inp_rtai_comedi_data_input(int port, double * y, double t)
{
  unsigned int channel = (unsigned int) inpDevStr[port].nch;
  unsigned int range   = (unsigned int) inpDevStr[port].dParam[0];
  unsigned int aref    = (unsigned int) inpDevStr[port].dParam[1];
  void *dev        = (void *) inpDevStr[port].ptr1;
  int subdev       = (int) inpDevStr[port].dParam[2];
  double range_min = inpDevStr[port].dParam[3];
  double range_max = inpDevStr[port].dParam[4];
  lsampl_t data, maxdata = comedi_get_maxdata(dev, subdev, channel);
  double x;

  comedi_data_read(dev, subdev, channel, range, aref, &data);
  x = data;
  x /= maxdata;
  x *= (range_max - range_min);
  x += range_min;
  *y = x;
}

void inp_rtai_comedi_data_update(void)
{
}

void out_rtai_comedi_data_end(int port)
{
  unsigned int channel = (unsigned int) outDevStr[port].nch;
  unsigned int range   = (unsigned int) outDevStr[port].dParam[0];
  unsigned int aref    = (unsigned int) outDevStr[port].dParam[1];
  void *dev        = (void *) outDevStr[port].ptr1;
  int subdev       = (int) outDevStr[port].dParam[2];
  double range_min = outDevStr[port].dParam[3];
  double range_max = outDevStr[port].dParam[4];
  lsampl_t data, maxdata = comedi_get_maxdata(dev, subdev, channel);
  double s;
  int len;

  len=strlen(outDevStr[port].sName);
  int index = outDevStr[port].sName[len-1]-'0';

  s = (0.0 - range_min)/(range_max - range_min)*maxdata;
  if (s < 0) {
    data = 0;
  } else if (s > maxdata) {
    data = maxdata;
  } else {
    data = (lsampl_t)(floor(s+0.5));
  }
  comedi_data_write(dev, subdev, channel, range, aref, data);

  ComediDev_InUse[index]--;
  ComediDev_AOInUse[index]--;
  if (!ComediDev_AOInUse[index]) {
    comedi_unlock(dev, subdev);
  }
  if (!ComediDev_InUse[index]) {
    comedi_close(dev);
    printf("\nCOMEDI %s closed.\n\n", outDevStr[port].sName );
    ComediDev[index] = NULL;
  }
}


void inp_rtai_comedi_data_end(int port)
{
  void *dev        = (void *) inpDevStr[port].ptr1;
  int subdev       = (int) inpDevStr[port].dParam[2];
  int len;

  len=strlen(inpDevStr[port].sName);
  int index = inpDevStr[port].sName[len-1]-'0';

  ComediDev_InUse[index]--;
  ComediDev_AIInUse[index]--;
  if (!ComediDev_AIInUse[index]) {
    comedi_unlock(dev, subdev);
  }
  if (!ComediDev_InUse[index]) {
    comedi_close(dev);
    printf("\nCOMEDI %s closed.\n\n", inpDevStr[port].sName);
    ComediDev[index] = NULL;
  }
}




