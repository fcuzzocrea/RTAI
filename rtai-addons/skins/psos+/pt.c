/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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
#include "psos+/pt.h"

static xnqueue_t psosptq;

void psospt_init (void) {
    initq(&psosptq);
}

void psospt_cleanup (void) {

    xnholder_t *holder;

    while ((holder = getq(&psosptq)) != NULL)
	psos_mark_deleted(link2psospt(holder));
}

u_long pt_create (char name[4],
		  void *paddr,
		  void *laddr,	/* unused */
		  u_long psize,
		  u_long bsize,
		  u_long flags,
		  u_long *ptid,
		  u_long *nbuf)
{
    u_long bitmapsize;
    psospt_t *pt;
    char *mp;
    u_long n;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if ((u_long)paddr & (sizeof(u_long) - 1))
	return ERR_PTADDR;

    if (bsize <= pt_align_mask)
	return ERR_BUFSIZE;

    if (bsize & (bsize - 1))
	return ERR_BUFSIZE; /* Not a power of two. */

    if (psize < sizeof(psospt_t))
	return ERR_TINYPT;

    psize -= sizeof(psospt_t);
    pt = (psospt_t *)paddr;
    inith(&pt->link);

    pt->name[0] = name[0];
    pt->name[1] = name[1];
    pt->name[2] = name[2];
    pt->name[3] = name[3];
    pt->name[4] = '\0';
    pt->flags = flags;
    pt->bsize = (bsize + pt_align_mask) & ~pt_align_mask;

    bitmapsize = (psize * XN_NBBY) / (pt->bsize + XN_NBBY);
    bitmapsize = (bitmapsize + pt_align_mask) & ~pt_align_mask;

    if (bitmapsize <= pt_align_mask)
	return ERR_TINYPT;

    pt->nblks = (psize - bitmapsize) / pt->bsize;
    pt->psize = pt->nblks * pt->bsize;
    pt->data = (char *)pt->bitmap + bitmapsize;
    pt->freelist = mp = pt->data;
    pt->ublks = 0;
    
    for (n = pt->nblks; n > 1; n--)
	{
	char *nmp = mp + pt->bsize;
	*((void **)mp) = nmp;
	mp = nmp;
	}

    *((void **)mp) = NULL;

    for (n = 0; n<bitmapsize / sizeof(u_long); n++)
	pt->bitmap[n] = 0;
    
    pt->magic = PSOS_PT_MAGIC;
    xnmutex_lock(&__imutex);
    appendq(&psosptq,&pt->link);
    xnmutex_unlock(&__imutex);
    *nbuf = pt->nblks;
    *ptid = (u_long)pt;

    return SUCCESS;
}

u_long pt_delete (u_long ptid)

{
    psospt_t *pt;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);

    pt = psos_h2obj_active(ptid,PSOS_PT_MAGIC,psospt_t);

    if (!pt)
	{
	u_long err = psos_handle_error(ptid,PSOS_PT_MAGIC,psospt_t);
	xnmutex_unlock(&__imutex);
	return err;
	}

    if (!(pt->flags & PT_DEL) && pt->ublks > 0)
	{
	xnmutex_unlock(&__imutex);
	return ERR_BUFINUSE;
	}

    psos_mark_deleted(pt);
    removeq(&psosptq,&pt->link);

    xnmutex_unlock(&__imutex);

    return SUCCESS;
}

u_long pt_getbuf (u_long ptid,
		  void **bufaddr)

{
    u_long numblk;
    psospt_t *pt;
    void *buf;

    xnmutex_lock(&__imutex);

    pt = psos_h2obj_active(ptid,PSOS_PT_MAGIC,psospt_t);

    if (!pt)
	{
	u_long err = psos_handle_error(ptid,PSOS_PT_MAGIC,psospt_t);
	xnmutex_unlock(&__imutex);
	return err;
	}

    if ((buf = pt->freelist) != NULL)
	{
	pt->freelist = *((void **)buf);
	pt->ublks++;
	numblk = ((char *)buf - pt->data) / pt->bsize;
	pt_bitmap_setbit(pt,numblk);
	}

    xnmutex_unlock(&__imutex);

    *bufaddr = buf;

    return SUCCESS;
}

u_long pt_retbuf (u_long ptid,
		  void *buf)
{
    u_long numblk;
    psospt_t *pt;

    xnmutex_lock(&__imutex);

    pt = psos_h2obj_active(ptid,PSOS_PT_MAGIC,psospt_t);

    if (!pt)
	{
	u_long err = psos_handle_error(ptid,PSOS_PT_MAGIC,psospt_t);
	xnmutex_unlock(&__imutex);
	return err;
	}

    if ((char *)buf < pt->data ||
	(char *)buf >= pt->data + pt->psize ||
	(((char *)buf - pt->data) % pt->bsize) != 0)
	{
	xnmutex_unlock(&__imutex);
	return ERR_BUFADDR;
	}

    numblk = ((char *)buf - pt->data) / pt->bsize;

    if (!pt_bitmap_tstbit(pt,numblk))
	{
	xnmutex_unlock(&__imutex);
	return ERR_BUFFREE;
	}

    pt_bitmap_clrbit(pt,numblk);
    *((void **)buf) = pt->freelist;
    pt->freelist = buf;
    pt->ublks--;

    xnmutex_unlock(&__imutex);

    return SUCCESS;
}

u_long pt_ident (char name[4],
		 u_long node,
		 u_long *ptid)
{
    xnholder_t *holder;
    psospt_t *pt;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (node > 1)
	return ERR_NODENO;

    xnmutex_lock(&__imutex);

    for (holder = getheadq(&psosptq);
	 holder; holder = nextq(&psosptq,holder))
	{
	pt = link2psospt(holder);

	if (pt->name[0] == name[0] &&
	    pt->name[1] == name[1] &&
	    pt->name[2] == name[2] &&
	    pt->name[3] == name[3])
	    {
	    *ptid = (u_long)pt;
	    xnmutex_unlock(&__imutex);
	    return SUCCESS;
	    }
	}

    xnmutex_unlock(&__imutex);

    return ERR_OBJNF;
}

/*
 * IMPLEMENTATION NOTES:
 *
 * - A partition memory layout is as follows:
 *
 *   struct psospt {
 *      Partition's superblock
 *      (char *data => pointer to the user data area)
 *      (u_long bitmap[1] => first word of bitmap)
 *   }
 *   [...block status bitmap (busy/free)...]
 *   [...user data area...]
 *
 * - Each free block starts with a link to the next free block
 * in the partition's free list. A NULL link ends this list.
 */
