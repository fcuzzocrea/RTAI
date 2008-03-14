function [x,y,typ]=rtai_sinus(job,arg1,arg2)
//
// Copyright roberto.bucher@supsi.ch
x=[];y=[];typ=[];
select job
case 'plot' then
  standard_draw(arg1)
case 'getinputs' then
  [x,y,typ]=standard_inputs(arg1)
case 'getoutputs' then
  [x,y,typ]=standard_outputs(arg1)
case 'getorigin' then
  [x,y]=standard_origin(arg1)
case 'set' then
  x=arg1
  model=arg1.model;graphics=arg1.graphics;
  label=graphics.exprs;
  while %t do
    [ok,A,frq,phase,bias,delay,lab]=..
        getvalue('Set RTAI-sinus block parameters',..
        ['Amplitude';
	'Frequency';
        'Phase';
	'Bias';
	'Delay'],..
         list('vec',1,'vec',1,'vec',1,'vec',1','vec',1),label(1))

    if ~ok then break,end
    label(1)=lab
    rt_par=[A,frq,phase,bias,delay];
    rpar = rt_par(:);
    xx=[];ng=[];z=0;
    nx=0;nz=0;
    i=[];
    o=1;nout=1;
    ci=1;nevin=1;
    co=[];nevout=0;
    funtyp=2004;
    depu=%t;
    dept=%f;
    dep_ut=[depu dept];

    funam='i_sinus_' + string(%kk);

    [ok,tt]=getCode_sinus(funam)
    if ~ok then break,end
    [model,graphics,ok]=check_io(model,graphics,i,o,ci,co)
    if ok then
      model.sim=list(funam,funtyp)
      model.in=[]
      model.out=o
      model.evtin=ci
      model.evtout=[]
      model.state=[]
      model.dstate=[]
      model.rpar=rpar
      model.ipar=[]
      model.firing=[]
      model.dep_ut=dep_ut
      model.nzcross=0
      label(2)=tt
      x.model=model
      graphics.exprs=label
      x.graphics=graphics
      break
    end
  end
case 'define' then
  A=1;frq=1;phase=0;bias=0;delay=0;
  rt_par=[A,frq,phase,bias,delay];
  rpar=rt_par(:);
  model=scicos_model()
  model.sim=list(' ',2004)
  model.in=[]
  model.out=1
  model.evtin=1
  model.evtout=[]
  model.state=[]
  model.dstate=[]
  model.rpar=rpar
  model.ipar=[]
  model.blocktype='c'
  model.firing=[]
  model.dep_ut=[%t %f]
  model.nzcross=0

  label=list([sci2exp(A), sci2exp(frq), sci2exp(phase), sci2exp(bias), sci2exp(delay)],[])

  gr_i=['xstringb(orig(1),orig(2),''Sine'',sz(1),sz(2),''fill'');']
  x=standard_define([3 2],model,label,gr_i)

end
endfunction

function [ok,tt]=getCode_sinus(funam)
  textmp=[
	  '#ifndef MODEL'
	  '#include <math.h>';
	  '#include <stdlib.h>';
	  '#include <scicos/scicos_block.h>';
	  '#endif';
	  '';
	  'void '+funam+'(scicos_block *block,int flag)';
	 ];
  textmp($+1)='{'
  textmp($+1)='  double w,pi=3.1415927;'
  textmp($+1)='  double t = get_scicos_time();'
  textmp($+1)='  switch(flag) {'
  textmp($+1)='  case 4:'
  textmp($+1)='   block->outptr[0][0]=0.0;'
  textmp($+1)='   break;';
  textmp($+1)='  case 1:'
  textmp($+1)='   if (t<block->rpar[4]) block->outptr[0][0]=0.0;'
  textmp($+1)='   else {'
  textmp($+1)='     w=2*pi*block->rpar[1]*(t-block->rpar[4])-block->rpar[2];'
  textmp($+1)='     block->outptr[0][0]=block->rpar[0]*sin(w)+block->rpar[3];'
  textmp($+1)='   }'
  textmp($+1)='   break;';
  textmp($+1)='  case 5: '
  textmp($+1)='   block->outptr[0][0]=0.0;'
  textmp($+1)='   break;';
  textmp($+1)='  }'
  textmp($+1)='}'
  textmp=[textmp;' '];

  tt=textmp;
  ok = %t;
endfunction
