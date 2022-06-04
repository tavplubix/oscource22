#pragma once
#include <stdint.h>
#include <inc/env.h>

typedef envid_t pid_t;


#define SIGINT 2        # term
#define SIGKILL 9       # kill
#define SIGUSR1 10      # term
#define SIGCHLD 17      # ign

#define SIGMAX 32

#define SIG_IGN (void (*)(int)0)
#define SIG_DFL (void (*)(int)1)

typedef uint32_t sigset_t;

union sigval {
    int sival_int;
    void * sival_ptr;
};

//#define SA_NOCLDSTOP  0x00000001
//#define SA_NOCLDWAIT  0x00000002
#define SA_SIGINFO  0x00000004
//#define SA_ONSTACK  0x08000000
//#define SA_RESTART  0x10000000
#define SA_NODEFER  0x40000000
#define SA_RESETHAND  0x80000000

typedef struct {
    int si_signo;
    int si_code;
    pid_t si_pid;
    void * si_addr;
    union sigval si_value;
} siginfo_t;

struct sigaction {
    union {
        void (* sa_handler)(int);
        void (* sa_sigaction)(int, siginfo_t *, void *);
    };
    sigset_t sa_mask;
    int sa_flags;
};


