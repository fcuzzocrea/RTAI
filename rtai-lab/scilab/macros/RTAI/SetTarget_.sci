function  SetTarget_()
  Cmenu='Open/Set'
  xinfo('Click on a Superblock (without activation output)'+..
        ' to obtain a coded block ! ')

  k=[]
  while %t
    if %pt==[] then
      [btn,%pt,win,Cmenu]=cosclick()

      if Cmenu<>[] then
        [%win,Cmenu]=resume(win,Cmenu)
      end
    else
      win=%win
    end

    xc=%pt(1);yc=%pt(2);%pt=[]
    k=getobj(scs_m,[xc;yc])
    if k<>[] then break,end
  end

  if scs_m.objs(k).model.sim(1)=='super' then
    disablemenus()
    all_scs_m=scs_m;
    lab=scs_m.objs(k).model.rpar.props.void3;
    if lab==[] then
	lab = ['rtai','ode4','10'];
    end

    while %t
      [ok,target,odefun,stp]=getvalue(..
          'Please fill the following values',..
          ['Target: ';
	  'Ode function: ';
	  'Step between sampling: '],..
          list('str',1,'str',1,'str',1),lab);
      if ~ok then break,end
  
      [fd,ierr]=mopen(SCI+'/macros/RTAI/RT_templates/'+target+'.mak','r');
      if ierr==0 then
         mclose(fd);
         lab=[target,odefun,stp];
	 scs_m.objs(k).model.rpar.props.void3 = lab;
         break;
      else
         x_message('Target not valid');
      end
    end

    edited=%t;
    enablemenus()
  else
    x_message('Block is not a superblock');
  end
endfunction

