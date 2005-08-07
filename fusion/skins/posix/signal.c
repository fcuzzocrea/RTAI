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

#include <posix/signal.h>

static struct sigaction actions[PSE51_SIGMAX - PSE51_SIGMIN + 1];

#define user2pse51_sigset(set) ((pse51_sigset_t *)(set))

static inline void emptyset (pse51_sigset_t *set) {

    clrbits(*set, ~0);
}

static inline void fillset (pse51_sigset_t *set) {

    setbits(*set, ~0);
}

static inline void addset (pse51_sigset_t *set, int sig) {

    setbits(*set, (1 << (sig - PSE51_SIGMIN)));
}

static inline void delset (pse51_sigset_t *set, int sig) {

    clrbits(*set, (1 << (sig - PSE51_SIGMIN)));
}

static inline int ismember (const pse51_sigset_t *set, int sig) {

    return testbits(*set, (1 << (sig - PSE51_SIGMIN)));
}

static inline int ffset(const pse51_sigset_t *set) {

    return ffs(*set)-1;
}


/* The sig*set functions are in the list of signal-async safe
   functions, hence the use of the interface mutex. */

int sigemptyset (sigset_t *user_set)

{
    pse51_sigset_t *set = user2pse51_sigset(user_set);
    spl_t s;
    
    xnlock_get_irqsave(&nklock, s);
    emptyset(set);
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int sigfillset(sigset_t *user_set)

{
    pse51_sigset_t *set = user2pse51_sigset(user_set);
    spl_t s;
    
    xnlock_get_irqsave(&nklock, s);
    fillset(set);
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int sigaddset(sigset_t *user_set, int sig)

{
    pse51_sigset_t *set = user2pse51_sigset(user_set);
    spl_t s;
    
    if (sig < PSE51_SIGMIN || sig > PSE51_SIGMAX)
	{
        thread_set_errno(EINVAL);
        return -1;
	}

    xnlock_get_irqsave(&nklock, s);
    addset(set, sig);
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int sigdelset (sigset_t *user_set, int sig)

{
    pse51_sigset_t *set = user2pse51_sigset(user_set);
    spl_t s;
    
    if (sig < PSE51_SIGMIN || sig > PSE51_SIGMAX)
	{
        thread_set_errno(EINVAL);
        return -1;
	}

    xnlock_get_irqsave(&nklock, s);
    delset(set, sig);
    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int sigismember (const sigset_t *user_set, int sig)

{
    pse51_sigset_t *set=user2pse51_sigset(user_set);
    int result;
    spl_t s;

    if (sig < PSE51_SIGMIN || sig > PSE51_SIGMAX)
	{
        thread_set_errno(EINVAL);
        return -1;
	}

    /* sigismember is required to return 1, not just non-zero. */
    xnlock_get_irqsave(&nklock, s);
    result = ismember(set,sig) ? 1 : 0;
    xnlock_put_irqrestore(&nklock, s);

    return result;
}

int sigaction (int sig, const struct sigaction *action, struct sigaction *old)

{
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (sig < PSE51_SIGMIN || sig > PSE51_SIGMAX || (!action && !old))
	{
        thread_set_errno(EINVAL);
        return -1;
	}

    if (action && testbits(action->sa_flags, ~SIGACTION_FLAGS))
	{
        thread_set_errno(ENOTSUP);
        return -1;
	}

    xnlock_get_irqsave(&nklock, s);

    if (old)
        *old = actions[sig - PSE51_SIGMIN];

    if (action)
	{
        struct sigaction *dest_action = &actions[sig-PSE51_SIGMIN];
            
        *dest_action = *action;

        if (!(testbits(action->sa_flags, SA_NOMASK)))
            addset(user2pse51_sigset(&dest_action->sa_mask), sig);
	}

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_sigmask (int how, const sigset_t *user_set, sigset_t *user_oset)

{
    pse51_sigset_t *set=user2pse51_sigset(user_set);
    pse51_sigset_t *oset=user2pse51_sigset(user_oset);
    pse51_sigset_t unblocked;
    pthread_t cur;
    spl_t s;

    emptyset(&unblocked);

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock, s);

    cur = pse51_current_thread();

    if (oset)
        *oset = cur->sigmask;

    if (!set)
	goto unlock_and_exit;

    if (xnthread_signaled_p(&cur->threadbase))
	/* Call xnpod_schedule to deliver any soon-to-be-blocked pending signal.
           Keep the interface mutex in order to avoid one of these signals to be
           posted in the meantime. */
	xnpod_schedule();

    switch (how)
	{

	case SIG_BLOCK:

	    setbits(cur->sigmask, *set);
	    break;

	case SIG_UNBLOCK:
	    /* Mark as pending any signal which was received while
	       blocked and is going to be unblocked. */
            unblocked = *set & cur->blocked_received;
	    clrbits(cur->sigmask, *set);
	    break;

	case SIG_SETMASK:

            unblocked = ~*set & cur->blocked_received;
	    cur->sigmask = *set;
	    break;

	default:

	    xnlock_put_irqrestore(&nklock, s);
	    return EINVAL;
	}
        
    /* Handle any unblocked signal. */
    if(unblocked)
        {
        setbits(xnthread_pending_signals(&cur->threadbase), unblocked);
    
        clrbits(cur->blocked_received, unblocked);

        xnpod_schedule();

        xnlock_put_irqrestore(&nklock, s);

        return 0;
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int pthread_kill (pthread_t thread, int sig)

{
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    /* sig == 0 has a special meaning: pthread_kill returns 0 if the
       target thread exists... Nothing else happens. */

    if (sig != 0 && (sig < PSE51_SIGMIN || sig > PSE51_SIGMAX))
        return EINVAL;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(thread, PSE51_THREAD_MAGIC,struct pse51_thread))
	{
        xnlock_put_irqrestore(&nklock, s);
        return ESRCH;
	}

    if (sig != 0)
	{
        if (ismember(&thread->sigmask, sig))
            addset(&thread->blocked_received, sig);
        else
            addset(&thread->threadbase.signals, sig);

        if (thread != pse51_current_thread())
            xnpod_unblock_thread(&thread->threadbase);

        xnpod_schedule();        
	}

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int sigpending(sigset_t *user_set)

{
    pse51_sigset_t *set = user2pse51_sigset(user_set);
    spl_t s;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (!set)
	{
        thread_set_errno(EINVAL);
        return -1;
	}

    xnlock_get_irqsave(&nklock, s);    /* To prevent pthread_kill from modifying
                                   blocked_received while we are reading. */

    *set = pse51_current_thread()->blocked_received;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int sigwait (const sigset_t *user_set, int *sig)

{
    pse51_sigset_t received, *set = user2pse51_sigset(user_set);
    pthread_t thread;
    spl_t s;
    int i;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (!set)
        return EINVAL;
    
    thread = pse51_current_thread();

    /* All signals in "set" must be blocked in order for sigwait to
       work reliably. */
    if (testbits(*set, ~thread->sigmask))
        return EINVAL;

    xnlock_get_irqsave(&nklock, s);

    received = (*set & thread->blocked_received);
    
    while (received == 0)
	{
        xnpod_suspend_thread(&thread->threadbase,
			     XNDELAY,
			     XN_INFINITE,
			     NULL);

        thread_cancellation_point(thread);

        received = (*set & thread->blocked_received);
	}

    i = ffset(&received) + PSE51_SIGMIN;

    delset(&thread->blocked_received, i);

    xnlock_put_irqrestore(&nklock, s);

    *sig = i;

    return 0;
}

static void pse51_dispatch_signals (xnsigmask_t sigs)

{
    pse51_sigset_t saved_mask;
    pthread_t thread;
    spl_t s;
    int sig;

    thread = pse51_current_thread();
    
    xnlock_get_irqsave(&nklock, s);

    saved_mask = thread->sigmask;

    for (sig = PSE51_SIGMIN; sig <= PSE51_SIGMAX; sig++)
	{
        if (ismember(&sigs, sig))
	    {
            struct sigaction *action = &actions[sig - PSE51_SIGMIN];

            if (action->sa_handler == SIG_IGN)
                continue;

            thread->sigmask = *user2pse51_sigset(&action->sa_mask);

            if (testbits(action->sa_flags, SA_ONESHOT))
		{
                action->sa_handler(sig);
                action->sa_handler = SIG_DFL;
		}
	    else
                action->sa_handler(sig);                
	    }
	}

    thread->sigmask = saved_mask;

    xnlock_put_irqrestore(&nklock, s);
}

void pse51_signal_init_thread (pthread_t newthread, const pthread_t parent)

{
    /* parent may be NULL if pthread_create is not called from a pse51
       thread. */
    emptyset(&newthread->blocked_received);

    if (parent)
        newthread->sigmask = parent->sigmask;
    else
        emptyset(&newthread->sigmask);

    newthread->threadbase.asr = &pse51_dispatch_signals;
    newthread->threadbase.asrmode = 0;
    newthread->threadbase.asrimask = 0;
}

void pse51_signal_pkg_init (void)

{
    int i;

    for (i = PSE51_SIGMIN; i <= PSE51_SIGMAX; i++)
	{
        actions[i].sa_handler = SIG_DFL;
        emptyset(user2pse51_sigset(&actions[i].sa_mask));
        actions[i].sa_flags = 0;
	}
}

void pse51_default_handler (int sig)

{
    pthread_t cur = pse51_current_thread();
    
    xnpod_fatal("Thread %s received unhandled signal %d.\n",
                thread_name(cur), sig);
}

EXPORT_SYMBOL(sigemptyset);
EXPORT_SYMBOL(sigfillset);
EXPORT_SYMBOL(sigaddset);
EXPORT_SYMBOL(sigdelset);
EXPORT_SYMBOL(sigismember);
EXPORT_SYMBOL(pthread_kill);
EXPORT_SYMBOL(pthread_sigmask);
EXPORT_SYMBOL(pse51_sigaction);
EXPORT_SYMBOL(sigpending);
EXPORT_SYMBOL(sigwait);
