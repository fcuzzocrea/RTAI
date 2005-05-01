#include <stdio.h>
#include <unistd.h>
#include <rtai/task.h>
#include <rtai/queue.h>

#define CONSUMER_TASK_PRI    1
#define CONSUMER_STACK_SIZE  8192

#define PRODUCER_TASK_PRI    2
#define PRODUCER_STACK_SIZE  8192

#define CONSUMER_WAIT 150
#define PRODUCER_TRIG 40

static const char *satch_s_tunes[] = {
    "Surfing With The Alien",
    "Lords of Karma",
    "Banana Mango",
    "Psycho Monkey",
    "Luminous Flesh Giants",
    "Moroccan Sunset",
    "Satch Boogie",
    "Flying In A Blue Dream",
    "Ride",
    "Summer Song",
    "Speed Of Light",
    "Crystal Planet",
    "Raspberry Jam Delta-V",
    "Champagne?",
    "Clouds Race Across The Sky",
    "Engines Of Creation"
};

static RT_TASK producer_task,
               consumer_task;

void consumer (void *cookie)

{
    RT_TASK_MCB mcb;
    const char *msg;
    int flowid;

    for (;;)
	{
	rt_task_sleep(CONSUMER_WAIT);

	for (;;)
	    {
	    mcb.opcode = 0;	/* Dummy. */
	    mcb.data = (caddr_t)&msg;
	    mcb.size = sizeof(msg);
	    flowid = rt_task_receive(&mcb,TM_NONBLOCK);

	    if (flowid < 0)
		break;

	    printf("Now playing %s...\n",msg);
	    rt_task_reply(flowid,NULL);
	    }
	}
}

void producer (void *cookie)

{
    int next_msg = 0;
    RT_TASK_MCB mcb;
    const char *msg;

    for (;;)
	{
	rt_task_sleep(PRODUCER_TRIG);

	msg = satch_s_tunes[next_msg++];
	next_msg %= (sizeof(satch_s_tunes) / sizeof(satch_s_tunes[0]));

	mcb.opcode = 0;	/* Dummy. */
	mcb.data = (caddr_t)&msg;
	mcb.size = sizeof(msg);
	rt_task_send(&consumer_task,&mcb,NULL,TM_INFINITE);
	}
}

int root_thread_init (void)

{
    rt_timer_start(1000000);	/* 1ms periodic tick. */

    rt_task_spawn(&consumer_task,
		  "ConsumerTask",
		  CONSUMER_STACK_SIZE,
		  CONSUMER_TASK_PRI,
		  0,
		  &consumer,
		  NULL);

    rt_task_spawn(&producer_task,
		  "ProducerTask",
		  PRODUCER_STACK_SIZE,
		  PRODUCER_TASK_PRI,
		  0,
		  &producer,
		  NULL);
    return 0;
}

void root_thread_exit (void)

{
    rt_task_delete(&producer_task);
    rt_task_delete(&consumer_task);
}

#ifndef __RTAI_SIM__

int main (int ac, char *av[])

{
    root_thread_init();
    pause();
    root_thread_exit();

    return 0;
}

#endif /* __RTAI_SIM__ */
