#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <efltk/Fl.h>
#include <efltk/Fl_Main_Window.h>
#include <efltk/Fl_Input.h>
#include <efltk/Fl_Menu_Bar.h>
#include <efltk/Fl_Choice.h>
#include <efltk/Fl_Tabs.h>
#include <efltk/Fl_Dialog.h>

#include "def_const.h"
#include "xgenconfig.h"

extern int n_out;
extern Device * outDev;
extern Fl_Tabs ** IO_Tabs;
extern char * outputIO[][N_PARAMS];

static Fl_Input * P[N_PARAMS-1];

static Fl_Dialog *dlg;

void create_out_dlg(int type,int port)
{
  int i=0;
  int j;
  dlg = new Fl_Dialog(300,300,"Parameters");
  dlg->new_group("default");

  for(j=0;j<N_PARAMS-1;j++) {
    if(outputIO[type][j+1][0]!='-') {
      P[j] = new Fl_Input(20,30+25*i,150,20,outputIO[type][j+1]);
      P[j]->align(FL_ALIGN_RIGHT);
      P[j]->value(outDev[port].p[j]);
      i++;
    }
  }
  dlg->end();
  dlg->buttons(FL_DLG_OK|FL_DLG_CANCEL,FL_DLG_OK);
}

void open_out_dialog(int nType, int nPort)
{
  Fl_Dialog& dialog = *dlg;
  int i;

  switch (dialog.show_modal()) {
  case FL_DLG_OK:
    outDev[nPort].nType = nType;
    outDev[nPort].sType = outputIO[nType][0];
    outDev[nPort].dir = "out";
    for(i=0;i<N_PARAMS-1;i++){
      if(outputIO[nType][i+1][0]!='-') outDev[nPort].p[i] = P[i]->value();
    }
    break;
  case FL_DLG_CANCEL:
    break;
  }
}

void out_choice_cb(Fl_Choice* c,long w) 
{
  int io_type = c->value();
  int io_num = (int) w;

  create_out_dlg(io_type,io_num);
  open_out_dialog(io_type,io_num);
  delete(dlg);
}

void init_device_out(int i)
{
  outDev[i].nType = 0;
  outDev[i].sType = outputIO[0][0];
  outDev[i].dir = "out";
  outDev[i].p[0] = "1";
  outDev[i].p[1] = "SCOPE";
  outDev[i].p[2] = "0";
  outDev[i].p[3] = "0";
  outDev[i].p[4] = "0";
  outDev[i].p[5] = "0";
  outDev[i].p[6] = "0";
  outDev[i].p[7] = "0";
}

void gen_outputs(Fl_Window * w)
{
  int i,j;
  Fl_Choice * c;

  { Fl_Tabs *o = IO_Tabs[1] = new Fl_Tabs((SIZE_X/2) +3, 30, (SIZE_X/2)-3, SIZE_Y);
  o->new_page("Outputs");

  for(i=0;i<n_out;i++){
    c = new Fl_Choice(20,30+25*i,150,20);
    for(j=0;j<N_OUTPUT;j++){
      c->add(outputIO[j][0]);
      c->callback((Fl_Callback*)out_choice_cb);
      c->user_data((void*)i);
      c->value(outDev[i].nType);
    }
  }
  o->end();
  }
}
