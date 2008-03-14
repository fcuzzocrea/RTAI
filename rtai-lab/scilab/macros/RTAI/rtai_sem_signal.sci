function [x,y,typ]=rtai_sem_signal(job,arg1,arg2)
//
// Copyright roberto.bucher@supsi.ch
x=[];y=[];typ=[];
select job
case 'plot' then
  graphics=arg1.graphics; exprs=graphics.exprs;
  name=exprs(1)(1);
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
    [ok,name,ipaddr,lab]=..
        getvalue('Set RTAI sem_signal block parameters',..
        ['Semaphore name'
	'IP addr'],..
         list('str',1,'str',1),label(1))

    if ~ok then break,end
    label(1)=lab
    funam='o_sem_' + name;
    xx=[];ng=[];z=0;
    nx=0;nz=0;
    o=[];
    i=[1];
    i=int(i(:));nin=size(i,1);
    ci=1;nevin=1;
    co=[];nevout=0;
    funtyp=2004;
    depu=%t;
    dept=%f;
    dep_ut=[depu dept];

    [ok,tt]=getCode_sem_signal(funam)
    if ~ok then break,end
    [model,graphics,ok]=check_io(model,graphics,i,o,ci,co)
    if ok then
      model.sim=list(funam,funtyp)
      model.in=i
      model.out=[]
      model.evtin=ci
      model.evtout=[]
      model.state=[]
      model.dstate=[]
      model.rpar=[]
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
  in=1
  insz = 1
  name = 'SEM'
  ipaddr = '127.0.0.1'

  model=scicos_model()
  model.sim=list(' ',2004)
  model.in=insz
  model.out=[]
  model.evtin=1
  model.evtout=[]
  model.state=[]
  model.dstate=[]
  model.rpar=[]
  model.ipar=[]
  model.blocktype='d'
  model.firing=[]
  model.dep_ut=[%t %f]
  model.nzcross=0

  label=list([name,ipaddr],[])

  gr_i=['xstringb(orig(1),orig(2),[''SEM signal'';name],sz(1),sz(2),''fill'');']
  x=standard_define([3 2],model,label,gr_i)

end
endfunction

function [ok,tt]=getCode_sem_signal(funam)
   textmp=[
          '#ifndef MODEL'
          '#include <math.h>';
          '#include <stdlib.h>';
          '#include <scicos/scicos_block.h>';
          '#endif'
          '';
          'void '+funam+'(scicos_block *block,int flag)';
         ];
  textmp($+1)='{'
  textmp($+1)='#ifdef MODEL'
  textmp($+1)='static void * blk;'
  textmp($+1)='double u[1];'
  textmp($+1)='double t = get_scicos_time();' 
  textmp($+1)='  switch(flag) {'
  textmp($+1)='  case 4:'
  textmp($+1)='    blk=out_rtai_sem_init(""' + name + '"",""'+ipaddr+'"");'
  textmp($+1)='    break;'; 
  textmp($+1)='  case 2:'
  textmp($+1)='    u[0]=block->inptr[0][0];'
  textmp($+1)='    out_rtai_sem_output(blk,u,t);'
  textmp($+1)='    break;'
  textmp($+1)='  case 5:'
  textmp($+1)='    out_rtai_sem_end(blk);'
  textmp($+1)='    break;'
  textmp($+1)='  }'
  textmp($+1)='#endif'
  textmp($+1)='}'
  tt=textmp;

  ok = %t;

endfunction
