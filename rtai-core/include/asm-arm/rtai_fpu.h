/*
 * include/asm-arm/fpu.h
 *
 * Just a dummy for RTAI now, because our ARM has no FPU
 * If you have an ARM-processor with FPU, add the necessary code to up_task_sw ...
 */

#ifndef RTAI_FPU_ASM_H
#define RTAI_FPU_ASM_H

#ifdef CONFIG_RTAI_FPU_SUPPORT

extern void save_fpenv(long *fpu_reg);
extern void restore_fpenv(long *fpu_reg);

#else /* notdef CONFIG_RTAI_FPU_SUPPORT */

#define save_fpenv(x)
#define restore_fpenv(x)

#endif /* CONFIG_RTAI_FPU_SUPPORT */

#endif /* RTAI_FPU_ASM_H */
