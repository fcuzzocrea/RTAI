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


#include "pse51/thread.h"
#include "pse51/tsd.h"

typedef void (*pse51_key_destructor_t)(void *);

struct pse51_key {
    unsigned magic;
    unsigned key;
    pse51_key_destructor_t destructor;

    xnholder_t link;            /* link in the list of free keys or
                                   destructors. */

#define link2key(laddr) (!laddr ? NULL : \
((pthread_key_t) (((void *)laddr) - (int)(&((pthread_key_t)0)->link))))
};

static xnqueue_t free_keys, valid_keys;
static unsigned allocated_keys;

int pthread_key_create(pthread_key_t *key, pse51_key_destructor_t destructor)
{
    pthread_key_t result;
    xnholder_t *holder;

    xnmutex_lock(&__imutex);
    if(!key) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    if(allocated_keys == PTHREAD_KEYS_MAX) {
        if(!(result = link2key(getq(&free_keys)))) {
            xnmutex_unlock(&__imutex);
            return EAGAIN;
        }

        /* We are reusing a deleted key, we hence need to make sure that the
           values previously associated with this key are NULL. */
        for(holder=getheadq(&pse51_threadsq); holder;
            holder=nextq(&pse51_threadsq, holder))
            thread_settsd(link2pthread(holder), result->key, NULL);
    } else {
        if(!(result=xnmalloc(sizeof(*result)))) {
            xnmutex_unlock(&__imutex);
            return ENOMEM;
        }

        result->key = allocated_keys++;
    }

    result->magic = PSE51_KEY_MAGIC;
    result->destructor = destructor;
    inith(&result->link);
    prependq(&valid_keys, &result->link);
    xnmutex_unlock(&__imutex);

    *key = result;
    return 0;
}

int pthread_setspecific(pthread_key_t key, const void *value)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(key, PSE51_KEY_MAGIC, struct pse51_key)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }
    xnmutex_unlock(&__imutex);

    thread_settsd(pse51_current_thread(), key->key, value);
    
    return 0;
}

void *pthread_getspecific(pthread_key_t key)
{
    const void *value;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(key, PSE51_KEY_MAGIC, struct pse51_key)) {
        xnmutex_unlock(&__imutex);
        return NULL;
    }
    xnmutex_unlock(&__imutex);
    
    value=thread_gettsd(pse51_current_thread(), key->key);
    
    return (void *) value;
}

int pthread_key_delete(pthread_key_t key)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(key, PSE51_KEY_MAGIC, struct pse51_key)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    pse51_mark_deleted(key);
    removeq(&valid_keys, &key->link);
    inith(&key->link);
    appendq(&free_keys, &key->link);
    xnmutex_unlock(&__imutex);

    return 0;
}

void pse51_tsd_init_thread(pthread_t thread)
{
    unsigned key;
    
    for(key=0; key<PTHREAD_KEYS_MAX; key++)
        thread_settsd(thread, key, NULL);
}

void pse51_tsd_cleanup_thread(pthread_t thread)
{
    int i, again=1;

    xnmutex_lock(&__imutex);
    for(i=0; again && i<PTHREAD_DESTRUCTOR_ITERATIONS; i++) {
        xnholder_t *holder=getheadq(&valid_keys);
        again = 0;
        while(holder) {
            pthread_key_t key = link2key(holder);
            const void *value;

            if(!pse51_obj_active(key, PSE51_KEY_MAGIC, struct pse51_key)) {
                /* A destructor deleted this key. */
                again=1;
                break;
            }

            holder=nextq(&valid_keys, holder);

            if((value=thread_gettsd(thread, key->key))) {
               thread_settsd(thread, key->key, NULL);
               if(key->destructor) {
                   again = 1;
                   key->destructor((void *) value);
               }
            }
        }
    };
    xnmutex_unlock(&__imutex);
}

void pse51_tsd_init(void)
{
    initq(&free_keys);
    initq(&valid_keys);    
}

void pse51_tsd_cleanup(void)
{
    pthread_key_t key;

    while((key=link2key(getq(&valid_keys)))) {
        pse51_mark_deleted(key);
        xnfree(key);
    }

    while((key=link2key(getq(&free_keys))))
        xnfree(key);
}
