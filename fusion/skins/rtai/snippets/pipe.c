#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <rtai/pipe.h>

#define PIPE_MINOR 0

/* User-space side */

int pipe_fd;

int main (int argc, char *argv[])

{
    char devname[32], buf[16];

    /* ... */

    sprintf(devname,"/dev/rtp%d",PIPE_MINOR);
    pipe_fd = open(devname,O_RDWR);
    
    if (pipe_fd < 0)
	fail();

    /* Wait for the prompt string "Hello"... */
    read(pipe_fd,buf,sizeof(buf));

    /* Then send the reply string "World": */
    write(pipe_fd,"World",sizeof("World"));

    /* ... */
}

void cleanup (void)

{
    close(pipe_fd);
}

/* Kernel-side */

RT_PIPE pipe_desc;

int init_module (void)

{
    RT_PIPE_MSG *msgout, *msgin;
    int err, len;

    /* ... */

    err = rt_pipe_open(&pipe_desc,PIPE_MINOR);

    if (err)
	fail();

    len = sizeof("Hello");
    /* Get a message block of the right size in order to initiate the
       message-oriented dialog with the user-space process. Sending a
       continuous stream of bytes is also possible using
       rt_pipe_stream(), in which case no message buffer needs to be
       preallocated. */
    msgout = rt_pipe_alloc(len);

    if (!msgout)
	fail();

    /* Send prompt message "Hello" (the output buffer will be freed
       automatically)... */
    strcpy(RT_PIPE_MSGPTR(msgout),"Hello");
    rt_pipe_write(&pipe_desc,msgout,len,P_NORMAL);

    /* Then wait for the reply string "World": */
    rt_pipe_read(&pipe_desc,&msgin,TM_INFINITE);
    printf("received msg> %s, size=%d\n",P_MSGPTR(msg),P_MSGSIZE(msg));
    /* Free the received message buffer. */
    rt_pipe_free(&pipe_desc,msgin);

    /* ... */
}

void cleanup_module (void)

{
    rt_pipe_close(&pipe_desc);
}
