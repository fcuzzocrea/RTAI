function [x,y,typ]=rtai_comedi_dioout(job,arg1,arg2)
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
    [ok,port,ch,name,thresh,lab]=..
        getvalue('Set RTAI-COMEDI DIO block parameters',..
        ['Port';
        'Channel';
        'Device';
        'Threshold'],..
         list('vec',1,'vec',1,'str',1,'vec',1),label(1))

    if ~ok then break,end
    label(1)=lab
    funam='o_comedi_data_' + string(port);
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
      model.evtin=1
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
  port=1;ch=0;thresh=1;; 
  rt_par=[thresh,0,0,0,0]
  rpar=rt_par(:)
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

  label=list([sci2exp(port),sci2exp(ch),name,sci2exp(thresh)],[])

  gr_i=['xstringb(orig(1),orig(2),''COMEDI DIOOUT'',sz(1),sz(2),''fill'');']
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
    	 l1 = '  out_rtai_comedi_dio_init(' + string(port) + ',' + string(ch) + ',';
    	 l2 = '""' + name + '"","""",' + string(thresh) + ',0,0,0,0';
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
	   "  double u[1];";
	   "  double t = get_scicos_time();";
	   "  u[0]=block->inptr[0][0];";
	   '#ifdef MODEL'
           "  out_rtai_comedi_dio_output(" + string(port) + ",u,t);";
	   '#endif'
	   "  return 0;";
           "}"];
  
  textmp($+1)='  case 5: '
      textmp($+1)='     set_block_error('+funam+"_bloc_ending(block,flag));";
      textmp($+1)='   break;'; 
        ttext=[ttext;'int '+funam+"_bloc_ending(scicos_block *block,int flag)";
	   "{";
	   '#ifdef MODEL'
	   "  out_rtai_comedi_dio_end(" + string(port) + ");";
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
