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

void out_rtai_scope_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5);
void out_rtai_scope_output(int port, double * u,double t);
void out_rtai_scope_end(int port);

void inp_rtai_comedi_data_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5);
void inp_rtai_comedi_data_input(int port, double * y, double t);
void inp_rtai_comedi_data_update(void);
void inp_rtai_comedi_data_end(int port);

void out_rtai_comedi_data_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5);
void out_rtai_comedi_data_output(int port, double * u,double t);
void out_rtai_comedi_data_end(int port);

void inp_rtai_comedi_dio_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5);
void inp_rtai_comedi_dio_input(int port, double * y, double t);
void inp_rtai_comedi_dio_update(void);
void inp_rtai_comedi_dio_end(int port);

void out_rtai_comedi_dio_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5);
void out_rtai_comedi_dio_output(int port, double * u,double t);
void out_rtai_comedi_dio_end(int port);

void out_rtai_led_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5);
void out_rtai_led_output(int port, double * u,double t);
void out_rtai_led_end(int port);

void out_rtai_meter_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5);
void out_rtai_meter_output(int port, double * u,double t);
void out_rtai_meter_end(int port);

void inp_extdata_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5);
void inp_extdata_input(int port, double * y, double t);
void inp_extdata_update(void);
void inp_extdata_end(int port);

void inp_cioquad4_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5);
void inp_cioquad4_input(int port, double * y, double t);
void inp_cioquad4_update(void);
void inp_cioquad4_end(int port);

void inp_pcan_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5);
void inp_pcan_input(int port, double * y, double t);
void inp_pcan_update();
void inp_pcan_end(int port);

void out_pcan_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5);
void out_pcan_output(int port, double * u,double t);
void out_pcan_end(int port);

void out_mbx_ovrwr_send_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5);
void out_mbx_ovrwr_send_output(int port, double * u,double t);
void out_mbx_ovrwr_send_end(int port);

void inp_mbx_receive_if_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5);
void inp_mbx_receive_if_input(int port, double * y, double t);
void inp_mbx_receive_if_update(void);
void inp_mbx_receive_if_end(int port);

void inp_mbx_receive_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5);
void inp_mbx_receive_input(int port, double * y, double t);
void inp_mbx_receive_update(void);
void inp_mbx_receive_end(int port);

void out_mbx_send_if_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5);
void out_mbx_send_if_output(int port, double * u,double t);
void out_mbx_send_if_end(int port);
void inp_rtai_fifo_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5);
void inp_rtai_fifo_input(int port, double * y, double t);
void inp_rtai_fifo_update(void);
void inp_rtai_fifo_end(int port);

void out_rtai_fifo_init(int port,int nch,char * sName,char * sParam,double p1,
                  double p2, double p3, double p4, double p5);
void out_rtai_fifo_output(int port, double * u,double t);
void out_rtai_fifo_end(int port);
