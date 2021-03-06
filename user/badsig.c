#include <inc/lib.h>
#include <inc/signal.h>


volatile int finished_children = 0;

static void
handler_chld(int signo) {
    assert(signo == SIGCHLD);
    ++finished_children;
}

static void
handler(int signo, siginfo_t * info, void *) {
    union sigval sv;
    sv.sival_int = 0;
    while (true) {
        // it will update `updated` flag in parent process
        sigqueue(info->si_pid, SIGUSR2, sv);
        sys_yield();
    }
}

volatile bool updated = false;

static void
handler_usr2(int signo) {
    assert(signo == SIGUSR2);
    updated = true;
}

void
umain(int argc, char **argv) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    // invalid signals
    int err;
    err = sigaction(0, &sa, NULL);
    assert(err == -E_INVAL);
    err = sigaction(SIGMAX, &sa, NULL);
    assert(err == -E_INVAL);
    err = sigaction(SIGKILL, &sa, NULL);
    assert(err == -E_INVAL);
    err = sigaction(SIGSTOP, &sa, NULL);
    assert(err == -E_INVAL);
    err = sigaction(SIGCONT, &sa, NULL);
    assert(err == -E_INVAL);
    union sigval sv;
    sv.sival_ptr = NULL;
    err = sigqueue(0, 0, sv);
    assert(err == -E_INVAL);
    err = sigqueue(0, SIGMAX, sv);
    assert(err == -E_INVAL);

    // invalid flags
    sa.sa_flags = 123;
    err = sigaction(SIGUSR1, &sa, NULL);
    assert(err == -E_INVAL);
    sa.sa_flags = 0;

    sa.sa_handler = handler_chld;
    sa.sa_flags = SA_NOCLDSTOP;
    err = sigaction(SIGCHLD, &sa, NULL);
    assert(err == 0);

    // invalid pointers
    if (fork() == 0) {
        sigaction(SIGUSR1, (void *)0xdeadbeef, NULL);
        assert(false);
    }
    if (fork() == 0) {
        sigaction(SIGUSR1, &sa, (void *)0xdeadbeef);
        assert(false);
    }
    if (fork() == 0) {
        sigwait((void *)0xdeadbeef, NULL);
        assert(false);
    }
    if (fork() == 0) {
        sigset_t set = 2;
        sigwait(&set, (void *)0xdeadbeef);
        assert(false);
    }

    // check that queue does not overflow
    // and that it's not possible to block special signals
    sa.sa_handler = handler_usr2;
    err = sigaction(SIGUSR2, &sa, NULL);
    assert(err == 0);
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_mask = (uint32_t)-1;
    err = sigaction(SIGUSR1, &sa, NULL);
    assert(err == 0);

    envid_t child1 = fork();
    if (child1 == 0) {
        // block all except USR1
        sigsetmask(~SIGNAL_FLAG(SIGUSR1));
        while (1) sys_yield();
    }

    while (err == 0)
        err = sigqueue(child1, SIGUSR1, sv);
    assert(err == -E_AGAIN);
    err = sigqueue(child1, SIGUSR1, sv);
    assert(err == -E_AGAIN);

    // stop child and check that flag is not updating anymore
    for (int i = 0; i < 10; ++i) sys_yield();
    err = sigqueue(child1, SIGSTOP, sv);
    assert(err == 0);
    assert(updated);
    updated = false;
    for (int i = 0; i < 10; ++i) sys_yield();
    assert(!updated);

    // continue
    err = sigqueue(child1, SIGCONT, sv);
    assert(err == 0);
    for (int i = 0; i < 10; ++i) sys_yield();
    assert(updated);

    // kill
    err = sigqueue(child1, SIGKILL, sv);
    assert(err == 0);
    updated = false;
    for (int i = 0; i < 10; ++i) sys_yield();
    assert(!updated);

    // test SIGCHLD and SA_NOCLDSTOP btw
    assert(finished_children == 5);

    sa.sa_handler = handler_chld;
    sa.sa_flags = 0;
    err = sigaction(SIGCHLD, &sa, NULL);
    assert(err == 0);

    envid_t child2 = fork();
    if (child2 == 0) {
        while (1) sys_yield();
    }
    sigqueue(child2, SIGSTOP, sv);
    sigqueue(child2, SIGCONT, sv);
    sigqueue(child2, SIGKILL, sv);
    assert(finished_children == 8);

    cprintf("Test finished\n");
}
