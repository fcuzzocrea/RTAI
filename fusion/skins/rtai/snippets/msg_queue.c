#include <stdio.h>
#include <string.h>
#include <rtai/queue.h>

RT_QUEUE q_desc;

int main (int argc, char *argv[])

{
    ssize_t len;
    void *msg;
    int err;

    /* Bind to a queue which has been created elsewhere, either in
       kernel or user-space. The call will block us until such queue
       is created with the expected name. The queue should have been
       created with the Q_SHARED mode set, which is implicit when
       creation takes place in user-space. */

    err = rt_queue_bind(&q_desc,"SomeQueueName");

    if (err)
	fail();

    /* Collect each message sent to the queue by the queuer() routine,
       until the queue is eventually removed from the system by a call
       to rt_queue_delete(). */

    while ((len = rt_queue_recv(&q_desc,&msg,TM_INFINITE)) > 0)
	{
	printf("received message> len=%d bytes, ptr=%p, s=%s\n",
	       len,msg,(const char *)msg);
	rt_queue_free(&q_desc,msg);
	}

    if (len != -EIDRM)
	/* We received some unexpected error notification. */
	fail();

    /* ... */
}

void queuer (void)

{
    static char *messages[] = { "hello", "world", NULL };
    int n, len;
    void *msg;

    for (n = 0; messages[n] != NULL; n++)
	{
	len = strlen(messages[n]) + 1;
	/* Get a message block of the right size. */
	msg = rt_queue_alloc(&q_desc,len);

	if (!msg)
	    /* No memory available. */
	    fail();

	strcpy(msg,messages[n]);
	rt_queue_send(&q_desc,msg,len,Q_NORMAL);
	}
}

void cleanup (void)

{
    /* We need to unbind explicitely from the queue in order to
       properly release the underlying memory mapping. Exiting the
       process unbinds all mappings automatically. */
    rt_queue_unbind(&q_desc);
}
