/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * As a special exception, the RTAI project gives permission
 * for additional uses of the text contained in its release of
 * Xenomai.
 *
 * The exception is that, if you link the Xenomai libraries with other
 * files to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the Xenomai libraries code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public
 * License.
 *
 * This exception applies only to the code released by the
 * RTAI project under the name Xenomai.  If you copy code from other
 * RTAI project releases into a copy of Xenomai, as the General Public
 * License permits, the exception does not apply to the code that you
 * add in this way.  To avoid misleading anyone as to the status of
 * such modified files, you must delete this exception notice from
 * them.
 *
 * If you write modifications of your own for Xenomai, it is your
 * choice whether to permit this exception to apply to your
 * modifications. If you do not wish that, delete this exception
 * notice.
 */

#ifndef _vxworks_defs_h
#define _vxworks_defs_h

#include "xenomai/xenomai.h"
#include "rtai_vxworks.h"

#define WIND_MAGIC(n) (0x8383##n##n)
#define WIND_TASK_MAGIC WIND_MAGIC(01)
#define WIND_SEM_MAGIC  WIND_MAGIC(02)
#define WIND_WD_MAGIC   WIND_MAGIC(03)
#define WIND_MSGQ_MAGIC WIND_MAGIC(04)

/* Given a handle 'h', return a pointer to the control block of an
   object of type 't' whose magic word should be 'm'. */

#define wind_h2obj_active(h,m,t) \
((h) && ((t *)(h))->magic == (m) ? ((t *)(h)) : NULL)

/* Same as previously, but check for a deleted object, just returning
   a boolean value since the object would not be accessible if
   destroyed. The following test will remain valid until the destroyed
   object memory has been recycled for another usage. */

#define wind_mark_deleted(t) ((t)->magic = 0)

typedef struct wind_task wind_task_t;

#define IS_WIND_TASK XNTHREAD_SPARE0


#define wind_current_task() (thread2wind_task(xnpod_current_thread()))


/* FIXME: handle errno in isrs */
#define wind_errnoset(value) do                                         \
{                                                                       \
    if(!xnpod_asynch_p() &&                                             \
       xnthread_test_flags(xnpod_current_thread(), IS_WIND_TASK))       \
        wind_current_task()->errorStatus = value;                       \
} while(0)


#define error_check(cond, status, action) do    \
{                                               \
    if( (cond) )                                \
    {                                           \
        wind_errnoset(status);                  \
        action;                                 \
    }                                           \
} while (0)


#define check_NOT_ISR_CALLABLE(action) do               \
{                                                       \
    if(xnpod_asynch_p())                                \
    {                                                   \
        wind_errnoset(S_intLib_NOT_ISR_CALLABLE);       \
        action;                                         \
    }                                                   \
} while(0)


#define check_alloc(type, ptr, action) do               \
{                                                       \
    ptr = (type *) xnmalloc (sizeof(type));             \
    if(!ptr)                                            \
    {                                                   \
        wind_errnoset(S_memLib_NOT_ENOUGH_MEMORY);      \
        action;                                         \
    }                                                   \
} while(0)


#define check_OBJ_ID_ERROR(id,type,ptr,magic,action) do \
{                                                       \
    ptr = wind_h2obj_active(id, magic, type);           \
    if(!ptr)                                            \
    {                                                   \
        wind_errnoset(S_objLib_OBJ_ID_ERROR);           \
        action;                                         \
    }                                                   \
} while(0)




/* modules initialization and cleanup: */
#ifdef __cplusplus
extern "C" {
#endif

    int wind_sysclk_init(u_long init_ticks);

    void wind_sysclk_cleanup(void);
    

    void wind_task_init(void);

    void wind_task_cleanup(void);


    void wind_task_hooks_init(void);
    
    void wind_task_hooks_cleanup(void);


    void wind_sem_init(void);

    void wind_sem_cleanup(void);


    void wind_wd_init(void);

    void wind_wd_cleanup(void);


    void wind_msgq_init(void);

    void wind_msgq_cleanup(void);


    void wind_set_rrperiod( xnticks_t ticks );

    
#ifdef __cplusplus
}
#endif

extern xnmutex_t __imutex;

#endif /* !_vxworks_defs_h */
