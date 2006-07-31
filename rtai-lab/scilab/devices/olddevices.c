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

#define MAX_RTAI_LEDS               1000
#define MBX_RTAI_LED_SIZE           5000
#define MAX_RTAI_METERS             1000
#define MBX_RTAI_METER_SIZE         5000
#define MAX_RTAI_SCOPES             1000
#define MBX_RTAI_SCOPE_SIZE         5000
#define FIFO_SIZE                   50000

extern void *ComediDev[];
extern int ComediDev_InUse[];
extern int ComediDev_AOInUse[];
extern int ComediDev_AIInUse[];
extern int ComediDev_DIOInUse[];
extern char *TargetMeterMbxID;
extern char *TargetMbxID;
extern char *TargetLedMbxID;

struct oMbxR{
  int nch;
  char mbxName[10];
  MBX * mbx;
  long tNode;
  long tPort;
  double * oldVal;
};

struct oMbxOwS{
  int nch;
  char mbxName[10];
  MBX * mbx;
  long tNode;
  long tPort;
};

struct oMbxRif{
  int nch;
  char mbxName[10];
  MBX * mbx;
  long tNode;
  long tPort;
  double * oldVal;
};

struct oMbxSif{
  int nch;
  char mbxName[10];
  MBX * mbx;
  long tNode;
  long tPort;
};

struct oextData{
  int nData;
  double * pData;
  int cnt;
};

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

struct oLed{
  int nch;
  char ledName[20];
  MBX * mbx;
};

struct oFifo{
  int nch;
  int fifon;
};

struct ometer{
  char meterName[20];
  MBX * mbx;
};

struct oscope{
  int nch;
  char scopeName[20];
  MBX * mbx;
};

struct oSem{
  char semName[20];
  SEM * sem;
  long tNode;
  long tPort;
};

void * inp_extdata_init(int nch,char * sName)
{
  FILE * fp;
  int i;

  if(nch==0) {
    fprintf(stderr, "Error - Data length is 0!\n");
    exit_on_error();
  }

  struct oextData * data;
  data=(struct oextData *) malloc(sizeof(struct oextData));

  data->pData=(double *) calloc(nch,sizeof(double));
  fp=fopen(sName,"r");
  if(fp!=NULL){
    data->cnt=0;
    for(i=0;i<nch;i++) {
      if(feof(fp)) break;
      fscanf(fp,"%lf",&(data->pData[i]));
    }
    data->nData=nch;
    fclose(fp);
  }
  else{
    fprintf(stderr, "File %s not found!\n",sName);
    data->cnt=-1;
    exit_on_error();
  }

  return((void *) data);
}

void inp_extdata_input(void * ptr, double * y, double t)
{
  struct oextData * data = (struct oextData *) ptr;
  if(data->nData>=0) {
    y[0]=data->pData[data->cnt];
    data->cnt = data->cnt % data->nData;
  }
  else y[0]=0.0;
}

void inp_extdata_update(void)
{
}

void inp_extdata_end(void * ptr)
{
  struct oextData * data = (struct oextData *) ptr;
  free(data->pData);
  free(data);
  printf("EXTDATA closed\n");
}



void * out_mbx_ovrwr_send_init(int nch,char * sName,char * IP)
{
  struct oMbxOwS * mbx = (struct oMbxOwS *) malloc(sizeof(struct oMbxOwS));
  mbx->nch=nch;
  strcpy(mbx->mbxName,sName);

  struct sockaddr_in addr;

  if(!strcmp(IP,"0")) {
    mbx->tNode = 0;
    mbx->tPort = 0;
  }
  else {
    inet_aton(IP, &addr.sin_addr);
    mbx->tNode = addr.sin_addr.s_addr;
    while ((mbx->tPort = rt_request_port_id(mbx->tNode,nam2num(sName))) <= 0 && mbx->tPort != -EINVAL);
  }

  mbx->mbx = (MBX *) RT_typed_named_mbx_init(mbx->tNode,mbx->tPort,sName,nch*sizeof(double),FIFO_Q);

  if(mbx->mbx == NULL) {
    fprintf(stderr, "Error in getting %s mailbox address\n", sName);
    exit_on_error();
  }

  return((void *) mbx);
}

