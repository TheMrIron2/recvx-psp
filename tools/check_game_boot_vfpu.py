"""Check the compiled facial transform's VFPU register layout on the host.

The scalar host build cannot execute the PSP branch. Decode its vtfm4 operand
and evaluate the register rows independently, using the VFPU layout described
by PPSSPP's ReadMatrix and Int_Vtfm, to catch an accidental M/E transpose.
This is an instruction/layout regression check, not a hardware emulator.
"""
import math
import re
import subprocess
import sys


def check_direct_skin(assembly):
    """Evaluate the actual compiled position/normal kernels, including row scratch."""
    text=assembly.split('<skin_vertex_direct>:',1)[1].split('Disassembly of section',1)[0]
    instructions=[]
    for line in text.splitlines():
        match=re.match(r'\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(\S+)\s*(.*)',line)
        if match and match[1].startswith(('lv.','sv.','vtfm','vone','vzero','vsub','vscl','vadd','vmov')):
            instructions.append((match[1],match[2]))
    blocks=[];current=[];stores=0
    for instruction in instructions:
        current.append(instruction)
        if instruction[0]=='sv.s':stores+=1
        if stores==6:blocks.append(current);current=[];stores=0
    assert len(blocks)==2 and not current,'expected rigid and blended skin kernels'
    first=[0,2,0,0,-3,0,0,0,0,0,0.5,0,7,-9,11,1]
    second=[-1,0,0,0,0,0.25,0,0,0,0,4,0,-13,5,-2,1]
    source=[0.5,3,1.5,float('nan'),1.5,-2,0.25,float('nan')]
    def coordinates(name,size):
        name=name.split('.')[0];kind=name[0];m,c,r=map(int,name[1:])
        assert m in (6,7),name
        return [(m,c,r)] if kind=='S' else [(m,c+i,r) if kind=='R' else (m,c,r+i) for i in range(size)]
    def transformed(matrix,offset,w):
        return [sum(matrix[k*4+r]*source[offset+k] for k in range(3))+matrix[12+r]*w for r in range(3)]
    for block in blocks:
        blended=sum(op=='lv.q' for op,_ in block)==8
        for weight in ([i/10 for i in range(1,10)] if blended else [1]):
            registers={};output={};transforms=0
            def read(name,size):return [registers[key] for key in coordinates(name,size)]
            def write(name,values):registers.update(zip(coordinates(name,len(values)),values))
            for op,args in block:
                parts=args.split(',')
                if op in ('lv.q','lv.s','sv.s'):
                    address=re.fullmatch(r'(-?\d+)\((\w+)\)',parts[1]);assert address,args
                    offset,base=int(address[1]),address[2]
                    if op=='lv.q':
                        assert base in ('a0','a1') and offset in (0,16,32,48),args
                        matrix=first if base=='a0' else second
                        write(parts[0],matrix[offset//4:offset//4+4])
                    elif op=='lv.s':
                        if base=='a2':write(parts[0],[source[offset//4]])
                        else:
                            assert parts[0]=='S633.s' and base=='sp' and offset==0,args
                            write(parts[0],[weight])
                    else:
                        assert base=='a3' and offset in (0,4,8,16,20,24),args
                        output[offset]=read(parts[0],1)[0]
                elif op in ('vone.s','vzero.s'):write(parts[0],[1 if op=='vone.s' else 0])
                elif op=='vtfm4.q':
                    assert parts[1]=='E600.q',args
                    assert not set(coordinates(parts[0],4))&set(coordinates(parts[2],4)),args
                    vector=read(parts[2],4)
                    result=[sum(registers[(6,c,r)]*vector[c] for c in range(4)) for r in range(4)]
                    write(parts[0],result);transforms+=1
                elif op=='vmov.t':write(parts[0],read(parts[1],3))
                elif op in ('vsub.t','vadd.t','vscl.t'):
                    a=read(parts[1],3);b=read(parts[2],1 if op=='vscl.t' else 3)
                    result=[a[i]*b[0] if op=='vscl.t' else a[i]-b[i] if op=='vsub.t' else a[i]+b[i] for i in range(3)]
                    write(parts[0],result)
                else:raise AssertionError((op,args))
            assert transforms==(4 if blended else 2),transforms
            for normal in (0,1):
                a=transformed(first,normal*4,1-normal);b=transformed(second,normal*4,1-normal)
                expected=[a[i]+(b[i]-a[i])*weight if blended else b[i] for i in range(3)]
                for i in range(3):assert math.isclose(output[normal*16+i*4],expected[i],rel_tol=1e-6,abs_tol=1e-5),(output,expected)
    print('PASS: compiled direct skin kernels, all ten weights, untranslated non-unit normals, row scratch and six-word output')

def check(object_path, skin=False):
    assembly = subprocess.check_output(['psp-objdump', '-d', object_path], text=True)
    if skin and '<skin_vertex_direct>:' in assembly:return check_direct_skin(assembly)
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



def check_resident(object_path):
    assembly = subprocess.check_output(['psp-objdump', '-d', object_path], text=True)
    instructions=[]
    for line in assembly.splitlines():
        match=re.match(r'\s*[0-9a-f]+:\s+([0-9a-f]{8})\s+(\S+)\s*(.*)',line)
        if match: instructions.append(match.groups())
    loads=0
    for start in range(len(instructions)-11):
        block=instructions[start:start+12]
        if all(name=='lv.q' and operand.startswith(f'C{3+i//4}{i%4}0.q,{(i%4)*16}(')
               for i,(_,name,operand) in enumerate(block)):
            loads+=1
    assert loads>=2, 'expected resident matrix loads for renderer and self-check'
    ranges=0; bounds=0
    seen={2:0,3:0,4:0,5:0}
    matrix=[0,2,0,0,-3,0,0,0,0,0,0.5,0,7,-9,11,1]
    point=[0.5,3,1.5,1]
    metric=[2,0.25,-0.125,0,0.25,3,0.375,0,-0.125,0.375,4,0,0,0,0,1]
    for i,(encoding,name,operands) in enumerate(instructions):
        if name=='vmin.t' and operands=='C600.t,C600.t,C620.t':
            assert instructions[i+1][1:]==('vmax.t','C610.t,C610.t,C620.t')
            for axis in range(3):
                assert instructions[i-3+axis][1]=='lv.s' and instructions[i-3+axis][2].startswith(f'S62{axis}.s,{axis*4}(')
            stores=[operand for _,op,operand in instructions[i+2:i+20] if op=='sv.s'][:6]
            assert len(stores)==6
            for axis,operand in enumerate(stores):
                assert operand.startswith(f'S6{axis//3}{axis%3}.s,{axis%3*4}('),stores
            bounds+=1
        if name=='vabs.t' and operands=='C620.t,C620.t':
            assert instructions[i-1][1:]==('vdot.t','S630.s,C600.t,C620.t')
            for axis in range(3):
                assert instructions[i+1+axis][1]=='lv.s' and instructions[i+1+axis][2].startswith(f'S60{axis}.s,{axis*4}(')
            assert [entry[1:] for entry in instructions[i+4:i+8]]==[
                ('vdot.t','S631.s,C600.t,C620.t'),('vadd.s','S631.s,S631.s,S631.s'),
                ('vsub.s','S632.s,S630.s,S631.s'),('vadd.s','S633.s,S630.s,S631.s')]
            assert instructions[i+8][1]=='sv.s' and instructions[i+8][2].startswith('S632.s,0(')
            assert instructions[i+9][1]=='sv.s' and instructions[i+9][2].startswith('S633.s,4(')
            ranges+=1
        if name not in ('vtfm4.q','vtfm3.t'):continue
        opcode=int(encoding,16)
        destination,source,vector=opcode&127,(opcode>>8)&127,(opcode>>16)&127
        bank=(source>>2)&7
        if bank not in seen:continue
        assert source&32 and not(source&3), operands
        if bank in (3,4):
            assert name=='vtfm4.q' and destination==28 and vector==29,operands
            expected_w='vone.s' if bank==3 else 'vzero.s'
            assert instructions[i-1][1:]==(expected_w,'S713.s'),operands
            w=int(bank==3);v=point[:3]+[w]
            result=[sum(matrix[col*4+row]*v[col] for col in range(4)) for row in range(4)]
            assert result==[-9+7*w,1-9*w,0.75+11*w,w],result
        elif bank==2:
            assert name=='vtfm4.q' and destination==29 and vector==24,operands
            assert instructions[i-1][1:]==('vdot.t','S631.s,C600.t,C600.t'),operands
            assert instructions[i-4][1:]==('vone.s','S603.s'),operands
            delta=[1.5,1,1.25,1]
            coeff=[1,-2,0.5,0,2,1,-1,0,3,0,2,0,4,-3,5,0]
            shared=sum(delta[row]*sum(metric[col*4+row]*delta[col] for col in range(3)) for row in range(3))
            result=[shared+sum(coeff[col*4+row]*delta[col] for col in range(4)) for row in range(4)]
            assert result==[shared+11.25,shared-5,shared+7.25,shared],result
        else:
            assert name=='vtfm3.t' and destination==26 and vector==24,operands
            previous=instructions[i-1][1:]
            assert previous in (('vsub.t','C600.t,C600.t,C610.t'),('vone.s','S603.s')),operands
            delta=[1.5,1,1.25]
            transformed=[sum(metric[col*4+row]*delta[col] for col in range(3)) for row in range(3)]
            result=sum(delta[row]*transformed[row] for row in range(3))
            expected=2*delta[0]**2+3*delta[1]**2+4*delta[2]**2+0.5*delta[0]*delta[1]-0.25*delta[0]*delta[2]+0.75*delta[1]*delta[2]
            assert result==expected,(result,expected)
        seen[bank]+=1
    assert bounds>=2, "expected strided XYZ min/max kernel and runtime self-check"
    assert ranges>=2, "expected box-distance interval kernel and runtime self-check"
    assert all(count>=2 for count in seen.values()),seen
    print(f'PASS: resident model/normal/metric VFPU layouts and {loads} matrix-load sequences: {seen}; box interval kernels={ranges}, min/max kernels={bounds}')

if __name__ == '__main__':
    if '--resident' in sys.argv[2:]:check_resident(sys.argv[1])
    else:check(sys.argv[1], '--skin' in sys.argv[2:])
