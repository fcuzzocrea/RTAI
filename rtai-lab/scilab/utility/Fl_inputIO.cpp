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

extern int n_inp;
extern Device * inpDev;
extern Fl_Tabs ** IO_Tabs;
extern char * inputIO[][N_PARAMS];

static Fl_Input * P[N_PARAMS-1];

static Fl_Dialog *dlg;

void create_inp_dlg(int type,int port)
{
  int i=0;
  int j;
  dlg = new Fl_Dialog(300,300,"Parameters");
  dlg->new_group("default");

  for(j=0;j<N_PARAMS-1;j++) {
    if(inputIO[type][j+1][0]!='-') {
      P[j] = new Fl_Input(20,30+25*i,150,20,inputIO[type][j+1]);
      P[j]->align(FL_ALIGN_RIGHT);
      P[j]->value(inpDev[port].p[j]);
      i++;
    }
  }
  dlg->end();

  dlg->buttons(FL_DLG_OK|FL_DLG_CANCEL,FL_DLG_OK);
}

void open_inp_dialog(int nType, int nPort)
{
  Fl_Dialog& dialog = *dlg;
  int i;

  switch (dialog.show_modal()) {
  case FL_DLG_OK:
    inpDev[nPort].nType = nType;
    inpDev[nPort].sType = inputIO[nType][0];
    inpDev[nPort].dir = "inp";
    for(i=0;i<N_PARAMS-1;i++){
      if(inputIO[nType][i+1][0]!='-') inpDev[nPort].p[i] = P[i]->value();
    }
    break;
  case FL_DLG_CANCEL:
    break;
  }
}

void inp_choice_cb(Fl_Choice* c,long w) 
{
  int io_type = c->value();
  int io_num = (int) w;

  create_inp_dlg(io_type,io_num);
  open_inp_dialog(io_type,io_num);
  delete(dlg);
}

void init_device_inp(int i)
{
  inpDev[i].nType = 0;
  inpDev[i].sType = inputIO[0][0];
  inpDev[i].dir = "inp";
  inpDev[i].p[0] = "1";
  inpDev[i].p[1] = "0";
  inpDev[i].p[2] = "0";
  inpDev[i].p[3] = "1";
  inpDev[i].p[4] = "1";
  inpDev[i].p[5] = "0";
  inpDev[i].p[6] = "0";
  inpDev[i].p[7] = "0";
}

void gen_inputs(Fl_Window * w)
{
  int i,j;
  Fl_Choice * c;

  { Fl_Tabs *o = IO_Tabs[0] = new Fl_Tabs(0, 30, (SIZE_X/2)-3, SIZE_Y);
  o->new_page("Inputs");

  for(i=0;i<n_inp;i++){
    c = new Fl_Choice(20,30+25*i,150,20);
    for(j=0;j<N_INPUT;j++){
      c->add(inputIO[j][0]);
      c->callback((Fl_Callback*)inp_choice_cb);
      c->user_data((void*)i);
      c->value(inpDev[i].nType);
    }
  }
  o->end();
  }
}
