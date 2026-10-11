#!/usr/bin/env python3
import re
import sys
from pathlib import Path


FUNCTIONS = ("bhObjDmy", "bhControlObjItm")


def extract_function(source, name):
    match = re.search(r"(?m)^\s*void\s+" + re.escape(name) + r"\s*\(", source)
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
    if len(sys.argv) != 3:
        raise SystemExit("usage: extract_original_object_runtime.py SOURCE OUTPUT")
    source_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    source = source_path.read_text(encoding="utf-8")
    output = [
        '#include "ps2/veronica/prog/objitm.h"',
        '#include "ps2/veronica/prog/MdlPut.h"',
        '#include "ps2/veronica/prog/hitchk.h"',
        '#include "ps2/veronica/prog/light.h"',
        '#include "ps2/veronica/prog/main.h"',
        '#include "ps2/veronica/prog/ps2_NaMatrix.h"',
        '#include "ps2/veronica/prog/ps2_NinjaCnk.h"',
        '#include "ps2/veronica/prog/pwksub.h"',
        '#include "ps2/veronica/prog/system.h"',
        '#include "ps2/veronica/prog/weapon.h"',
        '#include "psp/engine_original_fault.h"',
        "",
        "static void bhControlObjItmOriginal(void);",
        "void bhObjDmy(void);",
        "void (*bhJumpObject[101])() = { [0] = bhObjDmy };",
        "void (*bhJumpObject2[13])() = { 0 };",
        "",
    ]
    for name in FUNCTIONS:
        line, function = extract_function(source, name)
        if name == "bhControlObjItm":
            function = function.replace("void bhControlObjItm()", "static void bhControlObjItmOriginal(void)", 1)
            unsupported = (
                ("bhActionWeapon((BH_PWORK*)op);", "RM_0010 object weapon action"),
                ("bhControlAlphaFadeObject(op);", "RM_0010 object alpha fade"),
            )
            for call, reason in unsupported:
                count = function.count(call)
                if count == 0:
                    raise ValueError(f"expected original unsupported call not found: {call}")
                replacement = (
                    f'{{ recvxEngineOriginalFaultSet("{reason}"); return; }}'
                )
                function = function.replace(call, replacement)
        output.extend((f'#line {line} "{source_path.as_posix()}"', function, ""))
    output.extend((
        "void bhControlObjItm(void)",
        "{",
        "    int index;",
        "    int count;",
        "",
        "    if ((sys->sp_flg & 0x4) == 0)",
        "        return;",
        "    count = (sys->cb_flg & 0x40000000) ? 4 : rom->obj_n;",
        "    if (count < 0 || count > (39936 / (int)sizeof(O_WRK))) {",
        '        recvxEngineOriginalFaultSet("RM_0010 object count outside work table");',
        "        return;",
        "    }",
        "    for (index = 0; index < count; ++index) {",
        "        O_WRK *object = &sys->obwp[index];",
        "        unsigned int type;",
        "        if ((object->flg & 0x1) == 0)",
        "            continue;",
        "        if (object->id >= 1200) {",
        "            type = (unsigned int)(object->id - 1200);",
        "            if (type >= 13 || bhJumpObject2[type] == NULL) {",
        '                recvxEngineOriginalFaultSet("unsupported RM_0010 object ID");',
        "                return;",
        "            }",
        "        } else {",
        "            type = object->type;",
        "            if (type >= 101 || bhJumpObject[type] == NULL) {",
        '                recvxEngineOriginalFaultSet("unsupported RM_0010 object type");',
        "                return;",
        "            }",
        "        }",
        "    }",
        "    bhControlObjItmOriginal();",
        "}",
        "",
    ))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(output), encoding="utf-8")


if __name__ == "__main__":
    main()
