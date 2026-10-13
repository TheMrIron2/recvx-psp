/* Test-only Linux i386 startup for hosts with 32-bit runtime libraries but no
   multilib CRT development package. Use glibc's normal process initialization. */
.globl _start
.type _start, @function
_start:
    xorl %ebp, %ebp
    popl %esi
    movl %esp, %ecx
    andl $-16, %esp
    pushl %eax
    pushl %esp
    pushl %edx
    pushl $0
    pushl $0
    pushl %ecx
    pushl %esi
    pushl $main
    call __libc_start_main
    hlt
.size _start, .-_start
.section .note.GNU-stack,"",@progbits
