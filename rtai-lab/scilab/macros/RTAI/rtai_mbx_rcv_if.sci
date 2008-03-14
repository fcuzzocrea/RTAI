function [x,y,typ]=rtai_mbx_rcv_if(job,arg1,arg2)
//
// Copyright roberto.bucher@supsi.ch
x=[];y=[];typ=[];
select job
case 'plot' then
  graphics=arg1.graphics; exprs=graphics.exprs;
  name=exprs(1)(2);
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
    [ok,op,name,ipaddr,lab]=..
        getvalue('Set RTAI-mbx_rcv_if block parameters',..
        ['output ports';
	'MBX Name';
	'IP Addr'],..
         list('vec',-1,'str',1,'str',1),label(1))

    if ~ok then break,end
    label(1)=lab
    funam='i_mbx_rcvif_' + name;
    xx=[];ng=[];z=0;
    nx=0;nz=0;
    o=[];
    i=[];
    for nn = 1 : op
      o=[o,1];
    end
    o=int(o(:));nout=size(o,1);
    ci=1;nevin=1;
    co=[];nevout=0;
    funtyp=2004;
    depu=%t;
    dept=%f;
    dep_ut=[depu dept];

    [ok,tt]=getCode_mbx_rcv_if(funam)
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
  out=1
  outsz = 1
  name = 'MBX'
  ipaddr = '127.0.0.1'

  model=scicos_model()
  model.sim=list(' ',2004)
  model.in=[]
  model.out=outsz
  model.evtin=1
  model.evtout=[]
  model.state=[]
  model.dstate=[]
  model.rpar=[]
  model.ipar=[]
  model.blocktype='c'
  model.firing=[]
  model.dep_ut=[%t %f]
  model.nzcross=0

  label=list([sci2exp(out),name,ipaddr],[])

  gr_i=['xstringb(orig(1),orig(2),[''Mbx Rcv no blk'';name],sz(1),sz(2),''fill'');']
  x=standard_define([3 2],model,label,gr_i)

end
endfunction

function [ok,tt]=getCode_mbx_rcv_if(funam)
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
  textmp($+1)='int i;'
  textmp($+1)='double y[' + string(nout) + '];'
  textmp($+1)='double t = get_scicos_time();' 
  textmp($+1)='  switch(flag) {'
  textmp($+1)='  case 4:'
  textmp($+1)='    blk=inp_mbx_receive_if_init('+ string(nout)+','+ '""' + name + '"",""'+ipaddr+'"");'
  textmp($+1)='    break;'; 
  textmp($+1)='  case 1:'
  textmp($+1)='    inp_mbx_receive_if_input(blk,y,t);'
  textmp($+1)='    for (i=0;i<' + string(nout) + ';i++) block->outptr[i][0] = y[i];'
  textmp($+1)='    break;'
  textmp($+1)='  case 5:'
  textmp($+1)='    inp_mbx_receive_if_end(blk);'
  textmp($+1)='    break;'
  textmp($+1)='  }'
  textmp($+1)='#endif'
  textmp($+1)='}'
  tt=textmp;

  ok = %t;

endfunction
