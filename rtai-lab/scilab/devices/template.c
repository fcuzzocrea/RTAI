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
#include <string.h>

struct str_xxx{

};

void * inp_xxx_init()
{
  struct scope * xxx = (struct str_xxx *) malloc(sizeof(struct str_xxx));

  return((void *) xxx);
}

void inp_xxx_input(void * ptr, double * y, double t)
{
  struct scope * xxx = (struct str_xxx *) ptr;
  /*     *y=XXXX; */
}

void inp_xxx_update(void)
{
}

void inp_xxx_end(void * ptr)
{
  struct scope * xxx = (struct str_xxx *) ptr;
  printf(" closed\n");
  free(xxx);
}

void * out_xxx_init()
{
  struct scope * xxx = (struct str_xxx *) malloc(sizeof(struct str_xxx));

  return((void *) xxx);
}

void out_xxx_output(void * ptr, double * u,double t)
{ 
  struct scope * xxx = (struct str_xxx *) ptr;
  /*     XXXX=*u; */
}

void out_xxx_end(void * ptr)
{
  struct scope * xxx = (struct str_xxx *) ptr;
  printf(" closed\n");
  free(xxx);
}




