ok=gen_gui();
if ok then [CCode,FCode]=gen_blocks(), end;
Code=make_decl();
Code=[Code;Protos];
Code=[Code;make_static()];
Code=[Code;make_computational()];
Code=[Code;make_main0()];         
Code=[Code;make_main1()];
Code=[Code;make_main2()];
Code=[Code;make_init()];
Code=[Code;make_end()];
Code=[Code;c_make_doit1(cpr,%f)];
Code=[Code;c_make_doit2(cpr,%f)];
Code=[Code;c_make_outtb(%f)];
Code=[Code;c_make_initi(cpr,%f)];
Code=[Code;c_make_endi(cpr,%f)];
Code=[Code;make_putevs()];
write_code(Code,CCode,FCode);
[ok,Makename]=buildnewblock()
if ok then ok=gen_loader(),end
dynflag=%t



