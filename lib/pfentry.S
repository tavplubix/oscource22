#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>
#include <kern/macro.h>

# Page fault upcall entrypoint.

# This is where we ask the kernel to redirect us to whenever we cause
# a page fault in user space (see the call to sys_set_pgfault_handler
# in pgfault.c).
#
# When a page fault actually occurs, the kernel switches our RSP to
# point to the user exception stack if we're not already on the user
# exception stack, and then it pushes a UTrapframe onto our user
# exception stack:
#
#  utf_rsp
#  utf_rflags
#  utf_rip
#  utf_regs.reg_rax
#  ...
#  utf_regs.reg_r15
#  utf_err (error code)
#  utf_fault_va <-- %rsp
#
# If this is a recursive fault, the kernel will reserve for us a
# blank word above the trap-time rsp for scratch work when we unwind
# the recursive call.
#
# We then have call up to the appropriate page fault handler in C
# code, pointed to by the global variable '_pgfault_handler'.

.text
.globl _pgfault_upcall
.globl _signal_handler_trampoline
_pgfault_upcall:
_signal_handler_trampoline:
    movq  %rsp,%rdi # passing the function argument (UTrapframe *) in rdi
    # sizeof(struct EnqueuedSignal) == 56
    # sizeof(curenv->env_sig_mask) == 4
    add $60, %rdi

    # load signo
    mov (%rsp), %eax
    test %eax, %eax
    jnz _call_generic_signal_handler

    # Call the C page fault handler.
    movabs $_handle_vectored_pagefault, %rax
    call *%rax
    jmp _return_from_handler

_call_generic_signal_handler:
    movq %rsp, %rsi # passing the function argument (EnqueuedSignal *) in rsi
    movabs $_generic_signal_handler, %rax
    call *%rax

_return_from_handler:
    # ignore EnqueuedSignal and env_sig_mask
    add $60, %rsp
    # load env_sig_mask (as the first func arg)
    mov -4(%rsp), %rdi
    # restore blocked signals mask
    movabs $sys_sigsetmask, %rax
    call *%rax
    # now we may receive another signal

    # Now the C page fault handler has returned and you must return
    # to the trap time state.
    # Push trap-time %eip onto the trap-time stack.
    #
    # Explanation:
    #   We must prepare the trap-time stack for our eventual return to
    #   re-execute the instruction that faulted.
    #   Unfortunately, we can't return directly from the exception stack:
    #   We can't call 'jmp', since that requires that we load the address
    #   into a register, and all registers must have their trap-time
    #   values after the return.
    #   We can't call 'ret' from the exception stack either, since if we
    #   did, %esp would have the wrong value.
    #   So instead, we push the trap-time %eip onto the *trap-time* stack!
    #   Below we'll switch to that stack and call 'ret', which will
    #   restore %eip to its pre-fault value.
    #
    #   In the case of a recursive fault on the exception stack,
    #   note that the word we're pushing now will fit in the
    #   blank word that the kernel reserved for us.
    #
    # Throughout the remaining code, think carefully about what
    # registers are available for intermediate calculations.  You
    # may find that you have to rearrange your code in non-obvious
    # ways as registers become unavailable as scratch space.

    # LAB 9: Your code here

    # Restore the trap-time registers.  After you do this, you
    # can no longer modify any general-purpose registers (POPA).
    # LAB 9: Your code here

    # ignore utf_fault_va, utf_err
    addq $16, %rsp
    # restore utf_regs, save rax again
    POPA
    pushq %rax

    # 24 utf_rsp
    # 16 utf_rflags
    #  8 utf_rip
    #  0 rax

    # load utf_rip
    movq 8(%rsp), %rax
    # save rbx to utf_rip
    movq %rbx, 8(%rsp)
    # load utf_rsp
    movq 24(%rsp), %rbx
    # store return address to *utf_stack
    subq $8, 24(%rsp)
    movq %rax, -8(%rbx)
    # restore rax and rbx
    popq %rax
    popq %rbx

    # Restore rflags from the stack.  After you do this, you can
    # no longer use arithmetic operations or anything else that
    # modifies rflags.
    # LAB 9: Your code here
    popfq

    # Switch back to the adjusted trap-time stack.
    # LAB 9: Your code here
    popq %rsp

    # Return to re-execute the instruction that faulted.
    ret
