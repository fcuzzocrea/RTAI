
/* We're _not_ going to implement this hack method of getting into
 * the kernel on mips.  We'll do it the correct way and use a
 * driver. */

#ifndef _RTAI_ASM_MIPS_RTAI_SHM_H_
#define _RTAI_ASM_MIPS_RTAI_SHM_H_

#undef __SHM_USE_VECTOR

#define RTAI_SHM_VECTOR  0xFD

#ifndef __KERNEL__

static inline long long rtai_shmrq(unsigned long srq, unsigned long whatever)
{
	return 0;
}

#endif

#endif

