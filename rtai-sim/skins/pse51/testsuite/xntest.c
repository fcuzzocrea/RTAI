/*
 * This file is part of the XENOMAI project.
 *
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 *
 * VxWorks is a registered trademark of Wind River Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of either the GNU General Public License
 * or the Clarified Artistic License, as specified in the PACKAGE_LICENSE
 * file.
 *
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
 */

#include <xenomai/xenomai.h>
#include "xntest.h"
#include <stdarg.h>

typedef struct xntest_mark
{
    char *threadname;
    int count;
    xnholder_t link;

#define link2mark(laddr)                                                        \
((xntest_mark_t *)(((char *)laddr) - (int)(&((xntest_mark_t *)0)->link)))

} xntest_mark_t;


typedef void (*xntimer_handler) (void *);



static xnqueue_t marks_q;
static xnmutex_t test_mutex;
static xntimer_t watchdog;
static int test_failures;
static int tests;



static inline xnholder_t *gettailq (xnqueue_t *qslot) {
    xnholder_t *holder = qslot->head.last;
    if (holder == &qslot->head) return NULL;
    return holder;
}

#define test_timeout 1500

static inline int strings_differ(const char *str1, const char *str2)
{
    return ((!str1 || !str2) ? str1!=str2 : strcmp(str1, str2));
}

static void interrupt_test (void *dummy)
{
   xnpod_fatal("test interrupted by watchdog.\n");
}



void xntest_start(void)
{
    spl_t s;

    splhigh(s);
    xntimer_init(&watchdog, interrupt_test, 0);
    xntimer_start(&watchdog, xnpod_ticks2time(test_timeout), XN_INFINITE);

    xnmutex_init(&test_mutex);
    initq(&marks_q);
    tests=0;
    test_failures=0;
    splexit(s);
}



int xntest_assert(int status, char *assertion, char *file, int line)
{
    xnmutex_lock(&test_mutex);
    ++tests;
    if(!status) {
        ++test_failures;
        xnarch_printf("%s:%d: TEST %s failed.\n", file, line, assertion);
    } else
        xnarch_printf("%s:%d TEST passed.\n", file, line);
    xnmutex_unlock(&test_mutex);

    return status;
}

void xntest_mark(xnthread_t *thread)
{
    xnholder_t *holder;
    xntest_mark_t *mark;
    const char *threadname;

    xnmutex_lock(&test_mutex);
    holder = gettailq(&marks_q);
    threadname = xnthread_name(thread);

    if(!holder ||
       strings_differ(threadname, (mark=link2mark(holder))->threadname)) {
        size_t namelen = threadname ? strlen(threadname)+1: 0;
        mark = (xntest_mark_t *) xnmalloc(sizeof(xntest_mark_t)+namelen);
        mark->threadname=(threadname
                          ? (char *) mark + sizeof(xntest_mark_t)
                          : NULL);
        if(mark->threadname)
            memcpy(mark->threadname, threadname, namelen);
        
        mark->count = 1;
        inith(&mark->link);
        appendq(&marks_q, &mark->link);
    } else
        mark->count++;
    xnmutex_unlock(&test_mutex);
}



void xntest_check_seq(int next, ...)
{
    char *name;
    int count;
    va_list args;

    xnholder_t *holder;
    xntest_mark_t *mark;

    va_start(args, next);

    xnmutex_lock(&test_mutex);
    holder = getheadq(&marks_q);

    while(next) {
        name = va_arg(args,char *);
        count = va_arg(args,int);
        ++tests;
        if(holder == NULL) {
            xnarch_printf("Expected sequence: SEQ(\"%s\",%d); "
                          "reached end of recorded sequence.\n", name, count);
            ++test_failures;
        } else {
            mark = link2mark(holder);

            if(strings_differ(mark->threadname, name) || mark->count != count ) {
                xnarch_printf("Expected sequence: SEQ(\"%s\",%d); "
                              "got SEQ(\"%s\",%d)\n",
                              name, count, mark->threadname, mark->count);
                ++test_failures;
            } else
                xnarch_printf("Correct sequence: SEQ(\"%s\",%d)\n", name, count);

            holder = nextq(&marks_q, holder);
        }
        next = va_arg(args, int);
    }
    xnmutex_unlock(&test_mutex);
    va_end(args);
}



void xntest_finish(char *file, int line)
{
    xnholder_t *holder;
    xnholder_t *next_holder;
    
    xnmutex_lock(&test_mutex);
    for(holder = getheadq(&marks_q); holder ; holder=next_holder)
    {
        next_holder = nextq(&marks_q, holder);
        removeq(&marks_q, holder);
        xnfree(link2mark(holder));
    }
    xnmutex_unlock(&test_mutex);

    xnarch_printf("%s:%d, test finished: %d failures/ %d tests\n",
                  file, line, test_failures, tests);
    xnpod_fatal("Normal exit.\n");
}
