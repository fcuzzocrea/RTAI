/*
COPYRIGHT (C) 2003  Roberto Bucher (roberto.bucher@die.supsi.ch)

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
*/

#include <stdio.h>
#include <string.h>

FILE * fin;
FILE * fout;

void write_act(int port, char * s_init, char * s_out, char * s_end)
{
    fprintf(fout,"s/\\/\\* att_%d_init \\*\\//%s/g\n",port,s_init);
    fprintf(fout,"s/\\/\\* att_%d_output \\*\\//%s/g\n",port,s_out);
    fprintf(fout,"s/\\/\\* att_%d_end \\*\\//%s/g\n",port,s_end);
}

void write_sens(int port, char * s_init, char * s_inp, char * s_upd, char * s_end)
{
    fprintf(fout,"s/\\/\\* sens_%d_init \\*\\//%s/g\n",port,s_init);
    fprintf(fout,"s/\\/\\* sens_%d_input \\*\\//%s/g\n",port,s_inp);
    fprintf(fout,"s/\\/\\* sens_%d_update \\*\\//%s/g\n",port,s_upd);
    fprintf(fout,"s/\\/\\* sens_%d_end \\*\\//%s/g\n",port,s_end);
}

void fill_act_and_sens()
{
    char type[40];
    char io[5];
    int port;
    int ch;
    char name[20];
    char sParam[20];
    double dParam[5];
    int i;
    char str_init[100];
    char str_end[100];
    char str_upd[100];
    char str_inp[100];
    char str_out[100];

    while(!feof(fin)){
	fscanf(fin,"%s",type);
	if(strcmp(type,"end")==0) break;
	fscanf(fin,"%s",io);
	fscanf(fin,"%d",&port);
	fscanf(fin,"%d",&ch);
	fscanf(fin,"%s",name);
	fscanf(fin,"%s",sParam);
	for(i=0;i<5;i++) fscanf(fin,"%lf",&dParam[i]);
	sprintf(str_init,"%s_%s_init(%d,%d,\"%s\",\"%s\",%lf,%lf,%lf,%lf,%lf);",io,type,
		port,ch,name,sParam,dParam[0],dParam[1],dParam[2],dParam[3],dParam[4]);
	sprintf(str_end,"%s_%s_end(%d);",io,type,port);
	if(io[0]=='i'){
	    sprintf(str_upd,"%s_%s_update();",io,type);	    
	    sprintf(str_inp,"%s_%s_input(%d,y,*t);",io,type,port);
	    write_sens(port,str_init,str_inp,str_upd,str_end);	    
	}
	else{
	    sprintf(str_out,"%s_%s_output(%d,u,*t);",io,type,port);
	    write_act(port,str_init,str_out,str_end);	    
	}	
    }
    fclose(fin);
}

void open_files(char * file1, char * file2)
{
    char fileout[30];

    fin=fopen(file2,"r");
    if(fin==NULL) {
	printf("Unable to find %d\n",file2);
	exit(1);
    }
    sprintf(fileout,"%s_conf",file1);
    fout=fopen(fileout,"w");
}

main(int argc,char *argv[])
{
    if(argc!=3){
	printf("Usage: gen_io <appname> <configname>\n");
	exit(1);
    }
    open_files(argv[1],argv[2]);
    fill_act_and_sens();
    fclose(fin);
    fclose(fout);
}
