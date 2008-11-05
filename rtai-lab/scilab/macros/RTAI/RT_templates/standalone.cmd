[CCode,FCode]=gen_blocks()
Code=make_standalone42();
files=write_code(Code,CCode,FCode);
Makename=rt_gen_make(rdnom,files,archname);
ok=compile_standalone();
