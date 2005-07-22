/**
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org> 
 * @note Copyright (C) 2005 Nextream France S.A.
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

#include <compat/intr.h>

int rt_request_irq (unsigned irq,
		    void (*handler)(unsigned irq, void *cookie),
		    void *cookie)
{
    return rthal_irq_request(irq,handler,NULL,cookie);
}

int rt_release_irq (unsigned irq)

{
    return rthal_irq_release(irq);
}

void rt_ack_irq (unsigned irq)

{
    rthal_irq_enable(irq);
}

void rt_enable_irq (unsigned irq)

{
    rthal_irq_enable(irq);
}

void rt_disable_irq (unsigned irq)

{
    rthal_irq_disable(irq);
}

EXPORT_SYMBOL(rt_request_irq);
EXPORT_SYMBOL(rt_release_irq);
EXPORT_SYMBOL(rt_ack_irq);
EXPORT_SYMBOL(rt_enable_irq);
EXPORT_SYMBOL(rt_disable_irq);
