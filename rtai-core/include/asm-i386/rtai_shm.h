/*
 * Copyright (C) 1999-2003 Paolo Mantegazza <mantegazza@aero.polimi.it>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _RTAI_ASM_I386_SHM_H
#define _RTAI_ASM_I386_SHM_H

#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/rtai_vectors.h>

#define __SHM_USE_VECTOR 1

#ifndef __KERNEL__

#ifdef __SHM_USE_VECTOR
static inline long long rtai_shmrq(int srq, unsigned int whatever)
{
	long long retval;
	RTAI_DO_TRAP(RTAI_SHM_VECTOR,retval,srq,whatever);
	return retval;
}
#endif /* __SHM_USE_VECTOR */

#else /* __KERNEL__ */

#define RTAI_SHM_HANDLER rtai_shm_handler

#define __STR(x) #x
#define STR(x) __STR(x)

#define DEFINE_SHM_HANDLER \
static void rtai_shm_handler(void) \
{ \
	__asm__ __volatile__ (" \
	cld; pushl %es; pushl %ds; pushl %ebp;\n\t \
	pushl %edi; pushl %esi; pushl %ecx;\n\t \
	pushl %ebx; pushl %edx; pushl %eax;\n\t \
	movl $" STR(__KERNEL_DS) ",%ebx; mov %bx,%ds; mov %bx,%es"); \
	__asm__ __volatile__ ("call "SYMBOL_NAME_STR(shm_handler)); \
	__asm__ __volatile__ (" \
	addl $8,%esp; popl %ebx; popl %ecx; popl %esi;\n\t \
	popl %edi; popl %ebp; popl %ds; popl %es; iret"); \
}

#endif /* __KERNEL__ */

/* convert virtual user memory address to physical address */
/* (virt_to_phys only works for kmalloced kernel memory) */

static inline unsigned long uvirt_to_kva(pgd_t *pgd, unsigned long adr)
{
	unsigned long ret = 0UL;
	pmd_t *pmd;
	pte_t *ptep, pte;

	if(!pgd_none(*pgd)) {
		pmd = pmd_offset(pgd, adr);
		if (!pmd_none(*pmd)) {
			ptep = pte_offset(pmd, adr);
			pte = *ptep;
			if(pte_present(pte)){
				ret = (unsigned long) page_address(pte_page(pte));
				ret |= (adr&(PAGE_SIZE-1));
			}
		}
	}
	return ret;
}

static inline unsigned long uvirt_to_bus(unsigned long adr)
{
	unsigned long kva, ret;

	kva = uvirt_to_kva(pgd_offset(current->mm, adr), adr);
	ret = virt_to_bus((void *)kva);

	return ret;
}

static inline unsigned long kvirt_to_bus(unsigned long adr)
{
	unsigned long va, kva, ret;

	va = VMALLOC_VMADDR(adr);
	kva = uvirt_to_kva(pgd_offset_k(va), va);
	ret = virt_to_bus((void *)kva);

	return ret;
}

static inline unsigned long kvirt_to_pa(unsigned long adr)
{
	unsigned long va, kva, ret;

	va = VMALLOC_VMADDR(adr);
	kva = uvirt_to_kva(pgd_offset_k(va), va);
	ret = __pa(kva);

	return ret;
}

#endif  /* !_RTAI_ASM_I386_SHM_H */
