#!/usr/bin/env python3
"""Compile-check the game's C++ against the installed engine, without Unreal.

Runs clang with -fsyntax-only and the exact flags UnrealBuildTool saved for each
file at the last real build (Intermediate/.../*.cpp.o.rsp). No UBT, no editor, no
shader or DDC work, so it is safe while another project has Unreal busy.

Headers edited since that build have stale .generated.h files: UHT keys the
UCLASS() and GENERATED_BODY() macros by line number. A shadow copy of each stale
.generated.h with the line numbers moved to match is put first on the include
path. What this cannot check: a brand-new UCLASS/USTRUCT/UENUM (no generated
header exists yet) and reflection itself (UHT's job). A real build still has the
last word.

Usage: python3 check_compile.py [file.cpp ...]   (default: every .cpp in the module)
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

GAME = Path(__file__).resolve().parent.parent
SRC = GAME / "Source" / "HangingLake"
ENGINE = Path(os.environ.get("UE_ROOT", "/Users/Shared/Epic Games/UE_5.8"))
BUILD = GAME / "Intermediate/Build/Mac/arm64/UnrealEditor/Development/HangingLake"
UHT = GAME / "Intermediate/Build/Mac/UnrealEditor/Inc/HangingLake/UHT"

MACRO_LINE = re.compile(r"(FID_\w+?_h_)(\d+)(_[A-Z_]+)")
TYPE_LINE = re.compile(r"^\s*(UCLASS|USTRUCT|UINTERFACE)\s*\(")
BODY_LINE = re.compile(r"^\s*GENERATED_(UCLASS_)?BODY\s*\(")


def shadow_generated_headers(out_dir: Path) -> list[str]:
    """Write line-corrected copies of stale .generated.h files. Returns their names."""
    written = []
    for header in sorted(SRC.glob("*.h")):
        gen = UHT / f"{header.stem}.generated.h"
        if not gen.exists():
            continue
        lines = header.read_text().splitlines()
        type_lines = [i + 1 for i, l in enumerate(lines) if TYPE_LINE.match(l)]
        body_lines = [i + 1 for i, l in enumerate(lines) if BODY_LINE.match(l)]
        text = gen.read_text()
        old_prolog = sorted({int(m.group(2)) for m in MACRO_LINE.finditer(text) if m.group(3) == "_PROLOG"})
        old_body = sorted({int(m.group(2)) for m in MACRO_LINE.finditer(text) if m.group(3) != "_PROLOG"})
        if len(old_prolog) != len(type_lines) or len(old_body) != len(body_lines):
            print(f"note: {header.name} gained or lost a reflected type; it needs a real build to check")
            continue
        mapping = dict(zip(old_prolog, type_lines)) | dict(zip(old_body, body_lines))
        if all(k == v for k, v in mapping.items()):
            continue
        fixed = MACRO_LINE.sub(lambda m: f"{m.group(1)}{mapping.get(int(m.group(2)), int(m.group(2)))}{m.group(3)}", text)
        (out_dir / gen.name).write_text(fixed)
        written.append(gen.name)
    return written


def flags_for(cpp: Path, shadow: Path) -> list[str]:
    rsp = BUILD / f"{cpp.name}.o.rsp"
    template = rsp if rsp.exists() else next(p for p in sorted(BUILD.glob("HL*.cpp.o.rsp")))
    args = []
    for raw in template.read_text().replace("\r", "").splitlines():
        line = raw.strip()
        if not line or line == "-c" or line.startswith(("-MD", "-o ", "-gdwarf")):
            continue
        if line.startswith('"') and line.endswith('.cpp"'):
            line = f'"{cpp}"'
        args.append(line)
    return [f'-I"{shadow}"'] + args + ["-fsyntax-only"]


def check(cpp: Path, shadow: Path) -> tuple[Path, int, str]:
    with tempfile.NamedTemporaryFile("w", suffix=".rsp", delete=False) as f:
        f.write("\n".join(flags_for(cpp, shadow)))
        rsp = f.name
    try:
        r = subprocess.run(
            ["xcrun", "clang++", f"@{rsp}", "-fno-color-diagnostics"],
            cwd=ENGINE / "Engine" / "Source",
            capture_output=True,
            text=True,
        )
        return cpp, r.returncode, r.stderr
    finally:
        os.unlink(rsp)


def main() -> int:
    if not BUILD.exists():
        print(f"no previous build at {BUILD}; build the project once in Unreal first")
        return 2
    # Arguments may be paths or bare names from the module (HLFish.cpp).
    files = [Path(a).resolve() if Path(a).exists() else SRC / a for a in sys.argv[1:]] or sorted(SRC.glob("*.cpp"))
    missing = [f for f in files if not f.exists()]
    if missing:
        print("no such file: " + ", ".join(str(f) for f in missing))
        return 2
    with tempfile.TemporaryDirectory() as tmp:
        shadow = Path(tmp)
        for name in shadow_generated_headers(shadow):
            print(f"shadowed {name} (header moved since the last build)")
        with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
            results = list(pool.map(lambda c: check(c, shadow), files))
    failed = 0
    for cpp, code, err in results:
        status = "ok" if code == 0 else "FAIL"
        print(f"{status:4}  {cpp.name}")
        if code != 0:
            failed += 1
            print(err)
        elif err.strip():
            print(err)
    print(f"{len(files) - failed}/{len(files)} files compile")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
