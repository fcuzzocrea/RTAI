/*
  COPYRIGHT (C) 2003  Roberto Bucher (roberto.bucher@supsi.ch)

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

#include <stdio.h>
#include <stdlib.h>
#include <libpcan.h>

#define DATA_SIZE 4

static HANDLE h  = NULL;
static int h_cnt = 0;

static BYTE data[DATA_SIZE][8]= {
                          {0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                          {0x00, 0x15, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00},
                          {0x00, 0x15, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},
                          {0x00, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}
};

int write_can(DWORD ID, BYTE DATA[])
{
    int errno=0;
    int i;

    TPCANMsg T_message;

    T_message.ID = ID;
    T_message.MSGTYPE = MSGTYPE_STANDARD;
    T_message.LEN = 8;
    for(i=0;i<8;i++) T_message.DATA[i] = DATA[i];

    if ((errno = CAN_Write(h, &T_message)))
    {
	perror("request: CAN_Write()");
	exit(1);
    }
    return errno;
}

int read_can(DWORD id, BYTE DATA[])
{ 
    TPCANMsg R_message;
    __u32 status;
    int errno=0;
    int i;

    DATA[0]=0x00;
    DATA[1]=0x14;
    DATA[2]=0x88;
    DATA[3]=0x00;
    DATA[4]=0x01;
    DATA[5]=0x00;
    DATA[6]=0x00;
    DATA[7]=0x00;
    write_can(id,DATA);

    if ((errno = CAN_Read(h, &R_message))){
	perror("request: CAN_Read()");
	exit(1);
    }
    else{
	for(i=0;i<8;i++) DATA[i]=R_message.DATA[i];
	if (R_message.MSGTYPE & MSGTYPE_STATUS){
	    status = CAN_Status(h);
	    if ((int)status < 0)
	    {
		errno = nGetLastError();
		perror("request: CAN_Status()");
		exit(1);
	    }
	    
	    else
		printf("request: pending CAN status 0x%04x read.\n", (__u16)status);
	}
    } 
    return errno;
}

void init_peak(int id, int Kp, int Ki)
{
    int i;

    int nType = HW_DONGLE_SJA_EPP;
    __u32 dwPort = 0x378;
    __u16 wIrq = 7;
    __u16 wBTR0BTR1 = CAN_BAUD_500K;
    int   nExtended = CAN_INIT_TYPE_ST;
  
    if(h==NULL){

	int errno=0;
	h_cnt=0;
 
	h = CAN_Open(nType, dwPort, wIrq);
	if (h){ 
	    CAN_Status(h);
	    if (wBTR0BTR1){
		errno = CAN_Init(h, wBTR0BTR1, nExtended);
		if (errno)
		{
		    perror("request: CAN_Init()");
		    exit(1);
		}
	    }
	}
	else
	{
	    errno = nGetLastError();
	    perror("request: CAN_Open()");
	    exit(1);
	}
    }
    h_cnt++;

    data[1][6]= (char) (Kp & 0xff);
    data[1][7]= (char) ((Kp >> 8) & 0x00ff);

    data[2][6]= (char) (Ki & 0xff);
    data[2][7]= (char) ((Ki >> 8) & 0x00ff);

    for(i=0;i<DATA_SIZE;i++) write_can(id,data[i]);
}

void write_peak(int id ,double u)
{
    int U_can;
    unsigned char DATA[8];

    U_can = (int) u;

    DATA[0]=0x00;
    DATA[1]=0x22;
    DATA[2]=(BYTE)U_can;
    DATA[3]=(BYTE)(U_can>>8);
    DATA[4]=0x00;
    DATA[5]=0x00;
    DATA[6]=0x00;
    DATA[7]=0x00;
    write_can(id,DATA);
}

double read_peak(int id)
{
    double y_can;
    unsigned char DATA[8];
    double pi = 3.14159265359;

    read_can(id,DATA);
    y_can = (DATA[3] << 24)  + (DATA[2] << 16) + (DATA[5] << 8) + DATA[4];
    return y_can/(500*4)*2*pi;
}

void end_peak(int id)
{
    BYTE DATA[8];

    DATA[0]=0x00;
    DATA[1]=0x22;
    DATA[2]=0x00;
    DATA[3]=0x00;
    DATA[4]=0x00;
    DATA[5]=0x00;
    DATA[6]=0x00;
    DATA[7]=0x00;
    write_can(id,DATA);

    DATA[0]=0x00;
    DATA[1]=0x05;
    DATA[2]=0x00;
    DATA[3]=0x00;
    DATA[4]=0x00;
    DATA[5]=0x00;
    DATA[6]=0x00;
    DATA[7]=0x00;
    write_can(id,DATA);

    h_cnt--;
    if(h_cnt==0) CAN_Close(h);
}
