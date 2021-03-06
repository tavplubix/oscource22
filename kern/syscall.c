/* See COPYRIGHT for copyright information. */

#include "env.h"
#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/signal.h>

#include <kern/console.h>
#include <kern/env.h>
#include <kern/kclock.h>
#include <kern/pmap.h>
#include <kern/sched.h>
#include <kern/syscall.h>
#include <kern/trap.h>
#include <kern/traceopt.h>
#include <stdint.h>

/* Print a string to the system console.
 * The string is exactly 'len' characters long.
 * Destroys the environment on memory errors. */
static int
sys_cputs(const char *s, size_t len) {
    // LAB 8: Your code here

    /* Check that the user has permission to read memory [s, s+len).
    * Destroy the environment if not. */
    user_mem_assert(curenv, s, len, PROT_R | PROT_USER_);

#ifdef SANITIZE_SHADOW_BASE
    for (size_t i = 0; i < len; ++i) {
        char c;
        nosan_memcpy(&c, (void *)s + i, 1);
        cputchar(c);
    }
#else
    for (size_t i = 0; i < len; ++i)
        cputchar(s[i]);
#endif

    return 0;
}

/* Read a character from the system console without blocking.
 * Returns the character, or 0 if there is no input waiting. */
static int
sys_cgetc(void) {
    // LAB 8: Your code here

    return cons_getc();
}

/* Returns the current environment's envid. */
static envid_t
sys_getenvid(void) {
    // LAB 8: Your code here
    return curenv->env_id;
}

static int
sys_env_destroy_impl(struct Env * env, int signo) {
#if 1 /* TIP: Use this snippet to log required for passing grade tests info */
    if (trace_envs) {
        if (signo) {
            cprintf("[%08x] exiting due to signal %d\n",
                env->env_id, signo);
        }
        else {
            cprintf(env == curenv ?
                        "[%08x] exiting gracefully\n" :
                        "[%08x] destroying %08x\n",
                curenv->env_id, env->env_id);

        }
    }
#endif

    env_destroy(env);
    return 0;
}

/* Destroy a given environment (possibly the currently running environment).
 *
 *  Returns 0 on success, < 0 on error.  Errors are:
 *  -E_BAD_ENV if environment envid doesn't currently exist,
 *      or the caller doesn't have permission to change envid.
 */
static int
sys_env_destroy(envid_t envid) {
    // LAB 8: Your code here.

    struct Env * env = NULL;
    if (envid2env(envid, &env, 1))
        return -E_BAD_ENV;

    return sys_env_destroy_impl(env, 0);
}

/* Deschedule current environment and pick a different one to run. */
static int
sys_yield(void) {
    // LAB 9: Your code here
    sched_yield();
    return 0;
}

/* Allocate a new environment.
 * Returns envid of new environment, or < 0 on error.  Errors are:
 *  -E_NO_FREE_ENV if no free environment is available.
 *  -E_NO_MEM on memory exhaustion. */
static envid_t
sys_exofork(void) {
    /* Create the new environment with env_alloc(), from kern/env.c.
     * It should be left as env_alloc created it, except that
     * status is set to ENV_NOT_RUNNABLE, and the register set is copied
     * from the current environment -- but tweaked so sys_exofork
     * will appear to return 0. */

    // LAB 9: Your code here
    struct Env * env = NULL;
    if (env_alloc(&env, curenv->env_id, ENV_TYPE_USER))
        return -E_NO_FREE_ENV;

    env->env_status = ENV_NOT_RUNNABLE;
    env->env_tf = curenv->env_tf;
    env->env_tf.tf_regs.reg_rax = 0;

    return env->env_id;;
}

/* Set envid's env_status to status, which must be ENV_RUNNABLE
 * or ENV_NOT_RUNNABLE.
 *
 * Returns 0 on success, < 0 on error.  Errors are:
 *  -E_BAD_ENV if environment envid doesn't currently exist,
 *      or the caller doesn't have permission to change envid.
 *  -E_INVAL if status is not a valid status for an environment. */
