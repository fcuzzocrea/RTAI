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

void inp_rtai_comedi_dio_init(int port,int nch,char * sName,char * sParam,
			      double p1,double p2, double p3, double p4, double p5)
{
    int id;
    void *dev;
    int subdev;
    int len, index;

    unsigned int channel;
    int n_channels;
    char board[50];

    id=port-1;
    inpDevStr[id].nch=nch;
    sprintf(inpDevStr[id].sName,"/dev/%s",sName);
    strcpy(inpDevStr[id].IOName,"Comedi DIO input");
    inpDevStr[id].dParam[0]=p1;
    inpDevStr[id].dParam[1]=p2;
    inpDevStr[id].dParam[2]=p3;
    inpDevStr[id].dParam[3]=p4;
    inpDevStr[id].dParam[4]=p5;

    channel = nch;
    len=strlen(inpDevStr[id].sName);
    index = inpDevStr[id].sName[len-1]-'0';

    if (!ComediDev[index]) {
	dev = comedi_open(inpDevStr[id].sName);
	if (!dev) {
	    fprintf(stderr, "Comedi open failed\n");
	    exit_on_error();
	}
	rt_comedi_get_board_name(dev, board);
	printf("COMEDI %s (%s) opened.\n\n", inpDevStr[id].sName, board);
	ComediDev[index] = dev;
	if ((subdev = comedi_find_subdevice_by_type(dev, COMEDI_SUBD_DIO, 0)) < 0) {
	    fprintf(stderr, "Comedi find_subdevice failed (No digital I/O)\n");
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
	subdev = comedi_find_subdevice_by_type(dev, COMEDI_SUBD_DIO, 0);
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
    if ((comedi_dio_config(dev, subdev, channel, COMEDI_INPUT)) < 0) {
	fprintf(stderr, "Comedi DIO config failed for subdevice %d\n", subdev);
	comedi_unlock(dev, subdev);
	comedi_close(dev);
	exit_on_error();
    }
    ComediDev_InUse[index]++;
    ComediDev_DIOInUse[index]++;
    comedi_dio_write(dev, subdev, channel, 0);

    inpDevStr[id].ptr1 = (void *)dev;
    inpDevStr[id].dParam[2]  = (double) subdev;
}

void out_rtai_comedi_dio_init(int port,int nch,char * sName,char * sParam,
			      double p1,double p2, double p3, double p4, double p5)
{
    int id;
    void *dev;
    int subdev;

    unsigned int channel;
    int n_channels;
    char board[50];
    int len, index;

    id=port-1;
    outDevStr[id].nch=nch;
    strcpy(outDevStr[id].IOName,"Comedi DIO output");
    sprintf(outDevStr[id].sName,"/dev/%s",sName);
    outDevStr[id].dParam[0]=p1;
    outDevStr[id].dParam[1]=p2;
    outDevStr[id].dParam[2]=p3;
    outDevStr[id].dParam[3]=p4;
    outDevStr[id].dParam[4]=p5;

    len=strlen(outDevStr[id].sName);
    index = outDevStr[id].sName[len-1]-'0';
    channel = nch;
    if (!ComediDev[index]) {
	dev = comedi_open(outDevStr[id].sName);
	if (!dev) {
	    fprintf(stderr, "Comedi open failed\n");
	    exit_on_error();
	}
	rt_comedi_get_board_name(dev, board);
	printf("COMEDI %s (%s) opened.\n\n", outDevStr[id].sName, board);
	ComediDev[index] = dev;
	if ((subdev = comedi_find_subdevice_by_type(dev, COMEDI_SUBD_DIO, 0)) < 0) {
	    fprintf(stderr, "Comedi find_subdevice failed (No digital I/O)\n");
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
	subdev = comedi_find_subdevice_by_type(dev, COMEDI_SUBD_DIO, 0);
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
    if ((comedi_dio_config(dev, subdev, channel, COMEDI_OUTPUT)) < 0) {
	fprintf(stderr, "Comedi DIO config failed for subdevice %d\n", subdev);
	comedi_unlock(dev, subdev);
	comedi_close(dev);
	exit_on_error();
    }
    ComediDev_InUse[index]++;
    ComediDev_DIOInUse[index]++;
    comedi_dio_write(dev, subdev, channel, 0);

    outDevStr[id].ptr1 = (void *)dev;
    outDevStr[id].dParam[2]  = (double) subdev;
}

void out_rtai_comedi_dio_output(int port, double * u,double t)
{ 
    int id=port-1;
    unsigned int channel = (unsigned int) outDevStr[id].nch;
    void *dev        = (void *) outDevStr[id].ptr1;
    int subdev       = (int) outDevStr[id].dParam[2];
    unsigned int bit = 0;
    double threshold = outDevStr[id].dParam[0];

    if (*u >= threshold) {
	bit=1;
    }
    comedi_dio_write(dev, subdev, channel, bit);
}

void inp_rtai_comedi_dio_input(int port, double * y, double t)
{
    int id=port-1;
    unsigned int channel = (unsigned int) inpDevStr[id].nch;
    void *dev        = (void *) inpDevStr[id].ptr1;
    int subdev       = (int) inpDevStr[id].dParam[2];
    unsigned int bit;

    comedi_dio_read(dev, subdev, channel, &bit);
    *y = (double)bit;
}

void inp_rtai_comedi_dio_update(void)
{
}

void out_rtai_comedi_dio_end(int port)
{
    int id=port-1;
    unsigned int channel = (unsigned int) outDevStr[id].nch;
    void *dev        = (void *) outDevStr[id].ptr1;
    int subdev       = (int) outDevStr[id].dParam[2];
    int len, index;

    len=strlen(outDevStr[id].sName);
    index = outDevStr[id].sName[len-1]-'0';

    comedi_dio_write(dev, subdev, channel, 0);
    ComediDev_InUse[index]--;
    ComediDev_DIOInUse[index]--;
    if (!ComediDev_DIOInUse[index]) {
	comedi_unlock(dev, subdev);
    }
    if (!ComediDev_InUse[index]) {
	comedi_close(dev);
	printf("\nCOMEDI %s closed.\n\n", outDevStr[id].sName);
	ComediDev[index] = NULL;
    }
}

void inp_rtai_comedi_dio_end(int port)
{
    int id=port-1;
    void *dev        = (void *) inpDevStr[id].ptr1;
    int subdev       = (int) inpDevStr[id].dParam[2];
    int len, index;

    len=strlen(inpDevStr[id].sName);
    index = inpDevStr[id].sName[len-1]-'0';

    ComediDev_InUse[index]--;
    ComediDev_DIOInUse[index]--;
    if (!ComediDev_DIOInUse[index]) {
	comedi_unlock(dev, subdev);
    }
    if (!ComediDev_InUse[index]) {
	comedi_close(dev);
	printf("\nCOMEDI %s closed.\n\n", inpDevStr[id].sName);
	ComediDev[index] = NULL;
    }
}