void out_mbx_ovrwr_send_output(void * ptr, double * u,double t)
{
  struct oMbxOwS * mbx = (struct oMbxOwS *) ptr;
  int ntraces = mbx->nch;
  struct{
    double u[ntraces];
  } data;
  int i;

  for(i=0;i<ntraces;i++){
    data.u[i] = u[i];
  }
  RT_mbx_ovrwr_send(mbx->tNode, mbx->tPort,mbx->mbx,&data,sizeof(data));
}

void out_mbx_ovrwr_send_end(void * ptr)
{
  struct oMbxOwS * mbx = (struct oMbxOwS *) ptr;

  RT_named_mbx_delete(mbx->tNode, mbx->tPort,mbx->mbx);
  printf("OVRWR MBX %s closed\n",mbx->mbxName);
  if(mbx->tNode){
    rt_release_port(mbx->tNode,mbx->tPort);
  }
  free(mbx);
}

void * inp_mbx_receive_init(int nch,char * sName,char * IP)
{
  struct oMbxR * mbx = (struct oMbxR *) malloc(sizeof(struct oMbxR));
  mbx->nch=nch;
  strcpy(mbx->mbxName,sName);

  struct sockaddr_in addr;

  if(!strcmp(IP,"0")) {
    mbx->tNode = 0;
    mbx->tPort = 0;
  }
  else {
    inet_aton(IP, &addr.sin_addr);
    mbx->tNode = addr.sin_addr.s_addr;
    while ((mbx->tPort = rt_request_port_id(mbx->tNode,nam2num(sName))) <= 0 && mbx->tPort != -EINVAL);
  }

  mbx->mbx = (MBX *) RT_typed_named_mbx_init(mbx->tNode,mbx->tPort,sName,nch*sizeof(double),FIFO_Q);

  if(mbx->mbx == NULL) {
    fprintf(stderr, "Error in getting %s mailbox address\n", sName);
    exit_on_error();
  }
  mbx->oldVal = calloc(nch,sizeof(double));

  return((void *) mbx);
}

void inp_mbx_receive_input(void * ptr, double * y, double t)
{
  struct oMbxR * mbx = (struct oMbxR *) ptr;

  int ntraces = mbx->nch;
  struct{
    double u[ntraces];
  } data;
  int i;

  if(!RT_mbx_receive(mbx->tNode, mbx->tPort, mbx->mbx, &data, sizeof(data))) {
  for(i=0;i<ntraces;i++){
      mbx->oldVal[i] = data.u[i];
  }
  }
  for(i=0;i<ntraces;i++) y[i] = mbx->oldVal[i];
}

void inp_mbx_receive_update(void)
{
}

void inp_mbx_receive_end(void * ptr)
{
  struct oMbxR * mbx = (struct oMbxR *) ptr;

  RT_named_mbx_delete(mbx->tNode, mbx->tPort,mbx->mbx);
  printf("RECEIVE MBX %s closed\n",mbx->mbxName);
  if(mbx->tNode){
    rt_release_port(mbx->tNode,mbx->tPort);
  }
  free(mbx->oldVal);
  free(mbx);
}


void * inp_mbx_receive_if_init(int nch,char * sName,char * IP)
{
  struct oMbxRif * mbx = (struct oMbxRif *) malloc(sizeof(struct oMbxRif));
  mbx->nch=nch;
  strcpy(mbx->mbxName,sName);

  struct sockaddr_in addr;

  if(!strcmp(IP,"0")) {
    mbx->tNode = 0;
    mbx->tPort = 0;
  }
  else {
    inet_aton(IP, &addr.sin_addr);
    mbx->tNode = addr.sin_addr.s_addr;
    while ((mbx->tPort = rt_request_port_id(mbx->tNode,nam2num(sName))) <= 0 && mbx->tPort != -EINVAL);
  }

  mbx->mbx = (MBX *) RT_typed_named_mbx_init(mbx->tNode,mbx->tPort,sName,nch*sizeof(double),FIFO_Q);

  if(mbx->mbx == NULL) {
    fprintf(stderr, "Error in getting %s mailbox address\n", sName);
    exit_on_error();
  }
  mbx->oldVal = calloc(nch,sizeof(double));

  return((void *) mbx);
}

