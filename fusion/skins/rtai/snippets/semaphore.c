#include <rtai/sem.h>

#define SEM_INIT 1	 /* Initial semaphore count */
#define SEM_MODE S_FIFO	 /* Wait by FIFO order */

RT_SEM sem_desc;

int main (int argc, char *argv[])

{
    int err;

    /* Create a sempahore; we could also have attempted to bind to
       some pre-existing object, using rt_sem_bind() instead of
       creating it. */

    err = rt_sem_create(&sem_desc,"MySemaphore",SEM_INIT,SEM_MODE);

    /* Now, wait for a semaphore unit, then release it: */

    rt_sem_p(&sem_desc,RT_TIME_INFINITE);

    /* ... */

    rt_sem_v(&sem_desc);

    /* ... */
}

void cleanup (void)

{
    rt_sem_delete(&sem_desc);
}
