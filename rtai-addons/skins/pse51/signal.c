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


#include "pse51/signal.h"

static struct sigaction actions [PSE51_SIGMAX-PSE51_SIGMIN+1];

static void pse51_dispatch_signals(xnsigmask_t sigs);

#define user2pse51_sigset(set) ((pse51_sigset_t *)(set))


static inline void emptyset(pse51_sigset_t *set)
{
    *set=0;
}

static inline void fillset(pse51_sigset_t *set)
{
    *set=~0;
}

static inline void addset(pse51_sigset_t *set, int sig)
{
    *set |= 1<<(sig-PSE51_SIGMIN);
}

static inline void delset(pse51_sigset_t *set, int sig)
{
    *set &= ~(1<<(sig-PSE51_SIGMIN));
}

static inline int ismember(const pse51_sigset_t *set, int sig)
{
    return *set & (1<<(sig-PSE51_SIGMIN));
}



/* The sig*set functions are in the list of signal-async safe functions, hence
   the critical sections. */

int sigemptyset(sigset_t *user_set)
{
    pse51_sigset_t *set=user2pse51_sigset(user_set);
    
    xnmutex_lock(&__imutex);
    emptyset(set);
    xnmutex_unlock(&__imutex);

    return 0;
}



int sigfillset(sigset_t *user_set)
{
    pse51_sigset_t *set=user2pse51_sigset(user_set);
    
    xnmutex_lock(&__imutex);
    fillset(set);
    xnmutex_unlock(&__imutex);

    return 0;
}



int sigaddset(sigset_t *user_set, int sig)
{
    pse51_sigset_t *set=user2pse51_sigset(user_set);
    
    if(sig<PSE51_SIGMIN || sig>PSE51_SIGMAX) {
        thread_errno()=EINVAL;
        return -1;
    }

    xnmutex_lock(&__imutex);
    addset(set, sig);
    xnmutex_unlock(&__imutex);

    return 0;
}



int sigdelset(sigset_t *user_set, int sig)
{
    pse51_sigset_t *set=user2pse51_sigset(user_set);
    
    if(sig<PSE51_SIGMIN || sig>PSE51_SIGMAX) {
        thread_errno()=EINVAL;
        return -1;
    }

    xnmutex_lock(&__imutex);
    delset(set, sig);
    xnmutex_unlock(&__imutex);

    return 0;
}



int sigismember(const sigset_t *user_set, int sig)
{
    pse51_sigset_t *set=user2pse51_sigset(user_set);    
    int result;
    
    if(sig<PSE51_SIGMIN || sig>PSE51_SIGMAX) {
        thread_errno()=EINVAL;
        return -1;
    }

    /* sigismember is required to return 1, not just non-zero. */
    xnmutex_lock(&__imutex);
    result=ismember(set,sig) ? 1 : 0;
    xnmutex_unlock(&__imutex);

    return result;
}



int sigaction(int sig, const struct sigaction *action, struct sigaction *old)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if(sig<PSE51_SIGMIN || sig>PSE51_SIGMAX || (!action && !old)) {
        thread_errno()=EINVAL;
        return -1;
    }

    if(action && (action->sa_flags & ~SIGACTION_FLAGS)) {
        thread_errno()=ENOTSUP;
        return -1;
    }

    xnmutex_lock(&__imutex);
    if(old)
        *old = actions[sig-PSE51_SIGMIN];
    if(action) {
        actions[sig-PSE51_SIGMIN] = *action;
        if(!(action->sa_flags & SA_NOMASK))
            addset(user2pse51_sigset(&actions[sig-PSE51_SIGMIN].sa_mask), sig);
    }
    xnmutex_unlock(&__imutex);

    return 0;
}



int pthread_sigmask(int how, const sigset_t *user_set, sigset_t *user_oset)
{
    pse51_sigset_t *set=user2pse51_sigset(user_set);
    pse51_sigset_t *oset=user2pse51_sigset(user_oset);
    pthread_t cur;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);
    cur = pse51_current_thread();
    if(oset)
        *oset = cur->sigmask;
    if(set) {
        if(cur->threadbase.signals)
            xnpod_schedule(NULL);   /* In order to deliver any pending signal
                                       which is going to be blocked. */
        switch(how) {
        case SIG_BLOCK:
            cur->sigmask |= *set;
            break;
        case SIG_UNBLOCK:
            /* Mark as pending any signal which was received while blocked and
               is going to be unblocked. */
            cur->threadbase.signals |= (*set & cur->blocked_received);
            cur->sigmask &= ~*set;
            break;
        case SIG_SETMASK:
            cur->threadbase.signals |= (~*set & cur->blocked_received);
            cur->sigmask = *set;
            break;
        default:
            thread_errno()=EINVAL;
            xnmutex_unlock(&__imutex);
            return -1;
        }
        
        /* Handle any unblocked signal. */
        if(cur->threadbase.signals) {
            cur->blocked_received &= ~cur->threadbase.signals;
            xnpod_schedule(NULL);
        }
    }
    xnmutex_unlock(&__imutex);

    return 0;
}



