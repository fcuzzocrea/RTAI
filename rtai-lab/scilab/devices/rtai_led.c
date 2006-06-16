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

#include <stdio.h>
#include <stdlib.h>
#include <rtai_netrpc.h>
#include <rtai_msg.h>
#include <rtai_mbx.h>
#include <string.h>
#include "rtmain.h"

#define MAX_RTAI_LEDS               1000
#define MBX_RTAI_LED_SIZE           5000

struct Led{
  int nch;
  char ledName[20];
  MBX * mbx;
};

extern char *TargetLedMbxID;

void * out_rtai_led_init(int nch,char * sName)
{
  struct Led * led;

  led=(struct Led *) malloc(sizeof(struct Led));

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
  struct Led * led = (struct Led *) ptr;
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
  struct Led * led = (struct Led *) ptr;
  RT_named_mbx_delete(0, 0, led->mbx);
  printf("Led %s closed\n",led->ledName);
  free(led);
}



