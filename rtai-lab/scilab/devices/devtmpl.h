void * inp_xxx_init();
void inp_xxx_input(void * ptr, double * y, double t);
void inp_xxx_update(void);
void inp_xxx_end(void * ptr);

void * out_xxx_init();
void out_xxx_output(void * ptr, double * u,double t);
void out_xxx_end(void * ptr);
