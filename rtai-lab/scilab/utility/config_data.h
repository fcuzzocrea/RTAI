
char * inputIO[][N_PARAMS]={
  {"sinus","-","-","-","Amplitude","Frequency","Phase","Bias","Delay"},
  {"square","-","-","-","Amplitude","Period","Pulse width","Bias","Delay"},
  {"step","-","-","-","Amplitude","Delay","-","-","-"},
  {"mem","Id","-","-","-","-","-","-","-"},
  {"rtai_comedi_data","Channel","Device","-","Range","Aref","-","-","-"},
  {"rtai_comedi_dio","Channel","Device","-","-","-","-","-","-"},
  {"extdata","Number of values","File name","-","-","-","-","-","-"},
  {"pcan","-","CAN id [hex]","-","Proportional gain","Integral gain","-","-","-"},
  {"cioquad4","Modul number","Base Addr [hex]","-","Resolution","Precision [1,2,4]",
   "Rotation [-1,+1]","Initial [0] or continous [1] reset","-"},
  {"mbx_receive_if","Number of signals","IP Addr","MBX Name","-","-","-","-","-"},
  {"mbx_receive","Number of signals","IP Addr","MBX Name","-","-","-","-","-"}
};

char * outputIO[][N_PARAMS]={
  {"rtai_scope","Number of signals","Scope name","-","-","-","-","-","-"},
  {"rtai_led","Number of leds","Led name","-","-","-","-","-","-"},
  {"rtai_meter","-","Meter name","-","-","-","-","-","-"},
  {"mem","Id","-","-","-","-","-","-","-"},
  {"rtai_comedi_data","Channel","Device","-","Range","Aref","-","-","-"},
  {"rtai_comedi_dio","Channel","Device","-","Treshold","-","-","-","-"},
  {"mbx_ovrwr_send","Number of signals","IP Addr","MBX name","-","-","-","-","-"},
  {"mbx_send_if","Number of signals","IP Addr","MBX name","-","-","-","-","-"},
  {"pcan","-","CAN id [hex]","-","Proportional gain","Integral gain","-","-","-"}
};

