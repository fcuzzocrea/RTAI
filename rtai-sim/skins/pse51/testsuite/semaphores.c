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

#include <pse51_test.h>

sem_t sem;
pthread_t child_tid;

void *child(void *cookie)
{
    int value;
    
    TEST_MARK();

    TEST_ASSERT(sem_getvalue(&sem, &value) == 0 && value == 0);

    TEST_ASSERT(sem_trywait(&sem) == -1 && errno == EAGAIN);
    
    TEST_ASSERT_OK(sem_wait(&sem));

    TEST_MARK();
    
    return cookie;
}


void *root_thread(void *cookie)
{
    pthread_attr_t attr;
    void *child_result;
    int value;
    
    TEST_START(0);

    memset(&sem, 0, sizeof(sem));

    TEST_ASSERT(sem_wait(&sem) == -1 && errno == EINVAL);

    TEST_ASSERT(sem_init(&sem, 1, 0) == -1 && errno == ENOSYS);

    TEST_ASSERT(sem_init(&sem, 0, -1) == -1 && errno == EINVAL);

    TEST_ASSERT_OK(sem_init(&sem, 0, 0));

    TEST_ASSERT_OK(pthread_attr_init(&attr));
    TEST_ASSERT_OK(pthread_attr_setname_np(&attr, "child"));
    TEST_ASSERT_OK(pthread_create(&child_tid, &attr, child, &attr));
    TEST_ASSERT_OK(pthread_attr_destroy(&attr));

    TEST_ASSERT_OK(sched_yield());

    TEST_MARK();

    TEST_ASSERT(sem_getvalue(&sem, &value) == 0 && value == -1);
    
    TEST_ASSERT_OK(sem_post(&sem));

    TEST_ASSERT_OK(pthread_join(child_tid, &child_result) &&
                   child_result == (void *) &attr);

    TEST_ASSERT(sem_getvalue(&sem, &value) == 0 && value == 0);
    
    TEST_MARK();

    TEST_ASSERT_OK(sem_destroy(&sem));

    TEST_ASSERT(sem_wait(&sem) == -1 && errno == EINVAL);

    TEST_ASSERT_OK(sem_init(&sem, 0, SEM_VALUE_MAX-1));

    TEST_ASSERT_OK(sem_post(&sem));

    TEST_ASSERT(sem_getvalue(&sem, &value) == 0 && value == SEM_VALUE_MAX);

    TEST_ASSERT(sem_post(&sem) == -1 && errno == EAGAIN);
    
    TEST_ASSERT(sem_getvalue(&sem, &value) == 0 && value == SEM_VALUE_MAX);

    TEST_CHECK_SEQUENCE(SEQ("child", 1),
                        SEQ("root", 1),
                        SEQ("child", 1),
                        SEQ("root", 1),
                        END_SEQ);

    TEST_FINISH();

    return NULL;
}
