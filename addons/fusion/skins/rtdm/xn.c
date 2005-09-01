/*
 * Copyright (C) 2005 Paolo Mantegazza <mantegazza@aero.polimi.it>
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


#include <nucleus/xn.h>

// this is used and must be provided
xnlock_t nklock = XNARCH_LOCK_UNLOCKED;

// this is not used but must be provided
static xnpod_t nkpodummy;
xnpod_t *nkpod = &nkpodummy;

// this is needed for the subsequent unregistration of RTDM syscalls
static void *saved_systab;

// reg/unregistration of RTDM user space interface, as an LXRT extension in 
// disguise; it could be made an inline, it is used only once, but made this 
// way because of the need of saved_systab above
int xnshadow_register_interface(const char *name, unsigned magic, int nrcalls, xnsysent_t *systab, int (*eventcb)(int))
{
        int i;
        saved_systab = systab;
        for (i = 0; i < nrcalls; i++) {
                systab[i].fun  = (unsigned long)systab[i].type;
                systab[i].type = (void *)1;
        }
        if(set_rt_fun_ext_index((void *)systab, RTDM_INDX)) {
                xnprintf("Recompile RTDM with a different index\n");
                return -EACCES;
        }
        return 0;
}

int xnshadow_unregister_interface(int muxid)
{
        reset_rt_fun_ext_index(saved_systab, RTDM_INDX);
        return 0;
}

// this is difficult to inline; needed mostly because RTDM isr does not care of
// the PIC; WARNING: the RTAI dispatcher might have cared of the ack already
int xnintr_irq_handler(unsigned long irq, xnintr_t *intr)
{
	int retval = ((int (*)(void *))intr->isr)(intr);
	++intr->hits;
	if (retval & XN_ISR_ENABLE) {
	        xnintr_enable(intr);
	}
	if (retval & XN_ISR_CHAINED) {
	        xnintr_chain(intr->irq);
	}
	return 0;
}

EXPORT_SYMBOL(xnintr_irq_handler);
