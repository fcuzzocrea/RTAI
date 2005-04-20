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
#include "devstruct.h"
#include <asm/io.h>

#define RLD 0x00
#define CMR 0x20
#define IOR 0x40
#define IDR 0x60
#define PRST2FLTPSC 0x18

#define PI	3.14159265358979

#define RESET_DETECT 0x800000
#define INDEX_DETECT 0x000000

extern devStr inpDevStr[];
extern int pinp_cnt;

int inp_cioquad4_init(int modul ,char * Addr,int reso, int prec, int Rot, int Reset)
{
  int   mode;
  int bAdrG;
  int bAdr;
  int port=pinp_cnt++;

  strcpy(inpDevStr[port].IOName,"cioquad4");
  sscanf(Addr,"%x",& bAdrG);
  inpDevStr[port].dParam[0]=(double) reso;
  inpDevStr[port].dParam[1]=(double) prec;
  inpDevStr[port].dParam[2]=(double) Rot;
  inpDevStr[port].dParam[3]=(double) Reset;

  inpDevStr[port].i1=0;

  bAdr = bAdrG + (modul-1) * 2;
  inpDevStr[port].nch=bAdr;

  mode = (int) inpDevStr[port].dParam[1];

  switch(mode){
  case 1:
  case 2:
    outb(CMR | (mode << 3),bAdr + 0x01);
    break;
  case 4:
    outb(CMR | (3 << 3),bAdr + 0x01);
    break;
  }

  outb(RLD | 0x04,bAdr + 0x01);          // RESET BT, CT, CPT, S
  outb(RLD | 0x06,bAdr + 0x01);          // RESET E  

  outb(RLD | 0x01,bAdr + 0x01);          // RESET BP  
  outb(0x01,bAdr);                       // BOARD FREQ
  outb(PRST2FLTPSC,bAdr + 0x01);

  outb(RLD | 0x01,bAdr + 0x01);          // RESET BP  
  outb(INDEX_DETECT & 0x0000ff,bAdr);         // PRESET INDEX_DETECT 
  outb((INDEX_DETECT >> 8) & 0x0000ff,bAdr);  
  outb((INDEX_DETECT >> 16) & 0x0000ff,bAdr);

  outb(IOR | 0x00,bAdr + 0x01);   // DISABLE A/B 
  outb(IDR | 0x03,bAdr + 0x01);   // ENABLE INDEX POSITIVE

  // Now : Index is enabled, positive and 
  //       Load CNTR Input

  outb(0x0f,bAdrG + 0x08);         // Index TO LCNTR
  outb(0x00,bAdrG + 0x09);         // 4x 24 Bit Counter
  outb(0x00,bAdrG + 0x12);         // DISABLE INTERRUPT
  outb(RLD | 0x08,bAdr + 0x01);   // PRESET TO COUNTER
  outb(RLD | 0x10,bAdr + 0x01);   // COUNTER TO LATCH

  outb(RLD | 0x01,bAdr + 0x01);   // RESET BP

  outb(RESET_DETECT & 0x0000ff,bAdr);
  outb((RESET_DETECT >> 8) & 0x0000ff,bAdr);
  outb((RESET_DETECT >> 16) & 0x0000ff,bAdr);

  return(port);
}

void inp_cioquad4_input(int port, double * y, double t)
{
  int       enc_flags, cntrout;
  int       tmpout;
  int       tmpres = (int) inpDevStr[port].dParam[0];
  int       bAdr  = inpDevStr[port].nch;
  int       quad_mode = (int) inpDevStr[port].dParam[1];
  int       firstindex = inpDevStr[port].i1;
  double    rotation = inpDevStr[port].dParam[2];
  int       counter  = (int) inpDevStr[port].dParam[3];
 
  enc_flags = inb(bAdr + 0x01);  // READ FLAGS

  outb(RLD | 0x10,bAdr + 0x01);  // RESET FLAGS
  outb(RLD | 0x01,bAdr + 0x01);  // RESET BP

  cntrout = inb(bAdr) & 0x00ff;     // READ COUNTER
  cntrout = cntrout | ((inb(bAdr) & 0x00ff) * 0x100);
  cntrout = cntrout | ((inb(bAdr) & 0x00ff) * 0x10000);

  tmpout  = cntrout;

  if ((enc_flags & 0x03) != 0x00){
    firstindex=0;
    inpDevStr[port].i1=0;
    outb(IOR | 0x00,bAdr + 0x01); // DISABLE COUNTER
  }
  
  if (!firstindex) {
    //      if (enc_flags & 0x04){
    if(abs(tmpout-INDEX_DETECT)>(2*tmpres*quad_mode)){
      firstindex=1;
      inpDevStr[port].i1=1;
         
      if(counter == 2) outb(IOR | 0x01,bAdr + 0x01); // ENABLE AB
      // LOAD CNTR input
      else             outb(IOR | 0x03,bAdr + 0x01); // ENABLE AB
      // LOAD OL input
    }
    else { 
      y[0]  = 0.0;
    }
  }
  else{
    tmpout-=RESET_DETECT;
  
    y[0] = rotation* (tmpout)/((double)tmpres*quad_mode) * 2.0 * PI;
  }
}

void inp_cioquad4_update(void)
{
}

void inp_cioquad4_end(int port)
{
  printf("%s closed\n",inpDevStr[port].IOName);
}