void inp_mbx_receive_if_input(void * ptr, double * y, double t)
{
  struct oMbxRif * mbx = (struct oMbxRif *) ptr;

  int ntraces = mbx->nch;
  struct{
    double u[ntraces];
  } data;
  int i;

  if(!RT_mbx_receive_if(mbx->tNode, mbx->tPort, mbx->mbx, &data, sizeof(data))) {
  for(i=0;i<ntraces;i++){
      mbx->oldVal[i] = data.u[i];
  }
  }
  for(i=0;i<ntraces;i++) y[i] = mbx->oldVal[i];
}

void inp_mbx_receive_if_update(void)
{
}

void inp_mbx_receive_if_end(void * ptr)
{
  struct oMbxRif * mbx = (struct oMbxRif *) ptr;

  RT_named_mbx_delete(mbx->tNode, mbx->tPort,mbx->mbx);
  printf("RECEIVE IF MBX %s closed\n",mbx->mbxName);
  if(mbx->tNode){
    rt_release_port(mbx->tNode,mbx->tPort);
  }
  free(mbx->oldVal);
  free(mbx); 
}


void * out_mbx_send_if_init(int nch,char * sName,char * IP)
{
  struct oMbxSif * mbx = (struct oMbxSif *) malloc(sizeof(struct oMbxSif));
  mbx->nch=nch;
  strcpy(mbx->mbxName,sName);

  struct sockaddr_in addr;

  if(!strcmp(IP,"0")) {
    mbx->tNode = 0;
    mbx->tPort = 0;
  }
  else {
    inet_aton(IP, &addr.sin_addr);
    mbx->tNode = addr.sin_addr.s_addr;
    while ((mbx->tPort = rt_request_port_id(mbx->tNode,nam2num(sName))) <= 0 && mbx->tPort != -EINVAL);
  }

  mbx->mbx = (MBX *) RT_typed_named_mbx_init(mbx->tNode,mbx->tPort,sName,nch*sizeof(double),FIFO_Q);

  if(mbx->mbx == NULL) {
    fprintf(stderr, "Error in getting %s mailbox address\n", sName);
    exit_on_error();
  }

  return((void *) mbx);
}

void out_mbx_send_if_output(void * ptr, double * u,double t)
{
  struct oMbxSif * mbx = (struct oMbxSif *) ptr;
  int ntraces = mbx->nch;
  struct{
    double u[ntraces];
  } data;
  int i;

  for(i=0;i<ntraces;i++){
    data.u[i] = u[i];
  }
  RT_mbx_send_if(mbx->tNode, mbx->tPort,mbx->mbx,&data,sizeof(data));
}

void out_mbx_send_if_end(void * ptr)
{
  struct oMbxSif * mbx = (struct oMbxSif *) ptr;

  RT_named_mbx_delete(mbx->tNode, mbx->tPort,mbx->mbx);
  printf("MBX %s closed\n",mbx->mbxName);
  if(mbx->tNode){
    rt_release_port(mbx->tNode,mbx->tPort);
  }
  free(mbx);
}




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




void * inp_rtai_fifo_init(int nch,char * sName,char * sParam,double p1,
		       double p2, double p3, double p4, double p5)
{
  return(NULL);
}

void inp_rtai_fifo_input(void * ptr, double * y, double t)
{
  /*     *y=XXXX; */
}

void inp_rtai_fifo_update(void)
{
}

void inp_rtai_fifo_end(void * ptr)
{
}