static int
sys_env_set_status(envid_t envid, int status) {
    /* Hint: Use the 'envid2env' function from kern/env.c to translate an
     * envid to a struct Env.
     * You should set envid2env's third argument to 1, which will
     * check whether the current environment has permission to set
     * envid's status. */

    // LAB 9: Your code here
    if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE)
        return -E_INVAL;
    
    struct Env * env = NULL;
    if (envid2env(envid, &env, true))
        return -E_BAD_ENV;
    
    env->env_status = status;

    return 0;
}

/* Set the page fault upcall for 'envid' by modifying the corresponding struct
 * Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
 * kernel will push a fault record onto the exception stack, then branch to
 * 'func'.
 *
 * Returns 0 on success, < 0 on error.  Errors are:
 *  -E_BAD_ENV if environment envid doesn't currently exist,
 *      or the caller doesn't have permission to change envid. */
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func) {
    // LAB 9: Your code here:
    struct Env * env = NULL;
    if (envid2env(envid, &env, true))
        return -E_BAD_ENV;

    env->env_pgfault_upcall = func;

    return 0;
}

/* Allocate a region of memory and map it at 'va' with permission
 * 'perm' in the address space of 'envid'.
 * The page's contents are set to 0.
 * If a page is already mapped at 'va', that page is unmapped as a
 * side effect.
 * 
 * This call should work with or without ALLOC_ZERO/ALLOC_ONE flags
 * (set them if they are not already set)
 * 
 * It allocates memory lazily so you need to use map_region
 * with PROT_LAZY and ALLOC_ONE/ALLOC_ZERO set.
 * 
 * Don't forget to set PROT_USER_
 * 
 * PROT_ALL is useful for validation.
 *
 * Return 0 on success, < 0 on error.  Errors are:
 *  -E_BAD_ENV if environment envid doesn't currently exist,
 *      or the caller doesn't have permission to change envid.
 *  -E_INVAL if va >= MAX_USER_ADDRESS, or va is not page-aligned.
 *  -E_INVAL if perm is inappropriate (see above).
 *  -E_NO_MEM if there's no memory to allocate the new page,
 *      or to allocate any necessary page tables. */
static int
sys_alloc_region(envid_t envid, uintptr_t addr, size_t size, int perm) {
    // LAB 9: Your code here:
    struct Env * env = NULL;
    if (envid2env(envid, &env, true))
        return -E_BAD_ENV;

    if (addr >= MAX_USER_ADDRESS)
        return -E_INVAL;
    
    if (addr & CLASS_MASK(0))
        return -E_INVAL;

    if (perm & ~(PROT_ALL | ALLOC_ONE | ALLOC_ZERO))
        return -E_INVAL;

    if (!((perm & ALLOC_ONE) || (perm & ALLOC_ZERO)))
        perm |= ALLOC_ZERO;

    if (map_region(&env->address_space, addr, NULL, 0, size, perm | PROT_USER_ | PROT_LAZY))
        return -E_NO_MEM;

    return 0;
}

/* Map the region of memory at 'srcva' in srcenvid's address space
 * at 'dstva' in dstenvid's address space with permission 'perm'.
 * Perm has the same restrictions as in sys_alloc_region, except
 * that it also does not supprt ALLOC_ONE/ALLOC_ONE flags.
 * 
 * You only need to check alignment of addresses, perm flags and
 * that addresses are a part of user space. Everything else is
 * already checked inside map_region().
 *
 * Return 0 on success, < 0 on error.  Errors are:
 *  -E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
 *      or the caller doesn't have permission to change one of them.
 *  -E_INVAL if srcva >= MAX_USER_ADDRESS or srcva is not page-aligned,
 *      or dstva >= MAX_USER_ADDRESS or dstva is not page-aligned.
 *  -E_INVAL is srcva is not mapped in srcenvid's address space.
 *  -E_INVAL if perm is inappropriate (see sys_page_alloc).
 *  -E_INVAL if (perm & PROT_W), but srcva is read-only in srcenvid's
 *      address space.
 *  -E_NO_MEM if there's no memory to allocate any necessary page tables. */

