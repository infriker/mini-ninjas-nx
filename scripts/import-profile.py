#!/usr/bin/env python3
"""Import completed Mini Ninjas GCC 15.2 recordings into a new PGO directory."""
import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

VERSION = b"*25B"
UNIT = re.compile(r"CMakeFiles#minininjas_recomp\.dir#[\w.#-]*minininjas_recomp\.(\d+)\.cpp\.gcda")
ARCS = 0x01A10000


def decode(path, expected):
    data = path.read_bytes()
    units = {}
    seen = set()
    maximum = 0
    pos = 0
    while pos < len(data):
        if data[pos:pos + 8] != b"nfcg" + VERSION:
            raise ValueError(f"{path}: invalid GCC 15.2 stream at {pos}")
        length, = struct.unpack_from("<I", data, pos + 8)
        pos += 12
        name = data[pos:pos + length]
        if not name.endswith(b"\0"):
            raise ValueError(f"{path}: incomplete filename")
        name = name[:-1].decode("ascii")
        match = UNIT.fullmatch(name)
        if not match or match[1] in seen:
            raise ValueError(f"{path}: unexpected or repeated unit {name!r}")
        seen.add(match[1])
        pos += length
        start = pos
        if data[pos:pos + 8] != b"adcg" + VERSION:
            raise ValueError(f"{path}: invalid gcda header for {name}")
        pos += 16
        while True:
            tag, = struct.unpack_from("<I", data, pos)
            pos += 4
            if tag == 0:
                break
            size, = struct.unpack_from("<i", data, pos)
            pos += 4
            # A negative length is GCC's compact representation of all-zero counters.
            end = pos + max(0, size)
            if end > len(data) or (tag == ARCS and size % 8):
                raise ValueError(f"{path}: invalid record length in {name}")
            if tag == ARCS and size > 0:
                maximum = max(maximum, max(n for n, in struct.iter_unpack("<Q", memoryview(data)[pos:end])))
            pos = end
        units[name] = data[start:pos]
    if seen != expected or not maximum:
        raise ValueError(f"{path}: incomplete or empty recording ({len(seen)} translation units)")
    return units, maximum


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("recordings", type=Path, nargs="+", help="latest snapshot from each distinct run")
    args = parser.parse_args()
    destination = args.destination.resolve()
    if destination.exists():
        parser.error("choose a new destination directory")
    root = Path(__file__).resolve().parents[1]
    generated = Path(os.getenv("MINININJAS_GENERATED_DIR", root / "generated-perf"))
    expected = {p.name.split(".")[1] for p in generated.glob("minininjas_recomp.*.cpp")}
    if not expected:
        parser.error(f"no generated translation units in {generated}")
    if len({p.resolve().parent for p in args.recordings}) != len(args.recordings):
        parser.error("pass one snapshot per run folder to avoid counting a session twice")
    devkit = Path(os.getenv("DEVKITPRO", "/opt/devkitpro"))
    gcov_tool = devkit / "devkitA64/bin/aarch64-none-elf-gcov-tool"
    with tempfile.TemporaryDirectory(dir=destination.parent) as tmp:
        work = Path(tmp)
        combined = None
        identity = None
        for i, recording in enumerate(args.recordings):
            units, maximum = decode(recording, expected)
            ids = {name: raw[:16] for name, raw in units.items()}
            if identity is not None and ids != identity:
                raise ValueError(f"{recording}: profiles come from different recorder builds")
            identity = ids
            current = work / f"run-{i}"
            current.mkdir()
            # The freestanding GCC writer omits OBJECT_SUMMARY. A hosted GCC
            # exit adds one run and the run-wide maximum arc count to every TU.
            summary = struct.pack("<IIII", 0xA1000000, 8, 1, min(maximum, 0xFFFFFFFF))
            for name, raw in units.items():
                (current / name).write_bytes(raw[:16] + summary + raw[16:])
            print(f"{recording}: {len(units)} units, peak arc count {maximum}")
            if combined is None:
                combined = current
            else:
                merged = work / f"merged-{i}"
                subprocess.run([gcov_tool, "merge", combined, current, "-o", merged], check=True)
                if {p.name for p in merged.glob("*.gcda")} != set(units):
                    raise ValueError("GCC did not merge all translation units")
                combined = merged
        combined.rename(destination)


if __name__ == "__main__":
    main()
