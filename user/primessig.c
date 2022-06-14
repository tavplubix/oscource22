/* Concurrent version of prime sieve of Eratosthenes.
 * Invented by Doug McIlroy, inventor of Unix pipes.
 * See http://swtch.com/~rsc/thread/.
 * The picture halfway down the page and the text surrounding it
 * explain what's going on here.
 *
 * Since NENVS is 1024, we can print 1022 primes before running out.
 * The remaining two environments are the integer generator at the bottom
 * of main and user/idle. */

#include <inc/lib.h>
#include <inc/signal.h>

volatile sig_atomic_t value = 0;
volatile sig_atomic_t sender = 0;
volatile sig_atomic_t updated = 0;

static void
handler(int signo, siginfo_t * info, void *) {
    assert(signo == SIGUSR1);
    value = info->si_value.sival_int;
    sender = info->si_pid;
    updated = 1;
}

static int
sig_recv(envid_t * out) {
    while (!updated) {
        sys_yield();
    }
    updated = 0;
    *out = sender;
    int res = value;
    assert(!updated);
    union sigval sv;
    sv.sival_ptr = 0;
    // notify sender that we received value
    if (sigqueue(*out, SIGUSR2, sv))
        panic("sigqueue failed\n");
    return res;
}

static void
sig_send(envid_t id, int value) {
    union sigval sv;
    sv.sival_int = value;
    sigset_t set = SIGNAL_FLAG(SIGUSR2);
    if (sigsetmask(set))
        panic("sigsetmask failed\n");
    if (sigqueue(id, SIGUSR1, sv))
        panic("sigqueue failed\n");
    // ensure signal is processed before sending the next one
    int sig = 0;
    if (sigwait(&set, &sig))
        panic("sigwait failed\n");
    if (sigsetmask(0))
        panic("sigsetmask failed\n");
    assert(sig == SIGUSR2);
}

unsigned
primeproc(void) {
    int i, id, p;
    envid_t envid;

    /* Fetch a prime from our left neighbor */
top:
    p = sig_recv(&envid);
    cprintf("%d ", p);

    /* Fork a right neighbor to continue the chain */
    if ((id = fork()) < 0)
        panic("fork: %i", id);
    if (id == 0)
        goto top;

    /* Filter out multiples of our prime */
    while (1) {
        i = sig_recv(&envid);
        if (i % p)
            sig_send(id, i);
    }
}

void
umain(int argc, char **argv) {

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    int id;

    /* Fork the first prime process in the chain */
    if ((id = fork()) < 0)
        panic("fork: %i", id);
    if (id == 0)
        primeproc();

    /* Feed all the integers through */
    for (int i = 2;; i++)
        sig_send(id, i);
}