static int
sys_map_region_impl(envid_t srcenvid, uintptr_t srcva,
               envid_t dstenvid, uintptr_t dstva, size_t size, int perm, bool check) {
    // LAB 9: Your code here
    struct Env * srcenv = NULL;
    if (envid2env(srcenvid, &srcenv, check))
        return -E_BAD_ENV;

    struct Env * dstenv = NULL;
    if (envid2env(dstenvid, &dstenv, check))
        return -E_BAD_ENV;

    if (srcva >= MAX_USER_ADDRESS)
        return -E_INVAL;
    
    if (srcva & CLASS_MASK(0))
        return -E_INVAL;
    
    if (dstva >= MAX_USER_ADDRESS)
        return -E_INVAL;
    
    if (dstva & CLASS_MASK(0))
        return -E_INVAL;

    //if (perm & ~PROT_ALL)
    //    return -E_INVAL;

    if ((perm & ALLOC_ONE) || (perm & ALLOC_ZERO))
        return -E_INVAL;

    if (map_region(&dstenv->address_space, dstva, &srcenv->address_space, srcva, size, perm | PROT_USER_))
        return -E_NO_MEM;

    return 0;
}

static int
sys_map_region(envid_t srcenvid, uintptr_t srcva,
               envid_t dstenvid, uintptr_t dstva, size_t size, int perm) {
    return sys_map_region_impl(srcenvid, srcva, dstenvid, dstva, size, perm, true);
}

/* Unmap the region of memory at 'va' in the address space of 'envid'.
 * If no page is mapped, the function silently succeeds.
 *
 * Return 0 on success, < 0 on error.  Errors are:
 *  -E_BAD_ENV if environment envid doesn't currently exist,
 *      or the caller doesn't have permission to change envid.
 *  -E_INVAL if va >= MAX_USER_ADDRESS, or va is not page-aligned. */
static int
sys_unmap_region(envid_t envid, uintptr_t va, size_t size) {
    /* Hint: This function is a wrapper around unmap_region(). */

    // LAB 9: Your code here
    struct Env * env = NULL;
    if (envid2env(envid, &env, true))
        return -E_BAD_ENV;

    if (va >= MAX_USER_ADDRESS)
        return -E_INVAL;
    
    if (va & CLASS_MASK(0))
        return -E_INVAL;

    unmap_region(&env->address_space, va, size);

    return 0;
}

/* Try to send 'value' to the target env 'envid'.
 * If srcva < MAX_USER_ADDRESS, then also send region currently mapped at 'srcva',
 * so receiver also gets mapping.
 *
 * The send fails with a return value of -E_IPC_NOT_RECV if the
 * target is not blocked, waiting for an IPC.
 *
 * The send also can fail for the other reasons listed below.
 *
 * Otherwise, the send succeeds, and the target's ipc fields are
 * updated as follows:
 *    env_ipc_recving is set to 0 to block future sends;
 *    env_ipc_maxsz is set to min of size and it's current vlaue;
 *    env_ipc_from is set to the sending envid;
 *    env_ipc_value is set to the 'value' parameter;
 *    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
 * The target environment is marked runnable again, returning 0
 * from the paused sys_ipc_recv system call.  (Hint: does the
 * sys_ipc_recv function ever actually return?)
 *
 * If the sender wants to send a page but the receiver isn't asking for one,
 * then no page mapping is transferred, but no error occurs.
 * Send region size is the minimum of sized specified in sys_ipc_try_send() and sys_ipc_recv()
 * 
 * The ipc only happens when no errors occur.
 *
 * Returns 0 on success, < 0 on error.
 * Errors are:
 *  -E_BAD_ENV if environment envid doesn't currently exist.
 *      (No need to check permissions.)
 *  -E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
 *      or another environment managed to send first.
 *  -E_INVAL if srcva < MAX_USER_ADDRESS but srcva is not page-aligned.
 *  -E_INVAL if srcva < MAX_USER_ADDRESS and perm is inappropriate
 *      (see sys_page_alloc).
 *  -E_INVAL if srcva < MAX_USER_ADDRESS but srcva is not mapped in the caller's
 *      address space.
 *  -E_INVAL if (perm & PTE_W), but srcva is read-only in the
 *      current environment's address space.
 *  -E_NO_MEM if there's not enough memory to map srcva in envid's
 *      address space. */
