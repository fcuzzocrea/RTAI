void getstr(char * str, int par[], int init, int len)
{
  int i;
  int j=0;

  for(i=init;i<init+len;i++)
    str[j++]=(char) par[i];

  str[j]='\0';
}

