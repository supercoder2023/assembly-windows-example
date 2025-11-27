; hello.asm (NASM, Win64, GCC-friendly)
global main
extern puts

section .data
    msg db "Assalamualaikum!", 0

section .text
main:
    ; Windows x64 ABI requires 32 bytes shadow space
    sub rsp, 32          ; Allocate 32 bytes shadow space required by Microsoft x64 ABI

    lea rcx, [rel msg]   ; RCX = pointer to string
    call puts

    add rsp, 32         ; restore stack
    ret