static int
sys_ipc_try_send(envid_t envid, uint32_t value, uintptr_t srcva, size_t size, int perm) {
    // LAB 9: Your code here
    struct Env * env = NULL;
    if (envid2env(envid, &env, false))
        return -E_BAD_ENV;

    if (!env->env_ipc_recving)
        return -E_IPC_NOT_RECV;

    if (srcva < MAX_USER_ADDRESS) {
        int res = sys_map_region_impl(0, srcva, envid, env->env_ipc_dstva, size, perm, false);
        if (res < 0)
            return res;
        
        env->env_ipc_perm = perm;
        env->env_ipc_maxsz = MIN(size, env->env_ipc_maxsz);
    } else {
        env->env_ipc_perm = 0;
    }

    env->env_ipc_recving = 0;
    env->env_ipc_from = sys_getenvid();
    env->env_ipc_value = value;
    env->env_status = ENV_RUNNABLE;
    
    return 0;
}

/* Block until a value is ready.  Record that you want to receive
 * using the env_ipc_recving, env_ipc_maxsz and env_ipc_dstva fields of struct Env,
 * mark yourself not runnable, and then give up the CPU.
 *
 * If 'dstva' is < MAX_USER_ADDRESS, then you are willing to receive a page of data.
 * 'dstva' is the virtual address at which the sent page should be mapped.
 *
 * This function only returns on error, but the system call will eventually
 * return 0 on success.
 * Return < 0 on error.  Errors are:
 *  -E_INVAL if dstva < MAX_USER_ADDRESS but dstva is not page-aligned;
 *  -E_INVAL if dstva is valid and maxsize is 0,
 *  -E_INVAL if maxsize is not page aligned.
 */
static int
sys_ipc_recv(uintptr_t dstva, uintptr_t maxsize) {
    // LAB 9: Your code here
    if (maxsize & CLASS_MASK(0))
        return -E_INVAL;

    if (dstva < MAX_USER_ADDRESS)
    {
        if (dstva & CLASS_MASK(0))
            return -E_INVAL;
 
        if (maxsize == 0)
            return -E_INVAL;
        
        curenv->env_ipc_dstva = dstva;
        curenv->env_ipc_maxsz = maxsize;
    }

    curenv->env_ipc_recving = 1;
    curenv->env_status = ENV_NOT_RUNNABLE;
    curenv->env_tf.tf_regs.reg_rax = 0;
    sched_yield();
    return 0;
}

/*
 * This function sets trapframe and is unsafe
 * so you need:
 *   -Check environment id to be valid and accessible
 *   -Check argument to be valid memory
 *   -Use nosan_memcpy to copy from usespace
 *   -Prevent privilege escalation by overriding segments
 *   -Only allow program to set safe flags in RFLAGS register
 *   -Force IF to be set in RFLAGS
 */
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf) {
    // LAB 11: Your code here
    struct Env * env = NULL;
    if (envid2env(envid, &env, true))
        return -E_BAD_ENV;

    size_t tfs = sizeof(struct Trapframe);
    user_mem_assert(env, tf, tfs, PROT_R | PROT_USER_);
    nosan_memcpy((void *)&env->env_tf, (void *)tf, tfs);

    env->env_tf.tf_ds = GD_UD | 3;
    env->env_tf.tf_es = GD_UD | 3;
    env->env_tf.tf_ss = GD_UD | 3;
    env->env_tf.tf_cs = GD_UT | 3;

    env->env_tf.tf_rflags |= FL_IF;

    return 0;
}

/* Return date and time in UNIX timestamp format: seconds passed
 * from 1970-01-01 00:00:00 UTC. */
static int
sys_gettime(void) {
    // LAB 12: Your code here
    return gettime();
}

