#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <getopt.h>

#include <efltk/Fl.h>
#include <efltk/Fl_Main_Window.h>
#include <efltk/Fl_Input.h>
#include <efltk/Fl_Menu_Bar.h>
#include <efltk/Fl_Choice.h>
#include <efltk/Fl_Tabs.h>
#include <efltk/fl_ask.h>
#include <efltk/Fl_Dialog.h>
#include <efltk/Fl_String.h>

#include "def_const.h"
#include "config_data.h"
#include "xgenconfig.h"

int n_inp = 1;
int n_out = 1;
static int Verbose = 0;

Device *inpDev;
Device *outDev;

Fl_Tabs ** IO_Tabs;
Fl_Menu_Bar *Main_Menu;
Fl_Main_Window * Main_Window;
Fl_String Conf_Name = "config";

void quit_cb(Fl_Widget*, void*);
void create_cb(Fl_Widget*, void*);

Fl_Menu_Item Main_Menu_Table[] = {
	{" &File", FL_ALT+'f', 0, 0, FL_SUBMENU},
		{" Create config ", FL_ALT+'c', create_cb,  0},
		{" Quit ",          FL_ALT+'q', quit_cb,    0, 0},
		{0},
	{0}
};

void quit_cb(Fl_Widget*, void*)
{
  if (!fl_ask("Are you sure you want to stop the program?")) {
    return;
  }
  Main_Window->hide();
}

void create_cb(Fl_Widget*, void*)
{
  Fl_String str;
  int i,j;
  FILE * fp;

  fp = fopen(Conf_Name,"w");
  for(i=0;i<n_inp;i++){
    str = inpDev[i].sType + " " + inpDev[i].dir + " " + Fl_String(i+1);
    for(j=0;j<N_PARAMS-1;j++) str = str + " " + inpDev[i].p[j];
    fprintf(fp,"%s\n",str.c_str());
  }

  for(i=0;i<n_out;i++){
    str = outDev[i].sType + " " + outDev[i].dir + " " + Fl_String(i+1);
    for(j=0;j<N_PARAMS-1;j++) str = str + " " + outDev[i].p[j];
    fprintf(fp,"%s\n",str.c_str());
  }
  fprintf(fp,"end\n");
  fclose(fp);
}

void fill_inp(Device io_dev,int port)
{
  int i;

  if(port < n_inp) {
    inpDev[port].sType = io_dev.sType;
    inpDev[port].dir = io_dev.dir;
    for(i=0;i<N_PARAMS-1;i++) inpDev[port].p[i] = io_dev.p[i];
    for(i=0;i<N_INPUT;i++) 
      if(strcmp(inpDev[port].sType.c_str(),inputIO[i][0]) == 0) {
	inpDev[port].nType = i;
	break;
      }
  }
}

void fill_out(Device io_dev,int port)
{
  int i;

  if(port < n_out) {
    outDev[port].sType = io_dev.sType;
    outDev[port].dir = io_dev.dir;
    for(i=0;i<N_PARAMS-1;i++) outDev[port].p[i] = io_dev.p[i];
    for(i=0;i<N_OUTPUT;i++) 
      if(strcmp(outDev[port].sType.c_str(),outputIO[i][0]) == 0) {
	outDev[port].nType = i;
	break;
      }
  }
}

void get_old_data()
{
  char str[30];
  int i;
  int port;
  FILE * fp;

  Device io_dev;

  if(!(fp = fopen(Conf_Name,"r"))) return;

  while(!feof(fp)) {
    fscanf(fp,"%s",str);
    io_dev.sType = str;
    if(strcmp(io_dev.sType.c_str(),"end") == 0) break;
    fscanf(fp,"%s",str);
    io_dev.dir = str;
    fscanf(fp,"%d",&port);
    for(i=0;i<N_PARAMS-1;i++){
      fscanf(fp,"%s",str);
      io_dev.p[i] = str;
    }
    if(strcmp(io_dev.dir.c_str(),"inp") == 0) fill_inp(io_dev,port-1);
    else                                      fill_out(io_dev,port-1);       
  }
  fclose(fp);
}

void init_devices()
{
  int i;

  inpDev = new Device[n_inp];
  outDev = new Device[n_out];

  for(i=0;i<n_inp;i++) init_device_inp(i);
  for(i=0;i<n_out;i++) init_device_out(i);

}

struct option options[] = {
	{ "help",        0, 0, 'h' },
	{ "verbose",     0, 0, 'v' },
	{ "version",     0, 0, 'V' },
	{ "input",       1, 0, 'i' },
	{ "output" ,     1, 0, 'o' },
	{ "configfile" , 1, 0, 'f' }
};

void print_usage(void)
{
  fputs(
	("\nUsage:  xrtailab [OPTIONS]\n"
	 "\n"
	 "OPTIONS:\n"
	 "  -h, --help\n"
	 "      print usage\n"
	 "  -v, --verbose\n"
	 "      verbose output\n"
	 "  -V, --version\n"
	 "      print xgenconfig version\n"
	 "  -i num, --input num\n"
	 "      number of block input signals\n"
	 "  -o num, --output num\n"
	 "      number of block output signals\n"
	 "  -f num, --configfile filename\n"
	 "      name of the config file\n"
	 "\n")
	,stderr);
  exit(0);
}

int main(int argc, char **argv)
{
  char *lang_env;
  int c, option_index = 0;

  while (1) {
    c = getopt_long(argc, argv, "hvVi:o:f:", options, &option_index);
    if (c == -1)
      break;
    switch (c) {
    case 'v':
      Verbose = 1;
      break;
    case 'V':
      fputs("XGENCONFIG version " XGENCONFIG_VERSION "\n", stderr);
      exit(0);
    case 'i':
      n_inp = atoi(optarg);
      break;
    case 'o':
      n_out = atoi(optarg);
      break;
    case 'f':
      Conf_Name = strdup(optarg);
      break;
    case 'h':
      print_usage();
      exit(0);
    default:
      break;
    }
  }

  lang_env = getenv("LANG");
  setenv("LANG", "en_US", 1);

  Main_Window = new Fl_Main_Window(20,30,SIZE_X,SIZE_Y+40,"XGenConfig");

  Main_Window->begin();

  Main_Menu = Main_Window->menu();
  Main_Menu->box(FL_THIN_UP_BOX);
  Main_Menu->menu(Main_Menu_Table);

  init_devices();
  get_old_data();

  IO_Tabs = new Fl_Tabs*[2];
  gen_inputs(Main_Window);
  gen_outputs(Main_Window);

  Main_Window->end();
  Main_Window->show();

  return Fl::run();
}
