#!/usr/bin/env python3
"""
bin2c.py — embed a binary file as a C unsigned char array.

Replaces the typical `xxd -i` workflow with something portable that:
  - guarantees deterministic output (sorted, fixed width, fixed indent)
  - emits the correct length symbol name
  - works on macOS / Linux / Windows without depending on xxd

Usage:
    bin2c.py <input.bin> <output.c> <symbol_name>

Example:
    bin2c.py kernel.bin kernel_data.c kernel_bin

Produces a file containing:
    unsigned char kernel_bin[]     = { ...bytes... };
    unsigned int  kernel_bin_len   = N;
"""

from __future__ import annotations

import sys
from pathlib import Path

WIDTH = 12  # bytes per row, matches xxd -i


def emit(data: bytes, symbol: str) -> str:
    out: list[str] = []
    out.append(f"/* Auto-generated from kernel.bin by scripts/bin2c.py — do not edit. */")
    out.append(f"unsigned char {symbol}[] = {{")
    for i in range(0, len(data), WIDTH):
        chunk = data[i : i + WIDTH]
        line = "  " + ", ".join(f"0x{b:02x}" for b in chunk)
        if i + WIDTH < len(data):
            line += ","
        out.append(line)
    out.append("};")
    out.append(f"unsigned int {symbol}_len = {len(data)};")
    out.append("")
    return "\n".join(out)


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(f"usage: {argv[0]} <input.bin> <output.c> <symbol>", file=sys.stderr)
        return 2

    in_path = Path(argv[1])
    out_path = Path(argv[2])
    symbol = argv[3]

    if not in_path.is_file():
        print(f"error: {in_path} not found", file=sys.stderr)
        return 1

    data = in_path.read_bytes()
    out_path.write_text(emit(data, symbol), encoding="utf-8")
    print(f"[bin2c] {in_path} ({len(data)} bytes) -> {out_path} (symbol: {symbol})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
