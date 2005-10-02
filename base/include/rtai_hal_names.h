/*
 * Copyright 2005 Paolo Mantegazza <mantegazza@aero.polimi.it>
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


#ifndef _RTAI_HAL_NAMES_H
#define _RTAI_HAL_NAMES_H

#define HAL_VERSION_STRING   ADEOS_VERSION_STRING

#define HAL_NR_CPUS          ADEOS_NR_CPUS
#define HAL_NR_FAULTS        ADEOS_NR_FAULTS
#define HAL_NR_EVENTS        ADEOS_NR_EVENTS

#define HAL_APIC_HIGH_VECTOR  ADEOS_SERVICE_VECTOR3
#define HAL_APIC_LOW_VECTOR   ADEOS_SERVICE_VECTOR2

#define HAL_SCHEDULE_HEAD     ADEOS_SCHEDULE_HEAD
#define HAL_SCHEDULE_TAIL     ADEOS_SCHEDULE_TAIL
#define HAL_SYSCALL_PROLOGUE  ADEOS_SYSCALL_PROLOGUE
#define HAL_SYSCALL_EPILOGUE  ADEOS_SYSCALL_EPILOGUE
#define HAL_EXIT_PROCESS      ADEOS_EXIT_PROCESS
#define HAL_KICK_PROCESS      ADEOS_KICK_PROCESS

#define hal_domain_struct   adomain 
#define hal_root_domain     adp_root 
#define hal_current_domain  adp_cpu_current 

#define hal_critical_enter  adeos_critical_enter
#define hal_critical_exit   adeos_critical_exit

#define hal_clear_irq   __adeos_clear_irq
#define hal_lock_irq    __adeos_lock_irq
#define hal_unlock_irq  __adeos_unlock_irq

#define hal_std_irq_dtype        __adeos_std_irq_dtype
#define hal_adeos_std_irq_dtype  __adeos_std_irq_dtype

#define hal_tick_regs  __adeos_tick_regs
#define hal_tick_irq   __adeos_tick_irq

#define hal_sync_stage  __adeos_sync_stage

#define hal_set_irq_affinity  adeos_set_irq_affinity

#define hal_propagate_event  adeos_propagate_event

#define hal_get_sysinfo  adeos_get_sysinfo

#define FIRST_LINE_OF_RTAI_DOMAIN_ENTRY  static void rtai_domain_entry(int iflag) { if (iflag)
#define LAST_LINE_OF_RTAI_DOMAIN_ENTRY   }

#ifdef CONFIG_ADEOS_NOTHREADS
#define HAL_TYPE  "ADEOS-NOTHREADS"
#define hal_suspend_domain()  break
#else
#define HAL_TYPE  "ADEOS-THREADS"
#define hal_suspend_domain()  adeos_suspend_domain()
#endif

#define hal_alloc_irq       adeos_alloc_irq
#define hal_free_irq        adeos_free_irq
#define hal_virtualize_irq  adeos_virtualize_irq_from

#define hal_sysinfo_struct     adsysinfo
#define hal_attr_struct        adattr
#define hal_init_attr          adeos_init_attr
#define hal_register_domain    adeos_register_domain
#define hal_unregister_domain  adeos_unregister_domain
#define hal_catch_event        adeos_catch_event_from
#define hal_event_handler(e)   events[e].handler

#define hal_set_printk_sync   adeos_set_printk_sync
#define hal_set_printk_async  adeos_set_printk_async

#define hal_schedule_back_root  __adeos_schedule_back_root

#define hal_processor_id  adeos_processor_id

#define hal_hw_cli                adeos_hw_cli
#define hal_hw_sti                adeos_hw_sti
#define hal_hw_local_irq_save     adeos_hw_local_irq_save
#define hal_hw_local_irq_restore  adeos_hw_local_irq_restore
#define hal_hw_local_irq_flags    adeos_hw_local_irq_flags

#define hal_ack_system_irq  __adeos_ack_system_irq

#define hal_irq_handler  adeos_extern_irq_handler

#define hal_tskext  ptd

#endif
