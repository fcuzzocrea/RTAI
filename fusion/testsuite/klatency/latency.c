#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "latency.h"

int main (int argc, char **argv)

{
    const char *const communication_channel = "/dev/rtp0";
    struct rtai_latency_stat s;
    ssize_t sz;
    int n = 0;
    int fd;
    
    setlinebuf(stdout);
    
    fd = open(communication_channel, O_RDWR);
    
    if (fd < 0)
        {
        fprintf(stderr, "open(%s): %m\n", communication_channel);
        exit(1);
        }

    for (;;)
        {
        sz = read(fd,&s,sizeof(s));

        if (!sz)
            break;
        
        if (sz != sizeof(s))
            {
            perror("read");
            exit(1);
            }

        if ((n++ % 21)==0)
            printf("RTH|%12s|%12s|%12s|%12s\n", "jit min","jit avg","jit max","overrun");

        printf("RTD|%12d|%12d|%12d|%12d\n",
               s.minjitter,
               s.avgjitter,
               s.maxjitter,
               s.overrun);
        }

    return 0;
}
