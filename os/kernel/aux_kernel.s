; 보조 모놀리식 커널: 코어 1에서 0x200000에서 실행.
; 최소 기능 - 스택 설정 후 간단한 태스크(공유 메모리에 표시) 및 HLT 루프.

BITS 64
GLOBAL aux_kernel_start
aux_kernel_start:
    ; 스택 설정 (2-4MB 영역)
    mov rsp, 0x2FF000

    ; 공유 메모리 (BSP가 읽을 수 있음): 0x2FE000에 "Aux" 표시
    mov rax, 0x2FE000
    mov byte [rax + 0], 'A'
    mov byte [rax + 1], 'u'
    mov byte [rax + 2], 'x'
    mov byte [rax + 3], 0

.aux_loop:
    ; 최소 타이머 대기 (간단한 카운터 증가)
    mov rbx, 0x2FE010
    add dword [rbx], 1

    cli
    hlt
    jmp .aux_loop

GLOBAL aux_kernel_end
aux_kernel_end:

section .note.GNU-stack noalloc noexec nowrite progbits
