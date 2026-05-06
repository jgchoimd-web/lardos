; syscall_exit_to_kernel: switch stack to saved kernel rsp and ret to usermode_run
BITS 64
GLOBAL syscall_exit_to_kernel
EXTERN g_usermode_return_rsp

syscall_exit_to_kernel:
    ; Pop our trap frame: 15 regs + vec + err = 17*8 = 136 bytes
    ; Plus CPU-pushed: RIP, CS, RFLAGS, RSP, SS = 40 bytes
    add rsp, 176
    mov rsp, [g_usermode_return_rsp]
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
