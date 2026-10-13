"""Generate nonreturning PSP ABI boundaries for explicitly unported services.

This fixed inventory is reviewed source, never inferred from linker failures at
build time. Data symbols must have real typed storage, not these trampolines.
"""
from pathlib import Path
import re
import sys


def generate(inventory, destination, host=False):
    names = [line.split('#', 1)[0].strip() for line in inventory.read_text().splitlines()]
    names = [name for name in names if name]
    if len(names) != len(set(names)) or any(not re.fullmatch(r'[A-Za-z_]\w*', name) for name in names):
        raise ValueError('invalid or duplicate unported service name')
    lines = ['/* Explicit unported service inventory. Never returns success. */']
    if not host:
        lines += ['.set noreorder']
    for name in names:
        lines += [f'.section .text.{name},"ax",@progbits', f'.globl {name}',
                  f'.type {name}, @function', f'{name}:']
        if host == 'i386':
            lines += ['  subl $8, %esp', f'  pushl $.Lname_{name}',
                      '  call recvxGameBootUnsupported', '  ud2']
        elif host:
            lines += [f'  lea .Lname_{name}(%rip), %rdi', '  jmp recvxGameBootUnsupported']
        else:
            lines += [f'  lui $a0, %hi(.Lname_{name})', f'  addiu $a0, $a0, %lo(.Lname_{name})',
                      '  j recvxGameBootUnsupported', '  nop']
        lines += [f'.size {name}, .-{name}',
                  f'.section .rodata.{name},"a",@progbits', f'.Lname_{name}:', f'  .asciz "{name}"']
    if host:
        lines += ['.section .note.GNU-stack,"",@progbits']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    architecture = 'i386' if '--host32' in sys.argv[3:] else '--host' in sys.argv[3:]
    generate(Path(sys.argv[1]), Path(sys.argv[2]), architecture)
