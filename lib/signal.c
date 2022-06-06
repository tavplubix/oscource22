#include <inc/signal.h>
#include <inc/lib.h>

void
_generic_signal_handler(struct UTrapframe *utf, struct EnqueuedSignal * es) {
    cprintf("handler %d lol\n", es->signo);
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

int sigsetmask(uint32_t new_mask) {
    return sys_sigsetmask(new_mask);
}
