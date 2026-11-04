"""Check the final unstripped Switch ELF against the calls that bypass weak aliases."""
import argparse
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("elf", type=Path)
parser.add_argument("generated", type=Path)
parser.add_argument("nm")
parser.add_argument("--recorder", action="store_true")
args = parser.parse_args()
table = {}
output = subprocess.check_output([args.nm, "--defined-only", str(args.elf)], text=True)
for line in output.splitlines():
    fields = line.split(maxsplit=2)
    if len(fields) == 3:
        table[fields[2]] = int(fields[0], 16)
if table.get("_start") != 0:
    raise SystemExit("Switch startup code must be at offset zero")

targets = set()
units = list(args.generated.glob("minininjas_recomp.*.cpp"))
calls = re.compile(r"^\s*(?:__imp__(sub_[0-9A-F]{8})|mini_perf_(__\w+))\(ctx, base\);", re.M)
for unit in units:
    targets.update(m[1] or m[2] for m in calls.finditer(unit.read_text()))
if not targets:
    raise SystemExit("No optimized calls found in the specified generated tree")
for name in sorted(targets):
    if name not in table or table[name] != table.get("__imp__" + name):
        raise SystemExit(f"Optimized call would bypass a hook or missing alias: {name}")
manifest = json.loads((args.generated / "perf-manifest.json").read_text())
for name in manifest["nativeOriginals"]:
    if name not in table or "__imp__" + name not in table or table[name] == table["__imp__" + name]:
        raise SystemExit(f"Missing native override or original: {name}")

service_wrappers = {
    "__wrap_" + name for name in (
        "threadCreate", "threadClose", "nvFenceWait", "nvGpuChannelKickoff", "nvMapCreate",
        "nvAddressSpaceAllocFixed", "nvioctlNvhostAsGpu_MapBufferEx", "armDCacheClean",
        "nwindowQueueBuffer", "bqDequeueBuffer", "svcSleepThread",
    )
}
if args.recorder:
    if table["__mini_gcov_end"] - table["__mini_gcov_begin"] != 8 * len(units):
        raise SystemExit("Incomplete table of recorded translation units")
    if "__wrap_nvFenceWait" not in table or "RexSwitchStackSamplingEnabled" not in table:
        raise SystemExit("Recorder is missing the Switch service profiler or stack sampler")
    if any(name.startswith("__gcov_indirect_call_profiler") for name in table):
        raise SystemExit("The recording build contains the unsupported TLS value profiler")
else:
    unexpected = service_wrappers.intersection(table) | {
        symbol for symbol in table if symbol.startswith(("__gcov", "__mini_gcov", "RexSwitchPerf"))
        or "InformarEnviosParciales" in symbol
    }
    if unexpected:
        raise SystemExit("Unexpected profiling code in gameplay ELF: " + ", ".join(sorted(unexpected)))
    print("Gameplay ELF contains no PGO recorder, Switch service profiler, or partial-submit reporter")
print(f"Switch entry, {len(targets)} bypassed aliases and {len(manifest['nativeOriginals'])} native hooks verified")