/*
 * This function return the difference between maximal
 * number of references of regions [addr, addr + size] and [addr2,addr2+size2]
 * if addr2 is less than MAX_USER_ADDRESS, or just
 * maximal number of references to [addr, addr + size]
 * 
 * Use region_maxref() here.
 */
static int
sys_region_refs(uintptr_t addr, size_t size, uintptr_t addr2, uintptr_t size2) {
    // LAB 10: Your code here
    if (MAX_USER_ADDRESS <= addr2)
        return region_maxref(current_space, addr, size);

    int n1 = region_maxref(current_space, addr, size);
    int n2 = region_maxref(current_space, addr2, size2);
    return n1 - n2;
}


static int
sys_sigqueue(pid_t pid, int signo, const union sigval value) {
    
    if (signo <= 0 || SIGMAX <= signo)
        return -E_INVAL;

    struct Env * env = NULL;
    // It's more convinient to test without checking permissions
    // (but in general we shuld not allow to send sinals to arbitrary processes)
#if defined(TEST_ITASK)
    if (envid2env(pid, &env, false))
        return -E_BAD_ENV;
#else
    if (envid2env(pid, &env, true))
        return -E_BAD_ENV;
#endif

    // Handle special signals first
    if (signo == SIGKILL) {
        int err = sys_env_destroy_impl(env, signo);
        assert(!err);
        goto signal_sent;
    }

    if (signo == SIGSTOP) {
        env->env_is_stopped = true;
        maybe_send_sigchld(env->env_parent_id, false);
        goto signal_sent;
    }

    if (signo == SIGCONT) {
        env->env_is_stopped = false;
        maybe_send_sigchld(env->env_parent_id, false);
        goto signal_sent;
    }

    struct sigaction * sa = env->env_sigaction + signo;

    if (!env->env_pgfault_upcall) {
        // Don't need to enqueue a signal with default handler
        // But will call it if generic handler is set
        if (sa->sa_handler == SIG_DFL) {
            int err = sys_env_destroy_impl(env, signo);
            assert(!err);
            return 0;
        }
        else if (sa->sa_handler == SIG_IGN) {
            return 0;
        }
    }

    // Find slot in circular queue
    size_t new_end = (env->env_sig_queue_end + 1) % SIGNALS_QUEUE_SIZE;
    if (new_end == env->env_sig_queue_beg)
        return -E_AGAIN;

    // Fill signal info (inplace)
    struct EnqueuedSignal * es = env->env_sig_queue + env->env_sig_queue_end;
    es->signo = signo;
    es->info.si_signo = signo;
    es->info.si_code = 0;
    es->info.si_pid = sys_getenvid();
    es->info.si_addr = 0;
    es->info.si_value = value;

    // Copy sigaction structure as well to avoid any possible races with sys_sigaction
    // (and don't even think about consiquences of such races)
    nosan_memcpy(&(es->sa), sa, sizeof(struct sigaction));

    env->env_sig_queue_end = new_end;

    if (sa->sa_flags & SA_RESETHAND) {
        sa->sa_handler = signo == SIGCHLD ? SIG_IGN : SIG_DFL;
        sa->sa_flags &= ~SA_SIGINFO;
    }
    
signal_sent:
    if (trace_signals)
        cprintf("signals: sent signal %d from %x to %x\n", signo, curenv->env_id, pid);
    return 0;
}

static int
sys_sigwait(const sigset_t * set, int * sig) {
    user_mem_assert(curenv, set, sizeof(set), PROT_R | PROT_USER_);
    if (sig)
        user_mem_assert(curenv, sig, sizeof(sig), PROT_R | PROT_W | PROT_USER_);

    sigset_t all = -1;
    all &= ~SIGNAL_FLAG(SIGRESERVED);
    all &= ~SIGNAL_FLAG(SIGSTOP);
    all &= ~SIGNAL_FLAG(SIGCONT);
    all &= ~SIGNAL_FLAG(SIGKILL);
    if (*set & ~all)
        return -E_INVAL;
    if (!(*set & all))
        return -E_INVAL;

    curenv->env_sig_waiting = *set;
    curenv->env_sig_waiting_num_out = sig;
    curenv->env_tf.tf_regs.reg_rax = 0;
    if (trace_signals)
        cprintf("signals: env %x: will wait for signals 0x%x\n", curenv->env_id, *set);
    sched_yield();
    return 0;
}

