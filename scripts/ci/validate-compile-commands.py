#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 MontroneDSP
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate a CMake-generated compile_commands.json for Sonar CFamily."""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path


EXPECTED_BASENAMES = {
    "gardenia_core_api.cpp",
    "gardenia_core_dummy.cpp",
}


def _command_text(entry: dict) -> str:
    if "arguments" in entry:
        return " ".join(str(part) for part in entry["arguments"])
    return str(entry.get("command", ""))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("compile_commands", type=Path)
    parser.add_argument(
        "--compiler-substr",
        default="clang",
        help="Substring that must appear in every compiler command (default: clang)",
    )
    parser.add_argument(
        "--require-std",
        default="c++17",
        help="Required C++ language standard token (default: c++17)",
    )
    args = parser.parse_args()

    path = args.compile_commands.resolve()
    if not path.is_file():
        print(f"ERROR: compilation database missing: {path}", file=sys.stderr)
        return 1

    try:
        entries = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        print(f"ERROR: invalid JSON: {exc}", file=sys.stderr)
        return 1

    if not isinstance(entries, list) or not entries:
        print("ERROR: compilation database is empty", file=sys.stderr)
        return 1

    failures: list[str] = []
    basenames: set[str] = set()
    unique_files: set[str] = set()
    cxx_entries = 0
    c_entries = 0

    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            failures.append(f"entry[{index}] is not an object")
            continue

        file_value = entry.get("file")
        directory = entry.get("directory")
        command = _command_text(entry)

        if not file_value or not directory or not command:
            failures.append(f"entry[{index}] missing file/directory/command")
            continue

        file_path = Path(file_value)
        if not file_path.is_absolute():
            file_path = Path(directory) / file_path
        dir_path = Path(directory)

        if not file_path.exists():
            failures.append(f"missing source: {file_path}")
        if not dir_path.is_dir():
            failures.append(f"missing directory: {dir_path}")

        if args.compiler_substr.lower() not in command.lower():
            failures.append(
                f"entry[{index}] compiler does not look like {args.compiler_substr}: {command[:120]}"
            )

        basename = file_path.name
        basenames.add(basename)
        unique_files.add(os.path.normcase(str(file_path.resolve())))

        lowered = basename.lower()
        if lowered.endswith((".cpp", ".cc", ".cxx")):
            cxx_entries += 1
            if "-std=c++17" not in command and "-std=gnu++17" not in command:
                failures.append(f"entry[{index}] missing C++17 flag: {basename}")
        elif lowered.endswith(".c"):
            c_entries += 1

        # Preserve include and define information rather than stripping it.
        if "-I" not in command and "/I" not in command and "-isystem" not in command:
            # Shared objects may still compile with only -I via response files;
            # require at least one include form for this small project.
            failures.append(f"entry[{index}] has no include path flags: {basename}")

    missing_expected = sorted(EXPECTED_BASENAMES - basenames)
    if missing_expected:
        failures.append(f"missing expected translation units: {', '.join(missing_expected)}")

    print(f"compile_commands={path}")
    print(f"entries={len(entries)}")
    print(f"unique_translation_units={len(unique_files)}")
    print(f"cxx_entries={cxx_entries}")
    print(f"c_entries={c_entries}")
    print(f"basenames={','.join(sorted(basenames))}")

    if failures:
        print("VALIDATION FAILED", file=sys.stderr)
        for item in failures:
            print(f" - {item}", file=sys.stderr)
        return 1

    print("VALIDATION PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
