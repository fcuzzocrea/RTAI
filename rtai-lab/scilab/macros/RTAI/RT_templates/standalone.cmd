[CCode,FCode]=gen_blocks();
Code=make_decl_standalone();
Code=[Code;Protostalone];
Code=[Code;make_static_standalone()];
Code=[Code;make_standalone()];
files=write_code(Code,CCode,FCode);
write_act_sens()
Makename=gen_make(rdnom,files,archname);
ok=compile_standalone();
dynflag=%f



