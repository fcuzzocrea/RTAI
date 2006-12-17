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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>

#include "rtmain.h"
#include <rtai_netrpc.h>
#include <rtai_msg.h>
#include <rtai_mbx.h>
#include <rtai_lxrt.h>
#include <rtai_comedi.h>
#include <rtai_fifos.h>
#include <rtai_sem.h>

extern void *ComediDev[];
extern int ComediDev_InUse[];
extern int ComediDev_AOInUse[];
extern int ComediDev_AIInUse[];
extern int ComediDev_DIOInUse[];

struct oACOMDev{
  int channel;
  char devName[20];
  unsigned int range;
  unsigned int aref;
  void * dev;
  int subdev;
  double range_min;
  double range_max;
};

struct oDCOMDev{
  int channel;
  char devName[20];
  void * dev;
  int subdev;
  int subdev_type;
  double threshold;
};

void * inp_rtai_comedi_data_init(int nch,char * sName,int Range, int aRef)
{
  struct oACOMDev * comdev = (struct oACOMDev *) malloc(sizeof(struct oACOMDev));
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
  struct oACOMDev * comdev = (struct oACOMDev *) malloc(sizeof(struct oACOMDev));
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
  struct oACOMDev * comdev = (struct oACOMDev *) ptr;

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
  struct oACOMDev * comdev = (struct oACOMDev *) ptr;

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
  struct oACOMDev * comdev = (struct oACOMDev *) ptr;

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
  struct oACOMDev * comdev = (struct oACOMDev *) ptr;

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

void * inp_rtai_comedi_dio_init(int nch,char * sName)
{
  struct oDCOMDev * comdev = (struct oDCOMDev *) malloc(sizeof(struct oDCOMDev));
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
  struct oDCOMDev * comdev = (struct oDCOMDev *) malloc(sizeof(struct oDCOMDev));
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
  struct oDCOMDev * comdev = (struct oDCOMDev *) ptr;
  unsigned int bit = 0;

  if (*u >= comdev->threshold) {
    bit=1;
  }
  comedi_dio_write(comdev->dev,comdev->subdev, comdev->channel, bit);
}

void inp_rtai_comedi_dio_input(void * ptr, double * y, double t)
{
  struct oDCOMDev * comdev = (struct oDCOMDev *) ptr;
  unsigned int bit;

  comedi_dio_read(comdev->dev, comdev->subdev, comdev->channel, &bit);
  *y = (double)bit;
}

void inp_rtai_comedi_dio_update(void)
{
}

void out_rtai_comedi_dio_end(void * ptr)
{
  struct oDCOMDev * comdev = (struct oDCOMDev *) ptr;
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
  struct oDCOMDev * comdev = (struct oDCOMDev *) ptr;
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



