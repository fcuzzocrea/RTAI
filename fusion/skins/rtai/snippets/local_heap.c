#include <rtai/heap.h>

#define HEAP_SIZE (256*1024)
#define HEAP_MODE 0		/* Local heap. */

RT_HEAP heap_desc;

int init_module (void)

{
    void *block;
    int err;

    /* Create a 256Kb heap usable for dynamic memory allocation of
       variable-size blocks in kernel space. */

    err = rt_heap_create(&heap_desc,"MyHeapName",HEAP_SIZE,HEAP_MODE);

    if (err)
	fail();

    /* Request a 16-bytes block, asking for a blocking call until the
       memory is available: */
    err = rt_heap_alloc(&heap_desc,16,TM_INFINITE,&block);

    /* Free the block: */
    rt_heap_free(&heap_desc,block);
   
    /* ... */
}

void cleanup_module (void)

{
    rt_heap_delete(&heap_desc);
}
