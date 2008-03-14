function [x,y,typ]=rtai_comedi_dataout(job,arg1,arg2)
//
// Copyright roberto.bucher@supsi.ch
x=[];y=[];typ=[];
select job
case 'plot' then
  graphics=arg1.graphics; exprs=graphics.exprs;
  ch=exprs(1)(1);name=exprs(1)(2);
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
    [ok,ch,name,com_range,aref,lab]=..
        getvalue('Set RTAI-COMEDI DATA block parameters',..
        ['Channel';
        'Device';
	'Range';
        'Aref'],..
         list('vec',1,'str',1,'vec',1,'vec',1'),label(1))

    if ~ok then break,end
    label(1)=lab
    funam='o_comedi_data_' + name + '_' + string(ch);
    xx=[];ng=[];z=0;
    nx=0;nz=0;
    o=[];
    i=1;nin=1;
    ci=1;nevin=1;
    co=[];nevout=0;
    funtyp=2004;
    depu=%t;
    dept=%f;
    dep_ut=[depu dept];

    [ok,tt]=getCode_comedi_dataout(funam)
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
  ch=0;com_range=0;aref=0; 
  name='comedi0'

  model=scicos_model()
  model.sim=list(' ',2004)
  model.in=1
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

  label=list([sci2exp(ch),name,sci2exp(com_range),sci2exp(aref)],[])

  gr_i=['xstringb(orig(1),orig(2),[''COMEDI D/A'';name+'' CH-''+string(ch)],sz(1),sz(2),''fill'');']
  x=standard_define([3 2],model,label,gr_i)
end
endfunction

function [ok,tt]=getCode_comedi_dataout(funam)
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
  textmp($+1)='    blk=out_rtai_comedi_data_init('+ string(ch)+',""' + name + '"",'+string(com_range)+','+string(aref)+');'
  textmp($+1)='    break;'; 
  textmp($+1)='  case 2:'
  textmp($+1)='    u[0]=block->inptr[0][0];'
  textmp($+1)='    out_rtai_comedi_data_output(blk,u,t);'
  textmp($+1)='    break;'
  textmp($+1)='  case 5:'
  textmp($+1)='    out_rtai_comedi_data_end(blk);'
  textmp($+1)='    break;'
  textmp($+1)='  }'
  textmp($+1)='#endif'
  textmp($+1)='}'
  tt=textmp;

  ok = %t;

endfunction
