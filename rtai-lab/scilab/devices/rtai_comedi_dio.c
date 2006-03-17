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
#include <rtai_lxrt.h>
#include <rtai_comedi.h>
#include <string.h>
#include <stdlib.h>

extern void *ComediDev[];
extern int ComediDev_InUse[];
extern int ComediDev_DIOInUse[];

struct DCOMDev{
  int channel;
  char devName[20];
  void * dev;
  int subdev;
  int subdev_type;
  double threshold;
};

void * inp_rtai_comedi_dio_init(int nch,char * sName)
{
  struct DCOMDev * comdev = (struct DCOMDev *) malloc(sizeof(struct DCOMDev));
  comdev->subdev_type = -1;
  int len, index;

  int n_channels;
  char board[50];

  comdev->channel=nch;
  sprintf(comdev->devName,"/dev/%s",sName);

  len=strlen(comdev->devName);
  index = comdev->devName[len-1]-'0';

  if (!ComediDev[index]) {
    comdev->dev = comedi_open(comdev->devName);
    if (!(comdev->dev)) {
      fprintf(stderr, "Comedi open failed\n");
      exit_on_error();
    }
    rt_comedi_get_board_name(comdev->dev, board);
    printf("COMEDI %s (%s) opened.\n\n", comdev->devName, board);
    ComediDev[index] = comdev->dev;

    if ((comdev->subdev = comedi_find_subdevice_by_type(comdev->dev, COMEDI_SUBD_DI, 0)) < 0) {
      fprintf(stderr, "Comedi find_subdevice failed (No digital Input)\n");
    }else {
      comdev->subdev_type = COMEDI_SUBD_DI;
    }  
    if(comdev->subdev == -1){
      if ((comdev->subdev = comedi_find_subdevice_by_type(comdev->dev, COMEDI_SUBD_DIO, 0)) < 0) {
	fprintf(stderr, "Comedi find_subdevice failed (No digital I/O)\n");
	comedi_close(comdev->dev);
	exit_on_error();
      }else{
	comdev->subdev_type = COMEDI_SUBD_DIO;
      }  
    }

    if ((comedi_lock(comdev->dev,comdev-> subdev)) < 0) {
      fprintf(stderr, "Comedi lock failed for subdevice %d\n", comdev->subdev);
      comedi_close(comdev->dev);
      exit_on_error();
    }
  } else {
    comdev->dev = ComediDev[index];

    if((comdev->subdev = comedi_find_subdevice_by_type(comdev->dev, COMEDI_SUBD_DI, 0)) < 0){
      comdev->subdev = comedi_find_subdevice_by_type(comdev->dev, COMEDI_SUBD_DIO, 0);
      comdev->subdev_type =COMEDI_SUBD_DIO;
    }else comdev->subdev_type =COMEDI_SUBD_DI; 
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

  if(comdev->subdev_type == COMEDI_SUBD_DIO){
    if ((comedi_dio_config(comdev->dev, comdev->subdev, comdev->channel, COMEDI_INPUT)) < 0) {
      fprintf(stderr, "Comedi DIO config failed for subdevice %d\n", comdev->subdev);
      comedi_unlock(comdev->dev, comdev->subdev);
      comedi_close(comdev->dev);
      exit_on_error();
    }	
  }	

  ComediDev_InUse[index]++;
  ComediDev_DIOInUse[index]++;
  comedi_dio_write(comdev->dev, comdev->subdev, comdev->channel, 0);

  return((void *) comdev);
}

void * out_rtai_comedi_dio_init(int nch,char * sName,double threshold)
{
  struct DCOMDev * comdev = (struct DCOMDev *) malloc(sizeof(struct DCOMDev));
  comdev->subdev_type = -1;

  int n_channels;
  char board[50];
  int len, index;

  comdev->channel=nch;
  sprintf(comdev->devName,"/dev/%s",sName);
  comdev->threshold=threshold;

  len=strlen(comdev->devName);
  index = comdev->devName[len-1]-'0';

  if (!ComediDev[index]) {
    comdev->dev = comedi_open(comdev->devName);
    if (!(comdev->dev)) {
      fprintf(stderr, "Comedi open failed\n");
      exit_on_error();
    }
    rt_comedi_get_board_name(comdev->dev, board);
    printf("COMEDI %s (%s) opened.\n\n", comdev->devName, board);
    ComediDev[index] = comdev->dev;

    if ((comdev->subdev = comedi_find_subdevice_by_type(comdev->dev, COMEDI_SUBD_DO, 0)) < 0) {
      //      fprintf(stderr, "Comedi find_subdevice failed (No digital I/O)\n");
    }else {
      comdev->subdev_type = COMEDI_SUBD_DO;
    }
    if(comdev->subdev == -1){
      if ((comdev->subdev = comedi_find_subdevice_by_type(comdev->dev, COMEDI_SUBD_DIO, 0)) < 0) {
	fprintf(stderr, "Comedi find_subdevice failed (No digital Output)\n");
	comedi_close(comdev->dev);
	exit_on_error();
      }else{
	comdev->subdev_type = COMEDI_SUBD_DIO;
      }  
    }  

    if ((comedi_lock(comdev->dev, comdev->subdev)) < 0) {
      fprintf(stderr, "Comedi lock failed for subdevice %d\n",comdev-> subdev);
      comedi_close(comdev->dev);
      exit_on_error();
    }
  } else {
    comdev->dev = ComediDev[index];
    if((comdev->subdev = comedi_find_subdevice_by_type(comdev->dev, COMEDI_SUBD_DO, 0)) < 0){
      comdev->subdev = comedi_find_subdevice_by_type(comdev->dev, COMEDI_SUBD_DIO, 0);
      comdev->subdev_type =COMEDI_SUBD_DIO;
    }else comdev->subdev_type =COMEDI_SUBD_DO; 
  }
  if ((n_channels = comedi_get_n_channels(comdev->dev, comdev->subdev)) < 0) {
    fprintf(stderr, "Comedi get_n_channels failed for subdevice %d\n", comdev->subdev);
    comedi_unlock(comdev->dev, comdev->subdev);
    comedi_close(comdev->dev);
    exit_on_error();
  }
  if (comdev->channel >= n_channels) {
    fprintf(stderr, "Comedi channel not available for subdevice %d\n",comdev-> subdev);
    comedi_unlock(comdev->dev, comdev->subdev);
    comedi_close(comdev->dev);
    exit_on_error();
  }

  if(comdev->subdev_type == COMEDI_SUBD_DIO){
    if ((comedi_dio_config(comdev->dev,comdev->subdev, comdev->channel, COMEDI_OUTPUT)) < 0) {
      fprintf(stderr, "Comedi DIO config failed for subdevice %d\n", comdev->subdev);
      comedi_unlock(comdev->dev, comdev->subdev);
      comedi_close(comdev->dev);
      exit_on_error();
    }
  }

  ComediDev_InUse[index]++;
  ComediDev_DIOInUse[index]++;
  comedi_dio_write(comdev->dev, comdev->subdev, comdev->channel, 0);

  return((void *)comdev);
}

void out_rtai_comedi_dio_output(void * ptr, double * u,double t)
{ 
  struct DCOMDev * comdev = (struct DCOMDev *) ptr;
  unsigned int bit = 0;

  if (*u >= comdev->threshold) {
    bit=1;
  }
  comedi_dio_write(comdev->dev,comdev->subdev, comdev->channel, bit);
}

void inp_rtai_comedi_dio_input(void * ptr, double * y, double t)
{
  struct DCOMDev * comdev = (struct DCOMDev *) ptr;
  unsigned int bit;

  comedi_dio_read(comdev->dev, comdev->subdev, comdev->channel, &bit);
  *y = (double)bit;
}

void inp_rtai_comedi_dio_update(void)
{
}

void out_rtai_comedi_dio_end(void * ptr)
{
  struct DCOMDev * comdev = (struct DCOMDev *) ptr;
  int len, index;

  len=strlen(comdev->devName);
  index = comdev->devName[len-1]-'0';

  comedi_dio_write(comdev->dev, comdev->subdev, comdev->channel, 0);
  ComediDev_InUse[index]--;
  ComediDev_DIOInUse[index]--;
  if (!ComediDev_DIOInUse[index]) {
    comedi_unlock(comdev->dev, comdev->subdev);
  }
  if (!ComediDev_InUse[index]) {
    comedi_close(comdev->dev);
    printf("\nCOMEDI %s closed.\n\n", comdev->devName);
    ComediDev[index] = NULL;
  }
  free(comdev);
}

void inp_rtai_comedi_dio_end(void * ptr)
{
  struct DCOMDev * comdev = (struct DCOMDev *) ptr;
  int len, index;

  len=strlen(comdev->devName);
  index = comdev->devName[len-1]-'0';

  ComediDev_InUse[index]--;
  ComediDev_DIOInUse[index]--;
  if (!ComediDev_DIOInUse[index]) {
    comedi_unlock(comdev->dev, comdev->subdev);
  }
  if (!ComediDev_InUse[index]) {
    comedi_close(comdev->dev);
    printf("\nCOMEDI %s closed.\n\n", comdev->devName);
    ComediDev[index] = NULL;
  }
  free(comdev);
}




