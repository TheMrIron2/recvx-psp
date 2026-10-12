#!/usr/bin/env python3
import re
import sys
from pathlib import Path


CASES = {7, 11, 12, 13, 24, 47}
CASE_LINE = re.compile(r"^        case\s+(0[xX][0-9a-fA-F]+|[0-9]+)\s*:")


def extract_function(source, name):
    match = re.search(
        r"(?m)^\s*int\s+" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{",
        source,
    )
    if match is None:
        raise ValueError(f"original function not found: {name}")
    opening = source.find("{", match.start())
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
                    return source[match.start():index + 1]
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


def extract_cases(function):
    lines = function.splitlines()
    starts = []
    for index, line in enumerate(lines):
        match = CASE_LINE.match(line)
        if match is not None:
            starts.append((index, int(match.group(1), 0)))
    result = {}
    for position, (start, value) in enumerate(starts):
        if value in CASES:
            end = starts[position + 1][0] if position + 1 < len(starts) else len(lines) - 1
            result[value] = (start, "\n".join(lines[start:end]).rstrip())
    missing = CASES - result.keys()
    if missing:
        raise ValueError(f"original event command cases missing: {sorted(missing)}")
    return result


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: extract_original_event_commands.py SOURCE OUTPUT")
    source_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    source = source_path.read_text(encoding="utf-8")
    function = extract_function(source, "Common_controll")
    line = source.count("\n", 0, source.index(function)) + 1
    cases = extract_cases(function)
    output = [
        '#include "ps2/veronica/prog/event.h"',
        '#include "ps2/veronica/prog/hitchk.h"',
        '#include "ps2/veronica/prog/MdlPut.h"',
        '#include "ps2/veronica/prog/Motion.h"',
        '#include "ps2/veronica/prog/main.h"',
        '#include "ps2/veronica/prog/pwksub.h"',
        '#include "psp/engine_event_core.h"',
        "",
        "int Common_controll(void)",
        "{",
        "    switch (*bhScePtr) {",
    ]
    for command in sorted(CASES):
        case_line, case_body = cases[command]
        output.extend((
            f'#line {line + case_line} "{source_path.as_posix()}"',
            case_body,
        ))
    output.extend((
        "    default:",
        "        recvxEventCoreFailUnsupportedCommand(105, bhScePtr);",
        "        return -1;",
        "    }",
        "    return 0;",
        "}",
        "",
    ))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(output), encoding="utf-8")


if __name__ == "__main__":
    main()
