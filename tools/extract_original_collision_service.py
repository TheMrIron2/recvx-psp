#!/usr/bin/env python3
import re
import sys
from pathlib import Path


def extract_function(source, name):
    match = re.search(r"(?m)^\s*int\s+" + re.escape(name) + r"\s*\(", source)
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
        raise SystemExit("usage: extract_original_collision_service.py SOURCE OUTPUT")
    source_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    source = source_path.read_text(encoding="utf-8")
    line, function = extract_function(source, "npCollisionCheckCC")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        '\n'.join((
            '#include "ps2/veronica/prog/njplus.h"',
            '#include "ps2/veronica/prog/ps2_NaColi.h"',
            f'#line {line} "{source_path.as_posix()}"',
            function,
            '',
        )),
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
