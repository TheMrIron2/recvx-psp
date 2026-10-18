"""Check full-game dispatch coverage against the original 256-slot table."""
from pathlib import Path
import re

original = {
    int(number): (profile, name)
    for profile, number, name in re.findall(
        r'RECVX_EVENT_(CORE|ENGINE|FULL)\((\d+),\s*(\w+)\)',
        Path('include/ps2/veronica/prog/event_dispatch_table.inc').read_text())
}
assert len(original) == 256
source = Path('src/psp/game_boot_event.c').read_text().split('static const char *const event_names')[0]
actual = {number: name for number, (profile, name) in original.items() if profile == 'CORE'}
for number, name in re.findall(r'\[(\d+)\]\s*=\s*(\w+)', source):
    number = int(number)
    assert number not in actual, f'duplicate opcode {number}'
    actual[number] = name
rows = re.findall(r'RECVX_EVENT_REMAINING\((\d+),\s*(\w+),\s*(\d+)\)',
    Path('include/psp/game_boot_event_remaining.inc').read_text())
for number, name, extent in rows:
    number = int(number)
    assert number not in actual, f'duplicate opcode {number}'
    assert int(extent) > 0 or number in (100, 102)
    actual[number] = name
for number, (_, name) in original.items():
    if name == 'dm0':
        assert number not in actual, f'reserved slot {number} has a handler'
    else:
        assert actual.get(number) == name, f'opcode {number}: expected {name}, got {actual.get(number)}'
assert len(actual) == 223 and len(rows) == 130
print('PASS: all 223 assigned original opcodes mapped exactly; 130 added handlers; 33 reserved slots remain unsupported')