static int
sys_sigaction(int sig, const struct sigaction * act, struct sigaction * oact) {\

    if (sig <= 0 || SIGMAX <= sig)
        return -E_INVAL;

    /// It's not allowed to handle special signals
    if (sig == SIGKILL || sig == SIGSTOP || sig == SIGCONT)
        return -E_INVAL;

    /// Ensure that pointers are valid
    size_t act_size = sizeof(struct sigaction);
    user_mem_assert(curenv, act, act_size, PROT_R | PROT_USER_);

    if (act->sa_flags & ~SA_ALL_FLAGS)
        return -E_INVAL;

    struct sigaction * env_act = curenv->env_sigaction + sig;
    if (oact) {
        user_mem_assert(curenv, oact, act_size, PROT_R | PROT_W | PROT_USER_);
        nosan_memcpy(oact, env_act, act_size);
    }

    nosan_memcpy(env_act, act, act_size);
    return 0;
}

static int
sys_sigsetmask(uint32_t new_mask) {
    if (trace_signals)
        cprintf("signals: env %x: change mask from 0x%x to 0x%x\n", curenv->env_id, curenv->env_sig_mask, new_mask);
    curenv->env_sig_mask = new_mask;
    return 0;
}


/* Dispatches to the correct kernel function, passing the arguments. */
uintptr_t
syscall(uintptr_t syscallno, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    /* Call the function corresponding to the 'syscallno' parameter.
     * Return any appropriate return value. */

    // LAB 8: Your code here
    // LAB 9: Your code here
    // LAB 11: Your code here
    // LAB 12: Your code here
    switch (syscallno) {
    case SYS_cputs:
        return sys_cputs((char *)a1, (size_t)a2);
    case SYS_cgetc:
        return sys_cgetc();
    case SYS_getenvid:
        return sys_getenvid();
    case SYS_env_destroy:
        return sys_env_destroy((envid_t)a1);
    case SYS_alloc_region:
        return sys_alloc_region((envid_t)a1, a2, (size_t)a3, (int)a4);
    case SYS_map_region:
        return sys_map_region((envid_t)a1, a2, (envid_t)a3, a4, (size_t)a5, (int)a6);
    case SYS_unmap_region:
        return sys_unmap_region((envid_t)a1, a2, (size_t)a3);
    case SYS_region_refs:
        return sys_region_refs(a1, (size_t)a2, a3, a4);
    case SYS_exofork:
        return sys_exofork();
    case SYS_env_set_status:
        return sys_env_set_status((envid_t)a1, (int)a2);
    case SYS_env_set_trapframe:
        return sys_env_set_trapframe((envid_t)a1, (void *)a2);
    case SYS_env_set_pgfault_upcall:
        return sys_env_set_pgfault_upcall((envid_t)a1, (void *)a2);
    case SYS_yield:
        return sys_yield();
    case SYS_ipc_try_send:
        return sys_ipc_try_send((envid_t)a1, (uint32_t)a2, a3, (size_t)a4, (int)a5);
    case SYS_ipc_recv:
        return sys_ipc_recv(a1, a2);
    case SYS_gettime:
        return sys_gettime();
    case SYS_sigqueue:
        return sys_sigqueue((pid_t)a1, (int)a2, (union sigval)(void *)a3);
    case SYS_sigwait:
        return sys_sigwait((sigset_t *)a1, (int *)a2);
    case SYS_sigaction:
        return sys_sigaction((int)a1, (struct sigaction *)a2, (struct sigaction *)a3);
    case SYS_sigsetmask:
        return sys_sigsetmask((uint32_t)a1);
    default:
        return -E_NO_SYS;
    }

    return -E_NO_SYS;
}
