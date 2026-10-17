"""Check the compiled facial transform's VFPU register layout on the host.

The scalar host build cannot execute the PSP branch. Decode its vtfm4 operand
and evaluate the register rows independently, using the VFPU layout described
by PPSSPP's ReadMatrix and Int_Vtfm, to catch an accidental M/E transpose.
This is an instruction/layout regression check, not a hardware emulator.
"""
import re
import subprocess
import sys


def check(object_path, skin=False):
    assembly = subprocess.check_output(['psp-objdump', '-d', object_path], text=True)
    # Deliberately asymmetric rotation, scale and translation. Identity alone
    # cannot expose the column/row mistake.
    matrix = [0, 2, 0, 0, -3, 0, 0, 0, 0, 0, 0.5, 0, 7, -9, 11, 1]
    point = [0.5, 3, 1.5, 1]
    expected = [-2, -8, 11.75, 1]
    history = []
    transforms = 0
    directions = 0
    for line in assembly.splitlines():
        match = re.match(r'\s*[0-9a-f]+:\s+([0-9a-f]{8})\s+(\S+)\s*(.*)', line)
        if not match:
            continue
        encoding, mnemonic, operands = match.groups()
        if mnemonic != 'vtfm4.q':
            history.append((mnemonic, operands))
            continue
        loads = history[-8:]
        registers = [0.0] * 128
        for column in range(4):
            name, operand = loads[column]
            assert name == 'lv.q' and operand.startswith(f'C6{column}0.q,{column * 16}('), loads
            registers[96 + column * 4:100 + column * 4] = matrix[column * 4:column * 4 + 4]
        for row in range(3):
            name, operand = loads[4 + row]
            assert name == 'lv.s' and operand.startswith(f'S71{row}.s,{row * 4}('), loads
            registers[116 + row] = point[row]
        assert loads[7] in (('vone.s', 'S713.s'), ('vzero.s', 'S713.s')), loads
        w = 1 if loads[7][0] == 'vone.s' else 0
        registers[119] = w
        point[3] = w
        expected = [-9 + 7*w, 1 - 9*w, 0.75 + 11*w, w]
        directions += not w
        opcode = int(encoding, 16)
        destination, source, vector = opcode & 127, (opcode >> 8) & 127, (opcode >> 16) & 127
        assert destination == 28 and vector == 29, operands
        matrix_base = ((source >> 2) & 7) * 16
        assert matrix_base == 96 and source & 3 == 0, operands
        transpose = bool(source & 32)
        result = []
        for row in range(4):
            entries = [registers[matrix_base + (col * 4 + row if transpose else row * 4 + col)]
                       for col in range(4)]
            result.append(sum(value * coord for value, coord in zip(entries, point)))
        assert result == expected, f'{operands}: {result} != {expected}'
        transforms += 1
    if skin:
        assert transforms >= 2 and directions > 0, 'expected skin point and normal transforms'
    else:
        assert transforms >= 4 and directions == 0, 'expected self-check, jaw, tongue and eye transforms'
    print(f'PASS: {transforms} compiled VFPU transforms ({directions} normals) preserve Ninja matrix layout')


if __name__ == '__main__':
    check(sys.argv[1], '--skin' in sys.argv[2:])
