#include <stdio.h>
#include <string.h>

#include "def_const.h"
#include "config_data.h"

int get_nin()
{
  int n;

  printf("Number of input: ");
  scanf("%d",&n);
  return(n);
}

int get_nout()
{
  int n;

  printf("Number of output: ");
  scanf("%d",&n);
  return(n);
}

void get_in_strline(int n, char * str)
{
  int i;
  int type;
  char locStr[25];

  do{
    printf("\n\nInput %d\n\n",n+1);
    for(i=0;i<N_INPUT;i++) printf("%2d. %s\n",i+1,inputIO[i][0]);
    printf("\nInput # -> ");
    scanf("%d",&type);
    type--;

    printf("\n\nParameters for input -> %s\n\n",inputIO[type][0]);

    sprintf(str,"%s inp %d ",inputIO[type][0],n+1);

    for(i=1;i<N_PARAMS;i++){
      if(inputIO[type][i][0]=='-') strcat(str,"0 ");
      else{
	printf("%s : ",inputIO[type][i]);
	scanf("%s",locStr);
	strcat(str,locStr);
	strcat(str," ");
      }
    }
    printf("\nOK? [y/n] : ");
    scanf("%s",locStr);
  }while(locStr[0]!='y' && locStr[0]!='Y');
}

void get_out_strline(int n, char * str)
{
  int i;
  int type;
  char locStr[25];

  do{
    printf("\n\nOutput %d\n\n",n+1);
    for(i=0;i<N_OUTPUT;i++) printf("%2d. %s\n",i+1,outputIO[i][0]);
    printf("\nOutput # -> ");
    scanf("%d",&type);
    type--;

    printf("\n\nParameters for output -> %s\n\n",outputIO[type][0]);

    sprintf(str,"%s out %d ",outputIO[type][0],n+1);

    for(i=1;i<N_PARAMS;i++){
      if(outputIO[type][i][0]=='-') strcat(str,"0 ");
      else{
	printf("%s : ",outputIO[type][i]);
	scanf("%s",locStr);
	strcat(str,locStr);
	strcat(str," ");
      }
    }
    printf("\nOK? [y/n] : ");
    scanf("%s",locStr);
  }while(locStr[0]!='y' && locStr[0]!='Y');
}

main(int argc, char** argv)
{
  FILE * f;
  int n_in, n_out;
  int i;
  char str[80];

  if(argc==1) f = fopen("config","w");
  else        f = fopen(argv[1],"w");

  n_in = get_nin();
  n_out = get_nout();

  for(i=0;i<n_in;i++) {
    get_in_strline(i, str);
    fprintf(f,"%s\n",str);
  }

  for(i=0;i<n_out;i++) {
    get_out_strline(i, str);
    fprintf(f,"%s\n",str);
  }

  fprintf(f,"end\n");
  fclose(f);
}
