function [x,y,typ]=rtai_scope(job,arg1,arg2)
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
  oldlb=label(1)
  while %t do
    [ok,port,ip,name,lab]=..
        getvalue('Set RTAI-scope block parameters',..
        ['Port nr';
        'input ports';
	'Scope name'],..
         list('vec',1,'vec',-1,'str',1),label(1))

    if ~ok then break,end
    label(1)=lab
    funam='o_scope_' + string(port);
    xx=[];ng=[];z=0;
    nx=0;nz=0;
    o=[];
    i=[];
    for nn = 1 : ip
      i=[i,1];
    end
    i=int(i(:));nin=size(i,1);
    ci=1;nevin=1;
    co=[];nevout=0;
    funtyp=2004;
    depu=%t;
    dept=%f;
    dep_ut=[depu dept];

    tt=label(2);
    if find(oldlb <> label(1)) <> [] then
      tt=[]
    end

    [ok,tt]=getCode(funam,tt)
    if ~ok then break,end
    [model,graphics,ok]=check_io(model,graphics,i,o,ci,co)
    if ok then
      model.sim=list(funam,funtyp)
      model.in=i
      model.out=[]
      model.evtin=ci
      model.evtout=[]
      model.state=[]
      model.dstate=0
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
  port = 1
  name = 'SCOPE'
  rparam = [0,0,0,0,0]

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

  label=list([sci2exp(port),sci2exp(in),name],[])

  gr_i=['xstringb(orig(1),orig(2),''RTAI Scope'',sz(1),sz(2),''fill'');']
  x=standard_define([2 2],model,label,gr_i)

end
endfunction

function [ok,tt]=getCode(funam,tt)
//
if tt==[] then
  
  textmp=[
	  '#ifndef MODEL'
	  '#include <math.h>';
	  '#include <stdlib.h>';
	  '#include <scicos/scicos_block.h>';
	  '#endif'
          '';
	  'void '+funam+'(scicos_block *block,int flag)';
	 ];
  ttext=[];
  textmp($+1)='{'
  
  textmp($+1)='  switch(flag) {'
  textmp($+1)='  case 4:'
  textmp($+1)='   '+funam+"_bloc_init(block,flag);"
  textmp($+1)='   break;'; 
    l1 = '  out_rtai_scope_init(' + string(port) + ',' + string(nin) + ',';
    l2 = '""' + name + '"","""",0,0,0,0,0';
    ttext=[ttext;'int '+funam+"_bloc_init(scicos_block *block,int flag)";
	   '{';
	   '#ifdef MODEL'
	   l1 + l2 + ');';
	   '#endif'
	   '  return 0;';
           '}'];
  textmp($+1)=' '

  textmp($+1)='  case 2:'
  textmp($+1)='   set_block_error('+funam+"_bloc_outputs(block,flag));"
  textmp($+1)='   break;'; 
  ttext=[ttext;'int '+funam+"_bloc_outputs(scicos_block *block,int flag)";
	  "{";
           "  int i;";
	   "  double u[" + string(nin) + "];";
	   "  double t = get_scicos_time();";
	   "  for (i=0;i<" + string(nin) + ";i++) u[i]=block->inptr[i][0];";
	   '#ifdef MODEL'
           "  out_rtai_scope_output(" + string(port) + ",u,t);";
	   '#endif'
	   "  return 0;";
           "}"];
  
  textmp($+1)='  case 5: '
      textmp($+1)='     set_block_error('+funam+"_bloc_ending(block,flag));";
      textmp($+1)='   break;'; 
        ttext=[ttext;'int '+funam+"_bloc_ending(scicos_block *block,int flag)";
	   "{";
	   '#ifdef MODEL'
	   "  out_rtai_scope_end(" + string(port) + ");";
	   '#endif'
	   "  return 0;";
           "}"];
  textmp($+1)='  }'
  textmp($+1)='}'
  textmp=[textmp;' '; ttext];
else
  textmp=tt;
end

while 1==1
  [txt]=x_dialog(['Function definition in C';
		  'Here is a skeleton of the functions which you should edit'],..
		 textmp);
  
  if txt<>[] then
    tt=txt
    textmp=txt;
    break;
  end
end

endfunction
