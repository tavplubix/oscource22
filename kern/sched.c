#include <inc/assert.h>
#include <inc/x86.h>
#include <inc/string.h>
#include <kern/env.h>
#include <kern/monitor.h>
#include <kern/traceopt.h>
#include <kern/pmap.h>


struct Taskstate cpu_ts;
_Noreturn void sched_halt(void);

bool check_wait_for_signal(struct Env * env) {
    // Check if process is waiting for some signal (sys_sigwait)
    if (!env->env_sig_waiting)
        return false;

    // Try to find a signal in the queue
    size_t i = env->env_sig_queue_beg;
    while (i != env->env_sig_queue_end) {
        struct EnqueuedSignal * es = env->env_sig_queue + i;
        if (env->env_sig_waiting & SIGNAL_FLAG(es->signo))
            break;
        i = (i + 1) % SIGNALS_QUEUE_SIZE;
    }

    if (i == env->env_sig_queue_end)
        return true;

    // Remove signal from the queue and return from sys_sigwait
    struct EnqueuedSignal * es = env->env_sig_queue + i;

    if (trace_signals)
        cprintf("signals: env %x: wake up with %d\n", env->env_id, es->signo);

    //*env->env_sig_waiting_num_out = es->signo;
    if (env->env_sig_waiting_num_out)
        as_memcpy(&env->address_space, (uintptr_t)env->env_sig_waiting_num_out, (uintptr_t)&es->signo, sizeof(es->signo));
    env->env_sig_waiting_num_out = NULL;
    env->env_sig_waiting = 0;

    struct EnqueuedSignal * es_end = env->env_sig_queue + env->env_sig_queue_end;
    if (es < es_end) {
        memmove(es, es + 1, sizeof(struct EnqueuedSignal) * (env->env_sig_queue_end - i - 1));
    }
    else {
        memmove(es, es + 1, sizeof(struct EnqueuedSignal) * (SIGNALS_QUEUE_SIZE - i - 1));
        memcpy(env->env_sig_queue + SIGNALS_QUEUE_SIZE - 1, env->env_sig_queue, sizeof(struct EnqueuedSignal));
        memmove(env->env_sig_queue, env->env_sig_queue + 1, sizeof(struct EnqueuedSignal) * (env->env_sig_queue_end - 1));
    }

    env->env_sig_queue_end = (env->env_sig_queue_end - 1) % SIGNALS_QUEUE_SIZE;
    
    return false;
}

/* Choose a user environment to run and run it */
_Noreturn void
sched_yield(void) {
    /* Implement simple round-robin scheduling.
     *
     * Search through 'envs' for an ENV_RUNNABLE environment in
     * circular fashion starting just after the env was
     * last running.  Switch to the first such environment found.
     *
     * If no envs are runnable, but the environment previously
     * running is still ENV_RUNNING, it's okay to
     * choose that environment.
     *
     * If there are no runnable environments,
     * simply drop through to the code
     * below to halt the cpu */

    // LAB 3: Your code here:
    size_t next_env_idx = 0;
    if (curenv)
        next_env_idx = (curenv - envs) + 1;
    
    for (size_t i = 0; i < NENV; ++i)
    {
        size_t next_idx = (next_env_idx + i) % NENV;
        
        if (envs[next_idx].env_status != ENV_RUNNABLE)
            continue;
        
        if (envs[next_idx].env_is_stopped)
            continue;

        if (check_wait_for_signal(envs + next_idx))
            continue;

        env_run(envs + next_idx);
    }
 
    if (curenv && 
        curenv->env_status == ENV_RUNNING && 
        !curenv->env_is_stopped &&
        !check_wait_for_signal(curenv))
        env_run(curenv);

    cprintf("Halt\n");

    /* No runnable environments,
     * so just halt the cpu */
    sched_halt();
}

/* Halt this CPU when there is nothing to do. Wait until the
 * timer interrupt wakes it up. This function never returns */
_Noreturn void
sched_halt(void) {

    /* For debugging and testing purposes, if there are no runnable
     * environments in the system, then drop into the kernel monitor */
    int i;
    for (i = 0; i < NENV; i++)
        if (envs[i].env_status == ENV_RUNNABLE ||
            envs[i].env_status == ENV_RUNNING) break;
    if (i == NENV) {
        cprintf("No runnable environments in the system!\n");
        for (;;) monitor(NULL);
    }

    /* Mark that no environment is running on CPU */
    curenv = NULL;

    /* Reset stack pointer, enable interrupts and then halt */
    asm volatile(
            "movq $0, %%rbp\n"
            "movq %0, %%rsp\n"
            "pushq $0\n"
            "pushq $0\n"
            "sti\n"
            "hlt\n" ::"a"(cpu_ts.ts_rsp0));

    /* Unreachable */
    for (;;)
        ;
}