int pthread_kill(pthread_t thread, int sig)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    /* sig == 0 has a special meaning: pthread_kill returns 0 if the thread
       is exists... Nothing else happens. */
    if(sig && (sig<PSE51_SIGMIN || sig>PSE51_SIGMAX)) {
        return EINVAL;
    }

    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(thread, PSE51_THREAD_MAGIC,struct pse51_thread)) {
        xnmutex_unlock(&__imutex);
        return ESRCH;
    }

    if(sig) {
        if(ismember(&thread->sigmask, sig))
            addset(&thread->blocked_received, sig);
        else
            addset(&thread->threadbase.signals, sig);

        if(thread != pse51_current_thread())
            xnpod_unblock_thread(&thread->threadbase);
        xnpod_schedule(&__imutex);
    }
    xnmutex_unlock(&__imutex);

    return 0;
}



int sigpending(sigset_t *user_set)
{
    pse51_sigset_t *set=user2pse51_sigset(user_set);
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if(!set) {
        thread_errno()=EINVAL;
        return -1;
    }

    memcpy(set,&pse51_current_thread()->blocked_received,sizeof(*set));

    return 0;
}



int sigwait(const sigset_t *user_set, int *sig)
{
    pse51_sigset_t received, *set=user2pse51_sigset(user_set);    
    pthread_t thread;
    int i;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if(!set)
        return EINVAL;
    
    thread = pse51_current_thread();

    /* All signals in "set" must be blocked in order for sigwait to work
       reliably. */
    if(*set & ~thread->sigmask)
        return EINVAL;

    xnmutex_lock(&__imutex);
    while(!(received=*set & thread->blocked_received)) {
        xnpod_suspend_thread(&thread->threadbase, XNDELAY, XN_INFINITE, NULL,
                             &__imutex);
        thread_cancellation_point(thread, &__imutex);
    }
    for(i=PSE51_SIGMIN; i<=PSE51_SIGMAX; i++)
        if(ismember(&received, i))
            break;

    delset(&thread->blocked_received, i);
    xnmutex_unlock(&__imutex);
    *sig = i;
    return 0;
}

void pse51_signal_init_thread(pthread_t newthread, const pthread_t parent)
{
    /* parent may be NULL if pthread_create is not called from a pse51 thread. */
    emptyset(&newthread->blocked_received);
    if(parent)
        newthread->sigmask = parent->sigmask;
    else
        emptyset(&newthread->sigmask);
    newthread->threadbase.asr = pse51_dispatch_signals;
}



void pse51_signal_init(void)
{
    int i;

    for(i=PSE51_SIGMIN; i<PSE51_SIGMAX; i++) {
        actions[i].sa_handler=SIG_DFL;
        emptyset(user2pse51_sigset(&actions[i].sa_mask));
        actions[i].sa_flags=0;
    }
}



void pse51_default_handler(int sig) 
{
    pthread_t cur = pse51_current_thread();
    
    xnpod_fatal("Thread %s received unhandled signal %d.\n",
                thread_name(cur), sig);
}



static void pse51_dispatch_signals(xnsigmask_t sigs)
{
    int sig;
    pthread_t thread;
    pse51_sigset_t saved_mask;

    thread = pse51_current_thread();
    saved_mask = thread->sigmask;
    
    xnmutex_lock(&__imutex);
    for(sig=PSE51_SIGMIN; sig<=PSE51_SIGMAX; sig++) {
        if(ismember(&sigs, sig)) {
            struct sigaction *action;
            action = &actions[sig-PSE51_SIGMIN];
            if(action->sa_handler == SIG_IGN)
                continue;

            thread->sigmask=*user2pse51_sigset(&action->sa_mask);
            if(action->sa_flags & SA_ONESHOT) {
                action->sa_handler(sig);
                action->sa_handler = SIG_DFL;
            } else
                action->sa_handler(sig);                
        }
    }
    xnmutex_unlock(&__imutex);
    thread->sigmask = saved_mask;
}

