/*
COPYRIGHT (C) 2000  Paolo Mantegazza (mantegazza@aero.polimi.it)

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
*/

#ifndef _RTAI_ASM_PPC_FPU_H_
#define _RTAI_ASM_PPC_FPU_H_

#include <asm/processor.h>

typedef struct ppc_fpu_env { unsigned long fpu_reg[66]; } FPU_ENV;

#define save_cr0_and_clts(x)
#define restore_cr0(x)
#define enable_fpu()

#ifdef CONFIG_RTAI_FPU_SUPPORT
/*
 * Saving/restoring the FPU environment in PPC is like eating a cake, very simple. Just save/restore all of the floating 
 * point registers, recall they are always 64 bits long, and the floating point state register. Remark: at task init we
 * always enable FP, i.e. MSR flag FP set to 1, for real time tasks and accept default actions for faulty FLOPs, i.e. MSR 
 * flags FE0 and FE1 are set to zero.
 */

#define stringize(a) #a
#define str(a) stringize(a)

extern void __save_fpenv(FPU_ENV *fpenv);
#define save_fpenv(x) __save_fpenv(&(x))
#if 0
// FIXME: it does not work, waiting for cleanup!
static inline void __save_fpenv(FPU_ENV *env)
{
	asm(
	"stw	 0,  -4(1)\n"
	"mfmsr    0\n"
	"ori      0, 0, " str(MSR_FP) "\n"
	"mtmsr    0\n"
	"lwz      0,  -4(1)\n"
	"isync\n"
	"stfd	 0, 0*8(3)\n"
	"stfd	 1, 1*8(3)\n"
	"stfd	 2, 2*8(3)\n"
	"stfd	 3, 3*8(3)\n"
	"stfd	 4, 4*8(3)\n"
	"stfd	 5, 5*8(3)\n"
	"stfd	 6, 6*8(3)\n"
	"stfd	 7, 7*8(3)\n"
	"stfd	 8, 8*8(3)\n"
	"stfd	 9, 9*8(3)\n"
	"stfd	10,10*8(3)\n"
	"stfd	11,11*8(3)\n"
	"stfd	12,12*8(3)\n"
	"stfd	13,13*8(3)\n"
	"stfd	14,14*8(3)\n"
	"stfd	15,15*8(3)\n"
	"stfd	16,16*8(3)\n"
	"stfd	17,17*8(3)\n"
	"stfd	18,18*8(3)\n"
	"stfd	19,19*8(3)\n"
	"stfd	20,20*8(3)\n"
	"stfd	21,21*8(3)\n"
	"stfd	22,22*8(3)\n"
	"stfd	23,23*8(3)\n"
	"stfd	24,24*8(3)\n"
	"stfd	25,25*8(3)\n"
	"stfd	26,26*8(3)\n"
	"stfd	27,27*8(3)\n"
	"stfd	28,28*8(3)\n"
	"stfd	29,29*8(3)\n"
	"stfd	30,30*8(3)\n"
	"stfd	31,31*8(3)\n"
	"mffs	 0\n"
	"stfd	 0,32*8(3)\n");
}
#endif

extern void __restore_fpenv(FPU_ENV *fpenv);
#define restore_fpenv(x) __restore_fpenv(&(x))
#if 0
// FIXME: it does not work, waiting for cleanup!
static inline void __restore_fpenv(FPU_ENV *env)
{
	asm(
	"lfd	 0, 32*8(3)\n"
	"mtfsf	 0xFF,0\n"
	"lfd	 0, 0*8(3)\n"
	"lfd	 1, 1*8(3)\n"
	"lfd	 2, 2*8(3)\n"
	"lfd	 3, 3*8(3)\n"
	"lfd	 4, 4*8(3)\n"
	"lfd	 5, 5*8(3)\n"
	"lfd	 6, 6*8(3)\n"
	"lfd	 7, 7*8(3)\n"
	"lfd	 8, 8*8(3)\n"
	"lfd	 9, 9*8(3)\n"
	"lfd	10,10*8(3)\n"
	"lfd	11,11*8(3)\n"
	"lfd	12,12*8(3)\n"
	"lfd	13,13*8(3)\n"
	"lfd	14,14*8(3)\n"
	"lfd	15,15*8(3)\n"
	"lfd	16,16*8(3)\n"
	"lfd	17,17*8(3)\n"
	"lfd	18,18*8(3)\n"
	"lfd	19,19*8(3)\n"
	"lfd	20,20*8(3)\n"
	"lfd	21,21*8(3)\n"
	"lfd	22,22*8(3)\n"
	"lfd	23,23*8(3)\n"
	"lfd	24,24*8(3)\n"
	"lfd	25,25*8(3)\n"
	"lfd	26,26*8(3)\n"
	"lfd	27,27*8(3)\n"
	"lfd	28,28*8(3)\n"
	"lfd	29,29*8(3)\n"
	"lfd	30,30*8(3)\n"
	"lfd	31,31*8(3)\n");
}
#endif

#else

#define save_fpenv(x)
#define restore_fpenv(x)

#endif


#endif

