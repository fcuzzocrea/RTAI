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

static const pthread_mutexattr_t default_mutex_attr = {
    magic: PSE51_MUTEX_ATTR_MAGIC,
    type: PTHREAD_MUTEX_NORMAL,
    protocol: PTHREAD_PRIO_NONE
};


int pthread_mutexattr_init(pthread_mutexattr_t * attr)
{
    if(!attr)
        return ENOMEM;

    xnmutex_lock(&__imutex);
    if(pse51_obj_busy(attr)) {
        xnmutex_unlock(&__imutex);
        return EBUSY;
    }

    *attr=default_mutex_attr;
    xnmutex_unlock(&__imutex);

    return 0;    
}


int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_MUTEX_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }
    
    pse51_mark_deleted(attr);
    xnmutex_unlock(&__imutex);

    return 0;
}


int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type)
{
    if(!type)
        return EINVAL;

    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_MUTEX_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    *type = attr->type;
    xnmutex_unlock(&__imutex);

    return 0;    
}


int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_MUTEX_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    switch(type) {
    default:
        xnmutex_unlock(&__imutex);
        return EINVAL;
    case PTHREAD_MUTEX_DEFAULT:
        type=PTHREAD_MUTEX_NORMAL;
    case PTHREAD_MUTEX_NORMAL:
    case PTHREAD_MUTEX_RECURSIVE:
    case PTHREAD_MUTEX_ERRORCHECK:
        break;
    }
    
    attr->type = type;
    xnmutex_unlock(&__imutex);

    return 0;
}


int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *attr, int *proto)
{
    if(!proto)
        return EINVAL;
    
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_MUTEX_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    *proto = attr->protocol;
    xnmutex_unlock(&__imutex);

    return 0;
}


int pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr, int proto)
{
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(attr, PSE51_MUTEX_ATTR_MAGIC, pthread_attr_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    switch(proto) {
    default:
        xnmutex_unlock(&__imutex);
        return EINVAL;
    case PTHREAD_PRIO_PROTECT:
        xnmutex_unlock(&__imutex);
        return ENOTSUP;
    case PTHREAD_PRIO_NONE:
    case PTHREAD_PRIO_INHERIT:
        break;
    }
    
    attr->protocol = proto;
    xnmutex_unlock(&__imutex);

    return 0;
}
