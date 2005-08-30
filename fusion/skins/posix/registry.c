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

#include <posix/registry.h>
#include <posix/thread.h>

struct {
    pse51_node_t **node_buckets;
    unsigned buckets_count;

    pse51_desc_t **descs;
    unsigned maxfds;
    unsigned long *fdsmap;
    unsigned mapsz;
} pse51_reg;

#define PSE51_NODE_PARTIAL_INIT 1

int pse51_reg_pkg_init (unsigned buckets_count, unsigned maxfds)
{
    size_t size, mapsize;
    char *chunk;
    unsigned i;

    mapsize = maxfds / BITS_PER_LONG;
    if(maxfds % BITS_PER_LONG) ++mapsize;

    size = sizeof(pse51_node_t) * buckets_count +
        sizeof(pse51_desc_t) * maxfds +
        sizeof(long) * mapsize;

    chunk = (char *) xnmalloc(size);
    if(!chunk)
        return ENOMEM;

    pse51_reg.node_buckets = (pse51_node_t **) chunk;
    pse51_reg.buckets_count = buckets_count;
    for(i = 0; i < buckets_count; i++)
        pse51_reg.node_buckets[i] = NULL;

    chunk += sizeof(pse51_node_t) * buckets_count;
    pse51_reg.descs = (pse51_desc_t **) chunk;
    for(i = 0; i < maxfds; i++)
        pse51_reg.descs[i] = NULL;

    chunk += sizeof(pse51_desc_t) * maxfds;
    pse51_reg.fdsmap = (unsigned long *) chunk;
    pse51_reg.maxfds = maxfds;
    pse51_reg.mapsz = mapsize;

    /* Initialize fds map. Bit set means "descriptor free". */
    for(i = 0; i < maxfds / BITS_PER_LONG; i++)
        pse51_reg.fdsmap[i] = ~0;
    if(maxfds % BITS_PER_LONG)
        pse51_reg.fdsmap[mapsize-1] = (1 << (maxfds % BITS_PER_LONG)) - 1;

    return 0;
}

void pse51_reg_pkg_cleanup (void)
{
    unsigned i;
    for(i = 0; i < pse51_reg.maxfds; i++)
        if(pse51_reg.descs[i])
            {
#ifdef CONFIG_RTAI_OPT_DEBUG
            xnprintf("Posix descriptor %d was not destroyed, destroying now.\n",
                     i);
#endif /* CONFIG_RTAI_OPT_DEBUG */
            pse51_desc_destroy(pse51_reg.descs[i]);
            }

#ifdef CONFIG_RTAI_OPT_DEBUG
    for (i = 0; i < pse51_reg.buckets_count; i++)
        {
        pse51_node_t *node;
        for (node = pse51_reg.node_buckets[i]; node; node = node->next)
            xnprintf("Posix node \"%s\" was not unlinked, NOT unlinking now.\n",
                     node->name);
        }
#endif /* CONFIG_RTAI_OPT_DEBUG */

    xnfree(pse51_reg.node_buckets);
}


static unsigned pse51_reg_crunch (const char *key)
{
    unsigned h = 0, g;

#define HQON    24              /* Higher byte position */
#define HBYTE   0xf0000000      /* Higher nibble on */

    while (*key)
        {
        h = (h << 4) + *key++;
        if ((g = (h & HBYTE)) != 0)
            h = (h ^ (g >> HQON)) ^ g;
        }

    return h % pse51_reg.buckets_count;
}

static int pse51_node_lookup (pse51_node_t ***node_linkp,
                              const char *name,
                              unsigned long magic)
{
    pse51_node_t **node_link;

    if(strnlen(name, sizeof((*node_link)->name)) == sizeof((*node_link)->name))
        return ENAMETOOLONG;

    node_link = &pse51_reg.node_buckets[pse51_reg_crunch(name)];

    while(*node_link)
        {
        pse51_node_t *node = *node_link;

        if(!strncmp(node->name, name, PSE51_MAXNAME))
            {
            if (node->magic != magic)
                return EINVAL;
            break;
            }

        node_link = &node->next;
        }

    *node_linkp = node_link;
    return 0;
}

static void pse51_node_unbind(pse51_node_t *node)
{
    pse51_node_t **node_link;

    node_link = node->prev;
    *node_link = node->next;
    if(node->next)
        node->next->prev = node_link;
    node->prev = NULL;
    node->next = NULL;
}

int pse51_node_add (pse51_node_t *node,
                    const char *name,
                    unsigned magic)
{
    pse51_node_t **node_link;
    int err;

    err = pse51_node_lookup(&node_link, name, magic);

    if (err)
        return err;

    if (*node_link)
        return EEXIST;

    node->magic = magic;
    node->flags = 0;
    node->refcount = 1;
    node->completion_synch = NULL;

    /* Insertion in hash table. */
    node->next = NULL;
    node->prev = node_link;
    *node_link = node;
    strcpy(node->name, name);  /* name length is checked in
                                   pse51_node_lookup. */

    return 0;
}

