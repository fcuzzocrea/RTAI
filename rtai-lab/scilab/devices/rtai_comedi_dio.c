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
#include <rtai_lxrt.h>
#include <rtai_comedi.h>
#include <string.h>

extern void *ComediDev[];
extern int ComediDev_InUse[];
extern int ComediDev_DIOInUse[];

extern devStr inpDevStr[];
extern devStr outDevStr[];
extern int pinp_cnt;
extern int pout_cnt;

int inp_rtai_comedi_dio_init(int nch,char * sName)
{
  void *dev;
  int subdev_type = -1;
  int subdev;
  int len, index;

  int port=pinp_cnt++;

  unsigned int channel;
  int n_channels;
  char board[50];

  inpDevStr[port].nch=nch;
  sprintf(inpDevStr[port].sName,"/dev/%s",sName);
  strcpy(inpDevStr[port].IOName,"Comedi DIO input");

  channel = nch;
  len=strlen(inpDevStr[port].sName);
  index = inpDevStr[port].sName[len-1]-'0';

  if (!ComediDev[index]) {
    dev = comedi_open(inpDevStr[port].sName);
    if (!dev) {
      fprintf(stderr, "Comedi open failed\n");
      exit_on_error();
    }
    rt_comedi_get_board_name(dev, board);
    printf("COMEDI %s (%s) opened.\n\n", inpDevStr[port].sName, board);
    ComediDev[index] = dev;

    if ((subdev = comedi_find_subdevice_by_type(dev, COMEDI_SUBD_DI, 0)) < 0) {
      fprintf(stderr, "Comedi find_subdevice failed (No digital Input)\n");
    }else {
      subdev_type = COMEDI_SUBD_DI;
    }  
    if(subdev == -1){
      if ((subdev = comedi_find_subdevice_by_type(dev, COMEDI_SUBD_DIO, 0)) < 0) {
	fprintf(stderr, "Comedi find_subdevice failed (No digital I/O)\n");
	comedi_close(dev);
	return;
      }else{
	subdev_type = COMEDI_SUBD_DIO;
      }  
    }

    if ((comedi_lock(dev, subdev)) < 0) {
      fprintf(stderr, "Comedi lock failed for subdevice %d\n", subdev);
      comedi_close(dev);
      exit_on_error();
    }
  } else {
    dev = ComediDev[index];

    if((subdev = comedi_find_subdevice_by_type(dev, COMEDI_SUBD_DI, 0)) < 0){
      subdev = comedi_find_subdevice_by_type(dev, COMEDI_SUBD_DIO, 0);
      subdev_type =COMEDI_SUBD_DIO;
    }else subdev_type =COMEDI_SUBD_DI; 
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

  if(subdev_type == COMEDI_SUBD_DIO){
    if ((comedi_dio_config(dev, subdev, channel, COMEDI_INPUT)) < 0) {
      fprintf(stderr, "Comedi DIO config failed for subdevice %d\n", subdev);
      comedi_unlock(dev, subdev);
      comedi_close(dev);
      return;
    }	
  }	

  ComediDev_InUse[index]++;
  ComediDev_DIOInUse[index]++;
  comedi_dio_write(dev, subdev, channel, 0);

  inpDevStr[port].ptr1 = (void *)dev;
  inpDevStr[port].dParam[2]  = (double) subdev;

  return(port);
}

int out_rtai_comedi_dio_init(int nch,char * sName,char * sParam,double threshold)
{
  void *dev;
  int subdev;
  int subdev_type = -1;

  unsigned int channel;
  int n_channels;
  char board[50];
  int len, index;
  int port=pout_cnt++;

  outDevStr[port].nch=nch;
  strcpy(outDevStr[port].IOName,"Comedi DIO output");
  sprintf(outDevStr[port].sName,"/dev/%s",sName);
  outDevStr[port].dParam[0]=threshold;

  len=strlen(outDevStr[port].sName);
  index = outDevStr[port].sName[len-1]-'0';
  channel = nch;
  if (!ComediDev[index]) {
    dev = comedi_open(outDevStr[port].sName);
    if (!dev) {
      fprintf(stderr, "Comedi open failed\n");
      exit_on_error();
    }
    rt_comedi_get_board_name(dev, board);
    printf("COMEDI %s (%s) opened.\n\n", outDevStr[port].sName, board);
    ComediDev[index] = dev;

    if ((subdev = comedi_find_subdevice_by_type(dev, COMEDI_SUBD_DO, 0)) < 0) {
      //      fprintf(stderr, "Comedi find_subdevice failed (No digital I/O)\n");
    }else {
      subdev_type = COMEDI_SUBD_DO;
    }
    if(subdev == -1){
      if ((subdev = comedi_find_subdevice_by_type(dev, COMEDI_SUBD_DIO, 0)) < 0) {
	fprintf(stderr, "Comedi find_subdevice failed (No digital Output)\n");
	comedi_close(dev);
	return;
      }else{
	subdev_type = COMEDI_SUBD_DIO;
      }  
    }  

    if ((comedi_lock(dev, subdev)) < 0) {
      fprintf(stderr, "Comedi lock failed for subdevice %d\n", subdev);
      comedi_close(dev);
      exit_on_error();
    }
  } else {
    dev = ComediDev[index];
    if((subdev = comedi_find_subdevice_by_type(dev, COMEDI_SUBD_DO, 0)) < 0){
      subdev = comedi_find_subdevice_by_type(dev, COMEDI_SUBD_DIO, 0);
      subdev_type =COMEDI_SUBD_DIO;
    }else subdev_type =COMEDI_SUBD_DO; 
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

  if(subdev_type == COMEDI_SUBD_DIO){
    if ((comedi_dio_config(dev, subdev, channel, COMEDI_OUTPUT)) < 0) {
      fprintf(stderr, "Comedi DIO config failed for subdevice %d\n", subdev);
      comedi_unlock(dev, subdev);
      comedi_close(dev);
      return;
    }
  }

  ComediDev_InUse[index]++;
  ComediDev_DIOInUse[index]++;
  comedi_dio_write(dev, subdev, channel, 0);

  outDevStr[port].ptr1 = (void *)dev;
  outDevStr[port].dParam[2]  = (double) subdev;

  return(port);
}

void out_rtai_comedi_dio_output(int port, double * u,double t)
{ 
  unsigned int channel = (unsigned int) outDevStr[port].nch;
  void *dev        = (void *) outDevStr[port].ptr1;
  int subdev       = (int) outDevStr[port].dParam[2];
  unsigned int bit = 0;
  double threshold = outDevStr[port].dParam[0];

  if (*u >= threshold) {
    bit=1;
  }
  comedi_dio_write(dev, subdev, channel, bit);
}

void inp_rtai_comedi_dio_input(int port, double * y, double t)
{
  unsigned int channel = (unsigned int) inpDevStr[port].nch;
  void *dev        = (void *) inpDevStr[port].ptr1;
  int subdev       = (int) inpDevStr[port].dParam[2];
  unsigned int bit;

  comedi_dio_read(dev, subdev, channel, &bit);
  *y = (double)bit;
}

void inp_rtai_comedi_dio_update(void)
{
}

void out_rtai_comedi_dio_end(int port)
{
  unsigned int channel = (unsigned int) outDevStr[port].nch;
  void *dev        = (void *) outDevStr[port].ptr1;
  int subdev       = (int) outDevStr[port].dParam[2];
  int len, index;

  len=strlen(outDevStr[port].sName);
  index = outDevStr[port].sName[len-1]-'0';

  comedi_dio_write(dev, subdev, channel, 0);
  ComediDev_InUse[index]--;
  ComediDev_DIOInUse[index]--;
  if (!ComediDev_DIOInUse[index]) {
    comedi_unlock(dev, subdev);
  }
  if (!ComediDev_InUse[index]) {
    comedi_close(dev);
    printf("\nCOMEDI %s closed.\n\n", outDevStr[port].sName);
    ComediDev[index] = NULL;
  }
}

void inp_rtai_comedi_dio_end(int port)
{
  void *dev        = (void *) inpDevStr[port].ptr1;
  int subdev       = (int) inpDevStr[port].dParam[2];
  int len, index;

  len=strlen(inpDevStr[port].sName);
  index = inpDevStr[port].sName[len-1]-'0';

  ComediDev_InUse[index]--;
  ComediDev_DIOInUse[index]--;
  if (!ComediDev_DIOInUse[index]) {
    comedi_unlock(dev, subdev);
  }
  if (!ComediDev_InUse[index]) {
    comedi_close(dev);
    printf("\nCOMEDI %s closed.\n\n", inpDevStr[port].sName);
    ComediDev[index] = NULL;
  }
}




