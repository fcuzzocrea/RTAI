/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
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


#include "pse51/internal.h"

static const pthread_attr_t default_thread_attr = {
    magic: PSE51_THREAD_ATTR_MAGIC,
    detachstate: PTHREAD_CREATE_JOINABLE,
    stacksize: PTHREAD_STACK_MIN,
    inheritsched: PTHREAD_EXPLICIT_SCHED,
    policy: SCHED_RR,
    schedparam: {
        sched_priority: PSE51_MIN_PRIORITY
    },

    name: NULL,
    fp: 1,
};

int pthread_attr_init(pthread_attr_t *attr)
{
    if(!attr)
        return ENOMEM;

    xnmutex_lock(&__imutex);
    if(pse51_obj_busy(attr)) {
        xnmutex_unlock(&__imutex);
        return EBUSY;
    }

    *attr=default_thread_attr;
    xnmutex_unlock(&__imutex);

    return 0;
}


int pthread_attr_destroy(pthread_attr_t *attr)
{
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    if(attr->name)
        xnfree(attr->name);
    pse51_mark_deleted(attr);
    xnmutex_unlock(&__imutex);

    return 0;
}


int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate)
{
    if(!detachstate)
        return EINVAL;
        
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    *detachstate = attr->detachstate;
    xnmutex_unlock(&__imutex);

    return 0;
}


int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate)
{
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    attr->detachstate = detachstate;
    xnmutex_unlock(&__imutex);
       
    return 0;
}


int pthread_attr_getstackaddr(const pthread_attr_t *attr, void **stackaddr)
{
    if(!stackaddr)
        return EINVAL;

    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }
    xnmutex_unlock(&__imutex);

    return ENOSYS;
}


int pthread_attr_setstackaddr(pthread_attr_t *attr, void *stackaddr)
{
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }
    xnmutex_unlock(&__imutex);

    return ENOSYS;
}


int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize)
{
    if(!stacksize)
        return EINVAL;

    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    *stacksize = attr->stacksize;
    xnmutex_unlock(&__imutex);

    return 0;
}


int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)
{
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    if(stacksize < PTHREAD_STACK_MIN) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    attr->stacksize = stacksize;
    xnmutex_unlock(&__imutex);

    return 0;
}


int pthread_attr_getinheritsched(const pthread_attr_t *attr,int *inheritsched)
{
    if(!inheritsched)
        return EINVAL;

    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    *inheritsched = attr->inheritsched;

    return 0;
}


int pthread_attr_setinheritsched(pthread_attr_t *attr, int inheritsched)
{
    switch(inheritsched) {
    default:
        return EINVAL;
    case PTHREAD_INHERIT_SCHED:
    case PTHREAD_EXPLICIT_SCHED:
        break;
    }

    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }
    attr->inheritsched = inheritsched;
    xnmutex_unlock(&__imutex);
    
    return 0;
}


int pthread_attr_getschedpolicy(const pthread_attr_t *attr,int *policy)
{
    if(!policy)
        return EINVAL;
    
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    *policy = attr->policy;
    xnmutex_unlock(&__imutex);

    return 0;
}


int pthread_attr_setschedpolicy(pthread_attr_t *attr, int policy)
{
    switch(policy) {
    default:
        return EINVAL;
    case SCHED_OTHER:
        policy = SCHED_RR;
    case SCHED_FIFO:
    case SCHED_RR:
        break;
    }

    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }
    attr->policy = policy;
    xnmutex_unlock(&__imutex);

    return 0;
}


int
pthread_attr_getschedparam(const pthread_attr_t *attr, struct sched_param *par)
{
    if(!par)
        return EINVAL;
    
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    *par = attr->schedparam;
    xnmutex_unlock(&__imutex);

    return 0;
}


int
pthread_attr_setschedparam(pthread_attr_t *attr, const struct sched_param *par)
{
    if(!par)
        return EINVAL;

    if(par->sched_priority < PSE51_MIN_PRIORITY
       || par->sched_priority > PSE51_MAX_PRIORITY )
        return EINVAL;

    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }
    attr->schedparam = *par;
    xnmutex_unlock(&__imutex);

    return 0;
}


int pthread_attr_getscope(const pthread_attr_t *attr,int *scope)
{
    if(!scope)
        return EINVAL;
    
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }
    xnmutex_unlock(&__imutex);

    *scope = PTHREAD_SCOPE_SYSTEM;

    return 0;
}


int pthread_attr_setscope(pthread_attr_t *attr,int scope)
{
    if(scope != PTHREAD_SCOPE_SYSTEM)
        return ENOTSUP;

    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }
    xnmutex_unlock(&__imutex);

    return 0;
}


int pthread_attr_getname_np(const pthread_attr_t *attr, const char **name)
{
    if(!name)
        return EINVAL;
    
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    *name = attr->name;
    xnmutex_unlock(&__imutex);

    return 0;
}


int pthread_attr_setname_np(pthread_attr_t *attr, const char *name)
{
    if(!name)
        return EINVAL;
    
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    if(attr->name)
        xnfree(attr->name);
    if((attr->name = xnmalloc(strlen(name)+1)))
        strcpy(attr->name, name);
    
    xnmutex_unlock(&__imutex);

    return 0;
}


int pthread_attr_getfp_np(const pthread_attr_t *attr, int *fp)
{
    if(!fp)
        return EINVAL;
    
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    *fp = attr->fp;
    xnmutex_unlock(&__imutex);

    return 0;
}


int pthread_attr_setfp_np(pthread_attr_t *attr, int fp)
{
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_THREAD_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    attr->fp = fp;
    xnmutex_unlock(&__imutex);

    return 0;
}
