#include <inc/signal.h>
#include <inc/lib.h>
#include <inc/env.h>
#include <kern/traceopt.h>

static void
_generic_signal_handler_impl(struct UTrapframe *utf, struct EnqueuedSignal * es) {
    if (es->sa.sa_handler == SIG_DFL) {
        if (trace_signals)
            cprintf("signals: env %x: exiting due to signal %d\n", sys_getenvid(), es->signo);
        sys_env_destroy(CURENVID);
        assert(false);
    }

    if (es->sa.sa_handler == SIG_IGN) {
        if (trace_signals)
            cprintf("signals: env %x: ignoring signal %d\n", sys_getenvid(), es->signo);
        return;
    }

    if (es->sa.sa_flags & SA_SIGINFO) {
        (es->sa.sa_sigaction)(es->signo, &(es->info), utf);
    }
    else {
        (es->sa.sa_handler)(es->signo);
    }
}

void
_generic_signal_handler(struct UTrapframe *utf, struct EnqueuedSignal * es) {
    if (trace_signals)
        cprintf("signals: env %x: begin generic handler for %d\n", sys_getenvid(), es->signo);

    _generic_signal_handler_impl(utf, es);

    if (trace_signals)
        cprintf("signals: env %x: end generic handler for %d\n", sys_getenvid(), es->signo);
}

int sigqueue(pid_t pid, int signo, const union sigval value) {
    return sys_sigqueue(pid, signo, value);
}

int sigwait(const sigset_t * set, int * sig) {
    return sys_sigwait(set, sig);
}

extern void _signal_handler_trampoline(void);

int sigaction(int sig, const struct sigaction * act, struct sigaction * oact) {
    int err = sys_env_set_pgfault_upcall(CURENVID, _signal_handler_trampoline);
    if (err)
        return err;
    return sys_sigaction(sig, act, oact);
}

int sigprocmask(int how, const sigset_t * set, sigset_t * oldset) {
    return sys_sigprocmask(how, set, oldset);
}
