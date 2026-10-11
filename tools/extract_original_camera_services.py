#!/usr/bin/env python3
import re
import sys
from pathlib import Path


FUNCTIONS = (
    "bhChangeViewClip",
    "bhChangeViewClipRM",
    "bhChangeClipVolume",
    "bhChangeClipVolumeRM",
)


def extract_function(source, name):
    match = re.search(r"(?m)^\s*void\s+" + re.escape(name) + r"\s*\(", source)
    if match is None:
        raise ValueError(f"original function not found: {name}")
    opening = source.find("{", match.end())
    if opening < 0:
        raise ValueError(f"original function body not found: {name}")

    depth = 0
    state = "code"
    escaped = False
    index = opening
    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "code":
            if char == "/" and following == "/":
                state = "line"
                index += 1
            elif char == "/" and following == "*":
                state = "comment"
                index += 1
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
                index += 1
        else:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif state == "string" and char == '"':
                state = "code"
            elif state == "char" and char == "'":
                state = "code"
        index += 1
    raise ValueError(f"unterminated original function: {name}")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: extract_original_camera_services.py SOURCE OUTPUT")
    source_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    source = source_path.read_text(encoding="utf-8")
    output = [
        '#include "ps2/veronica/prog/event.h"',
        '#include "ps2/veronica/prog/main.h"',
        '#include "ps2/veronica/prog/ps2_NaSystem.h"',
        '#include "ps2/veronica/prog/ps2_NaView.h"',
        '#include "ps2/veronica/prog/ps2_dummy.h"',
        "",
        "float GameNear;",
        "float GameFar;",
        "unsigned int Ps2_ice_flag;",
        "",
    ]
    for name in FUNCTIONS:
        line, function = extract_function(source, name)
        output.extend((f'#line {line} "{source_path.as_posix()}"', function, ""))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(output), encoding="utf-8")


if __name__ == "__main__":
    main()