void * out_rtai_fifo_init(int nch, int fifon)
{
  struct oFifo * fifo = (struct oFifo *) malloc(sizeof(struct oFifo));
  fifo->nch=nch;
  fifo->fifon=fifon;

  rtf_create(fifon,FIFO_SIZE);
  rtf_reset(fifon);

  return((void *) fifo);
}

void out_rtai_fifo_output(void * ptr, double * u,double t)
{
  struct oFifo * fifo = (struct oFifo *) ptr;
  int ntraces=fifo->nch;
  struct {
    float t;
    float u[ntraces];
  } data;
  int i;

  data.t=(float) t;
  for (i = 0; i < ntraces; i++) {
    data.u[i] = (float) u[i];
  }
  rtf_put(fifo->fifon,&data, sizeof(data));
}

void out_rtai_fifo_end(void * ptr)
{
  struct oFifo * fifo = (struct oFifo *) ptr;
  rtf_destroy(fifo->fifon);
  printf("FIFO %d closed\n",fifo->fifon);
  free(fifo);
}




void * out_rtai_led_init(int nch,char * sName)
{
  struct oLed * led;

  led=(struct oLed *) malloc(sizeof(struct oLed));

  led->nch=nch;
  strcpy(led->ledName,sName);
  char name[7];

  rtRegisterLed(sName,nch);
  get_a_name(TargetLedMbxID,name);

  led->mbx = (MBX *) RT_typed_named_mbx_init(0,0,name,(MBX_RTAI_LED_SIZE/(sizeof(unsigned int)))*(sizeof(unsigned int)),FIFO_Q);
  if(led->mbx == NULL) {
    fprintf(stderr, "Cannot init mailbox\n");
    exit_on_error();
  }

  return((void *) led);
}

void out_rtai_led_output(void * ptr, double * u, double t)
{
  struct oLed * led = (struct oLed *) ptr;
  int nleds=led->nch;
  int i;
  unsigned int led_mask = 0;

  for (i = 0; i < nleds; i++) {
    if (u[i] > 0.) {
      led_mask += (1 << i);
    } else {
      led_mask += (0 << i);
    }
  }
  RT_mbx_send_if(0, 0, led->mbx, &led_mask, sizeof(led_mask));
}

void out_rtai_led_end(void * ptr)
{
  struct oLed * led = (struct oLed *) ptr;
  RT_named_mbx_delete(0, 0, led->mbx);
  printf("Led %s closed\n",led->ledName);
  free(led);
}



void * out_rtai_meter_init(char * sName)
{
  struct ometer * met;
  char name[7];

  met = (struct ometer *) malloc(sizeof(struct ometer));
  strcpy(met->meterName,sName);
  rtRegisterMeter(sName,1);
  get_a_name(TargetMeterMbxID,name);

  met->mbx = (MBX *) RT_typed_named_mbx_init(0,0,name,(MBX_RTAI_METER_SIZE/(sizeof(float)))*(sizeof(float)),FIFO_Q);
  if(met->mbx == NULL) {
    fprintf(stderr, "Cannot init mailbox\n");
    exit_on_error();
  }

  return((void *) met);
}

void out_rtai_meter_output(void * ptr, double * u, double t)
{
  struct ometer * met = (struct ometer *) ptr;
  float data;

  data = (float) *u;
  RT_mbx_send_if(0, 0, met->mbx, &data, sizeof(data));
}

void out_rtai_meter_end(void * ptr)
{
  struct ometer * met = (struct ometer *) ptr;
  RT_named_mbx_delete(0, 0, met->mbx);
  printf("Meter %s closed\n",met->meterName);
  free(met);
}



void * out_rtai_scope_init(int nch,char * sName)
{
  struct oscope * scp = (struct oscope *) malloc(sizeof(struct oscope));
  scp->nch=nch;
  strcpy(scp->scopeName,sName);
  char name[7];
  int nt=nch + 1;
  rtRegisterScope(sName,nch);
  get_a_name(TargetMbxID,name);

  scp->mbx = (MBX *) RT_typed_named_mbx_init(0,0,name,(5000/(nt*sizeof(float)))*(nt*sizeof(float)),FIFO_Q);
  if(scp->mbx == NULL) {
    fprintf(stderr, "Cannot init mailbox\n");
    exit_on_error();
  }

  return((void *) scp);
}

