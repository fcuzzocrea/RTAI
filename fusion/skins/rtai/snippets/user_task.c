#include <rtai/task.h>

#define TASK_PRIO  0 /* Highest RT priority */
#define TASK_MODE  0 /* No flags */
#define TASK_STKSZ 0 /* Stack size (unused in user-space) */

RT_TASK task_desc;

void task_body (void *cookie)

{
    for (;;) {
    /* ... "cookie" should be NULL ... */
    }
}

int main (int argc, char *argv[])

{
    int err;

    /* ... */

    err = rt_task_create(&task_desc,
			 "MyTaskName",
			 TASK_STKSZ,
			 TASK_PRIO,
			 TASK_MODE);
    if (!err)
	rt_task_start(&task_desc,&task_body,NULL);

    /* ... */
}

void cleanup (void)

{
    rt_task_delete(&task_desc);
}
