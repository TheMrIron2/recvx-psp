#!/usr/bin/env python3
import re
import sys
from pathlib import Path


FUNCTIONS = ("bhSetEffect", "bhControlEffect", "bhGetGidx")
HANDLERS = ("bhEff001", "bhEff108", "bhEffFil")


def extract_function(source, name):
    match = re.search(r"(?m)^\s*(?:unsigned\s+int|int|void)\s+" + re.escape(name) + r"\s*\(", source)
    if match is None:
        raise ValueError(f"original function not found: {name}")
    opening = source.find("{", match.end())
    depth = 0
    state = "code"
    escaped = False
    for index in range(opening, len(source)):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "code":
            if char == "/" and following == "/":
                state = "line"
            elif char == "/" and following == "*":
                state = "comment"
            elif char == '"':
                state = "string"
            elif char == "'":
                state = "char"
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    line = source.count("\n", 0, match.start()) + 1
                    return line, source[match.start():index + 1].rstrip()
        elif state == "line":
            if char == "\n":
                state = "code"
        elif state == "comment":
            if char == "*" and following == "/":
                state = "code"
        elif escaped:
            escaped = False
        elif char == "\\":
            escaped = True
        elif state == "string" and char == '"':
            state = "code"
        elif state == "char" and char == "'":
            state = "code"
    raise ValueError(f"unterminated original function: {name}")


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: extract_original_effect_runtime.py EFFECT_SOURCE HANDLER_SOURCE OUTPUT")
    source_path = Path(sys.argv[1])
    handler_path = Path(sys.argv[2])
    output_path = Path(sys.argv[3])
    source = source_path.read_text(encoding="utf-8")
    handler_source = handler_path.read_text(encoding="utf-8")
    output = [
        '#include "ps2/veronica/prog/effect.h"',
        '#include "ps2/veronica/prog/effsub1.h"',
        '#include "ps2/veronica/prog/hitchk.h"',
        '#include "ps2/veronica/prog/main.h"',
        '#include "ps2/veronica/prog/njplus.h"',
        '#include "ps2/veronica/prog/ps2_NaMath.h"',
        '#include "ps2/veronica/prog/ps2_NaMatrix.h"',
        '#include "ps2/veronica/prog/system.h"',
        '#include "psp/engine_original_fault.h"',
        '#include <stdio.h>',
        "",
        "typedef void (*bhJumpEffect_proc)(O_WRK *);",
        "typedef void (*bhJumpEffect0_proc)(O_WRK *);",
        "typedef void (*bhJumpEffect2_proc)(O_WRK *);",
        "typedef void (*bhJumpEffect3_proc)(O_WRK *);",
        "typedef void (*bhJumpEffect4_proc)(O_WRK *);",
        "",
        "bhJumpEffect_proc bhJumpEffect[150] = { [1] = bhEff001, [90] = bhEffFil, [91] = bhEffFil, [92] = bhEffFil, [108] = bhEff108 };",
        "bhJumpEffect0_proc bhJumpEffect0[100] = { 0 };",
        "bhJumpEffect2_proc bhJumpEffect2[50] = { 0 };",
        "bhJumpEffect3_proc bhJumpEffect3[50] = { 0 };",
        "bhJumpEffect4_proc bhJumpEffect4[50] = { 0 };",
        "O_WORK *ef_Zanzo[6] = { 0 };",
        "",
        "static void bhControlEffectOriginal(void);",
    ]
    for name in FUNCTIONS:
        line, function = extract_function(source, name)
        if name == "bhControlEffect":
            function = function.replace("void bhControlEffect()", "static void bhControlEffectOriginal(void)", 1)
        output.extend((f'#line {line} "{source_path.as_posix()}"', function, ""))
    for name in HANDLERS:
        line, function = extract_function(handler_source, name)
        output.extend((f'#line {line} "{handler_path.as_posix()}"', function, ""))
    output.extend((
        "void bhControlEffect(void)",
        "{",
        "    unsigned int index;",
        "    static char fault_message[96];",
        "    if ((sys->sp_flg & 0x8) == 0 && (sys->ef_flg & 0x1) == 0)",
        "        return;",
        "    for (index = 0; index < 512; ++index) {",
        "        O_WRK *effect = &eff[index];",
        "        unsigned int id;",
        "        if ((effect->flg & 0x1) == 0 || (effect->stflg & 0x1000000))",
        "            continue;",
        "        id = (unsigned int)effect->id;",
        "        if (id >= 400)",
        "            continue;",
        "        if ((id < 150 && bhJumpEffect[id] == NULL) ||",
        "            (id >= 150 && id < 250 && bhJumpEffect0[id - 150] == NULL) ||",
        "            (id >= 250 && id < 300 && bhJumpEffect2[id - 250] == NULL) ||",
        "            (id >= 300 && id < 350 && bhJumpEffect3[id - 300] == NULL) ||",
        "            (id >= 350 && id < 400 && bhJumpEffect4[id - 350] == NULL)) {",
        '            snprintf(fault_message, sizeof(fault_message),',
        '                     "unsupported RM_0010 effect ID %u at %u flags %08x",',
        '                     id, index, (unsigned int)effect->flg);',
        "            recvxEngineOriginalFaultSet(fault_message);",
        "            return;",
        "        }",
        "    }",
        "    bhControlEffectOriginal();",
        "}",
        "",
    ))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(output), encoding="utf-8")


if __name__ == "__main__":
    main()
