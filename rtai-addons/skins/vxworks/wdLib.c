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

#include "rtai_config.h"
#include "vxworks/defs.h"

#define WIND_WD_INITIALIZED XNTIMER_SPARE0

typedef struct wind_wd {

    unsigned magic;   /* Magic code - must be first */

    xnholder_t link;

#define link2wind_wd(laddr) \
((wind_wd_t *)(((char *)laddr) - (int)(&((wind_wd_t *)0)->link)))

    xntimer_t timerbase;

} wind_wd_t;

typedef void (* xntimer_handler) (void *);


static xnqueue_t wind_wd_q;


static void wd_destroy_internal(wind_wd_t *wd);




void wind_wd_init (void)
{
    initq(&wind_wd_q);
}


void wind_wd_cleanup (void)
{
    xnholder_t *holder;

    while ((holder = getheadq(&wind_wd_q)) != NULL)
	wd_destroy_internal(link2wind_wd(holder));
}




int wdCreate (void)
{
    wind_wd_t *wd;

    check_alloc(wind_wd_t, wd, return 0);

    inith(&wd->link);
    wd->magic = WIND_WD_MAGIC;

    setbits(wd->timerbase.status, WIND_WD_INITIALIZED);
    
    xnmutex_lock(&__imutex);
    appendq(&wind_wd_q,&wd->link);
    xnmutex_unlock(&__imutex);

    return (int) wd;
}



STATUS wdDelete (int handle)
{
    wind_wd_t *wd;
    /*    xnpod_check_context(XNPOD_THREAD_CONTEXT); */

    xnmutex_lock(&__imutex);
    check_OBJ_ID_ERROR(handle, wind_wd_t, wd, WIND_WD_MAGIC, goto error);
    wd_destroy_internal(wd);
    xnmutex_unlock(&__imutex);
    return OK;

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}



STATUS wdStart ( int handle,
                 int timeout,
                 wind_timer_t handler,
                 int arg )
{
    wind_wd_t *wd;

    if (!handler)
	return ERROR;

    xnmutex_lock(&__imutex);

    check_OBJ_ID_ERROR(handle, wind_wd_t, wd, WIND_WD_MAGIC, goto error);

    if(testbits(wd->timerbase.status, WIND_WD_INITIALIZED))
        clrbits(wd->timerbase.status, WIND_WD_INITIALIZED);
    else
        if(xntimer_active_p(&wd->timerbase))
            xntimer_stop(&wd->timerbase);
    
    xntimer_init(&wd->timerbase, (xntimer_handler) handler, (void *) arg);
    
    xntimer_start(&wd->timerbase,timeout,XN_INFINITE);

    xnmutex_unlock(&__imutex);
    return OK;

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}



STATUS wdCancel ( int handle )
{
    wind_wd_t *wd;
    
    xnmutex_lock(&__imutex);
    check_OBJ_ID_ERROR(handle, wind_wd_t, wd, WIND_WD_MAGIC, goto error);
    if(xntimer_active_p(&wd->timerbase))
        xntimer_stop(&wd->timerbase);
    xnmutex_unlock(&__imutex);

    return OK;

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}




static void wd_destroy_internal (wind_wd_t * handle)
{
    xnmutex_lock(&__imutex);
    if(testbits(handle->timerbase.status, XNTIMER_ENABLED))
        xntimer_destroy(&handle->timerbase);
    removeq(&wind_wd_q,&handle->link);
    wind_mark_deleted(handle);
    xnmutex_unlock(&__imutex);

    xnfree(handle);
}
