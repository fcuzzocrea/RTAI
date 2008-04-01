mode(-1);
// specific part
libname='rtai' // name of scilab function library [CUSTOM]

//** It is a better function to recover the absolute path information 
DIR = get_absolute_file_path('builder.sce')

if ~MSDOS then // Unix Linux
  if part(DIR,1)<>'/' then DIR=getcwd()+'/'+DIR,end
  MACROS=DIR+'macros/' // Path of the macros directory
  ROUTINES = DIR+'routines/' 
else  // windows- Visual C++
  if part(DIR,2)<>':' then DIR=getcwd()+'\'+DIR,end
  MACROS=DIR+'macros\' // Path of the macros directory
  ROUTINES = DIR+'routines\' 
end

//compile sci files if necessary and build lib file
genlib(libname,MACROS)

cd(ROUTINES)

names=['rtsinus';
       'rtsquare';
       'rt_step';
       'exit_on_error';
       'rt_delta_direct';
       'rt_delta_inverse';
       'par_getstr']
files=['rtai_sinus.o';
       'rtai_square.o';
       'rtai_step.o';
       'exit_on_error.o';
       'delta_direct.o'
       'delta_inverse.o'
       'getstr.o']

libn=ilib_for_link(names,files,[],"c","Makelib","loader.sce","rtinp","","-I.")

quit
