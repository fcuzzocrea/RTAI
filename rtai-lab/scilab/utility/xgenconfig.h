#define XGENCONFIG_VERSION   "3.0.0a"
#define SIZE_X 600
#define SIZE_Y 400
#define N_PARAMS 9

struct Device_Struct
{
  int nType;
  Fl_String sType;
  Fl_String dir;
  Fl_String p[N_PARAMS-1];
};

typedef struct Device_Struct Device;

void init_device_inp(int i);
void gen_inputs(Fl_Window * w);
void init_device_out(int i);
void gen_outputs(Fl_Window * w);