void out_rtai_scope_output(void * ptr, double * u, double t)
{
  struct oscope * scp = (struct oscope *) ptr;
  int ntraces=scp->nch;
  struct {
    float t;
    float u[ntraces];
  } data;
  int i;

  data.t=(float) t;
  for (i = 0; i < ntraces; i++) {
    data.u[i] = (float) u[i];
  }
  RT_mbx_send_if(0, 0, scp->mbx, &data, sizeof(data));
}

void out_rtai_scope_end(void * ptr)
{
  struct oscope * scp = (struct oscope *) ptr;
  RT_named_mbx_delete(0, 0, scp->mbx);
  printf("Scope %s closed\n",scp->scopeName);
  free(scp);
}



void * inp_rtai_sem_init(char * sName,char * IP)
{
  struct oSem * sem = (struct oSem *) malloc(sizeof(struct oSem));
  strcpy(sem->semName,sName);

  struct sockaddr_in addr;

  if(!strcmp(IP,"0")) {
    sem->tNode = 0;
    sem->tPort = 0;
  }
  else {
    inet_aton(IP, &addr.sin_addr);
    sem->tNode = addr.sin_addr.s_addr;
    while ((sem->tPort = rt_request_port(sem->tNode)) <= 0
	   && sem->tPort != -EINVAL);
  }

  sem->sem = RT_typed_named_sem_init(sem->tNode,sem->tPort,sem->semName, 0, CNT_SEM);
  if(sem->sem == NULL) {
    fprintf(stderr, "Error in getting %s semaphore address\n", sem->semName);
    exit_on_error();
  }

  return((void *) sem);
}

void inp_rtai_sem_input(void * ptr, double * y, double t)
{
  struct oSem * sem = (struct oSem *) ptr;
  int ret;

  ret = RT_sem_wait(sem->tNode, sem->tPort,sem->sem);
  y[0]=1.0;
}

void inp_rtai_sem_update(void)
{
}

void inp_rtai_sem_end(void * ptr)
{
  struct oSem * sem = (struct oSem *) ptr;
  RT_named_sem_delete(sem->tNode, sem->tPort,sem->sem);
  if(sem->tNode){
    rt_release_port(sem->tNode, sem->tPort);
  }
  printf("SEM %s closed\n",sem->semName);
  free(sem);
}

void * out_rtai_sem_init(char * sName,char * IP)
{
  struct oSem * sem = (struct oSem *) malloc(sizeof(struct oSem));
  strcpy(sem->semName,sName);
  struct sockaddr_in addr;

  if(!strcmp(IP,"0")) {
    sem->tNode = 0;
    sem->tPort = 0;
  }
  else {
    inet_aton(IP, &addr.sin_addr);
    sem->tNode = addr.sin_addr.s_addr;
    while ((sem->tPort = rt_request_port(sem->tNode)) <= 0
	   && sem->tPort != -EINVAL);
  }

  sem->sem = RT_typed_named_sem_init(sem->tNode,sem->tPort,sem->semName, 0, CNT_SEM);
  if(sem->sem == NULL) {
    fprintf(stderr, "Error in getting %s semaphore address\n", sem->semName);
    exit_on_error();
  }

  return((void *) sem);
}

void out_rtai_sem_output(void * ptr, double * u,double t)
{
  struct oSem * sem = (struct oSem *) ptr;
  int ret; 
  if(*u > 0.0) ret = RT_sem_signal(sem->tNode, sem->tPort,sem->sem);
}

void out_rtai_sem_end(void * ptr)
{
  struct oSem * sem = (struct oSem *) ptr;
  RT_named_sem_delete(sem->tNode, sem->tPort,sem->sem);
  if(sem->tNode){
    rt_release_port(sem->tNode, sem->tPort);
  }
  printf("SEM %s closed\n",sem->semName);
  free(sem);
}




