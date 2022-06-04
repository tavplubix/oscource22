#include <inc/signal.h>
#include <inc/lib.h>


int sigqueue(pid_t pid, int signo, const union sigval value) {
    return sys_sigqueue(pid, signo, value);
}

int sigwait(const sigset_t * set, int * sig) {
    return sys_sigwait(set, sig);
}

int sigaction(int sig, const struct sigaction * act, struct sigaction * oact) {
    return sys_sigaction(sig, act, oact);
}