int pse51_node_put(pse51_node_t *node)
{
    if (!pse51_node_ref_p(node))
        return EINVAL;

    --node->refcount;
    return 0;
}

int pse51_node_remove(pse51_node_t **nodep, const char *name, unsigned magic)
{
    pse51_node_t *node, **node_link;
    int err;

    err = pse51_node_lookup(&node_link, name, magic);

    if (err)
        return err;

    node = *node_link;

    if(!node)
        return ENOENT;

    *nodep = node;
    node->magic = ~node->magic;
    node->flags |= PSE51_NODE_REMOVED;
    pse51_node_unbind(node);
    return 0;
}

/* Look for a node and check the POSIX open flags. */
int pse51_node_get(pse51_node_t **nodep,
                   const char *name,
                   unsigned long magic,
                   long oflags)
{
    pse51_node_t *node, **node_link;
    int err;

    do 
        {
        err = pse51_node_lookup(&node_link, name, magic);

        if (err)
            return err;
        
        node = *node_link;

        if (node && (oflags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
            return EEXIST;

        if (!node && !(oflags & O_CREAT))
            return ENOENT;

        *nodep = node;

        if (!node)
            return 0;

        ++node->refcount;
    
        while (node->flags & PSE51_NODE_PARTIAL_INIT)
            {
            pthread_t cur;
        
            xnsynch_sleep_on(node->completion_synch, XN_INFINITE);
            
            cur = pse51_current_thread();
        
            if (xnthread_test_flags(&cur->threadbase, XNRMID))
                {
                err = EAGAIN;
                break;
                }

            if (xnthread_test_flags(&cur->threadbase, XNBREAK))
                {
                pse51_node_put(node);
                return EINTR;
                }
            }

        } while(err == EAGAIN);

    return err;
}

/* Add a partially built object. */
int pse51_node_add_start(pse51_node_t *node,
                         const char *name,
                         unsigned magic,
                         xnsynch_t *completion_synch)
{
    int err;
    
    err = pse51_node_add(node, name, magic);

    if(err)
        return err;

    xnsynch_init(completion_synch, XNSYNCH_PRIO);
    node->completion_synch = completion_synch;
    node->flags |= PSE51_NODE_PARTIAL_INIT;
    return 0;
}

void pse51_node_add_finished(pse51_node_t *node, int error)
{
    if (error) {
        node->refcount = 0;
        pse51_node_unbind(node);
    }

    if (xnsynch_flush(node->completion_synch,
                      error ? XNRMID : 0) == XNSYNCH_RESCHED)
        xnpod_schedule();

    node->flags &= ~PSE51_NODE_PARTIAL_INIT;
    node->completion_synch = NULL;
}

static int pse51_reg_fd_get (void)
{
    unsigned i;

    for(i = 0; i < pse51_reg.mapsz; i++)
        if(pse51_reg.fdsmap[i])
            {
            int fd  = ffnz(pse51_reg.fdsmap[i]);

            pse51_reg.fdsmap[i] &= ~(1 << fd);
            return fd + BITS_PER_LONG * i;
            }

    return -1;
}

static void pse51_reg_fd_put(int fd)
{
    unsigned i, bit;
    
    i = fd / BITS_PER_LONG;
    bit = 1 << (fd % BITS_PER_LONG);
    
    pse51_reg.fdsmap[i] |= bit;
    pse51_reg.descs[fd] = NULL;
}

static int pse51_reg_fd_lookup(pse51_desc_t **descp, int fd)
{
    unsigned i, bit;

    if(fd > pse51_reg.maxfds)
        return EBADF;

    i = fd / BITS_PER_LONG;
    bit = 1 << (fd % BITS_PER_LONG);
    
    if((pse51_reg.fdsmap[i] & bit))
        return EBADF;

    *descp = pse51_reg.descs[fd];
    return 0;
}


int pse51_desc_create(pse51_desc_t **descp, pse51_node_t *node)
{
    int fd = pse51_reg_fd_get();
    pse51_desc_t *desc;

    if (fd == -1)
        return ENFILE;

    desc = (pse51_desc_t *) xnmalloc(sizeof(*desc));

    if (!desc)
        return ENOMEM;

    pse51_reg.descs[fd] = desc;
    desc->node = node;
    desc->fd = fd;
    *descp = desc;
    return 0;
}


int pse51_desc_destroy(pse51_desc_t *desc)
{
    pse51_reg_fd_put(desc->fd);
    xnfree(desc);
    return 0;
}


int pse51_desc_get(pse51_desc_t **descp, int fd, unsigned magic)
{
    pse51_desc_t *desc;
    int err;

    err = pse51_reg_fd_lookup(&desc, fd);

    if (err)
        return err;

    if (desc->node->magic != magic)
        return EBADF;

    *descp = desc;
    return 0;
}

