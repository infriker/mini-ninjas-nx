"""Compare the native hooks with functions extracted from your generated tree."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--switch", type=Path, metavar="OUTPUT_NRO")
parser.add_argument("--bench", action="store_true", help="also time both paths on normal RAM")
args = parser.parse_args()
originals = {}
pattern = re.compile(r"^DEFINE_REX_FUNC\((sub_82417FF0|sub_82501EF0|sub_824F9340)\) \{\n.*?^\}$", re.M | re.S)
for unit in (root / "generated").glob("minininjas_recomp.*.cpp"):
    for match in pattern.finditer(unit.read_text()):
        if match[1] in originals:
            raise SystemExit(f"Duplicate original {match[1]}")
        originals[match[1]] = match[0]
if len(originals) != 3:
    raise SystemExit("All three original generated functions are required")

with tempfile.TemporaryDirectory() as temporary:
    tmp = Path(temporary)
    reference = tmp / "original.cpp"
    reference.write_text('#include "minininjas_pch.h"\n' + "\n".join(originals.values()))
    executable = tmp / "test"
    devkit = Path(os.environ.get("DEVKITPRO", "/opt/devkitpro"))
    includes = ["generated", "sdk/include", "build/host/include", "sdk", "sdk/thirdparty/simde",
                "sdk/thirdparty/simde/simde", "sdk/thirdparty/fmt/include",
                "sdk/thirdparty/spdlog/include", "sdk/thirdparty/utfcpp/source",
                "sdk/thirdparty/tomlplusplus/include"]
    flags = ["-std=c++23", "-O3", "-ffp-contract=off", "-fno-strict-aliasing"]
    if args.bench:
        flags.append("-DMINI_NATIVE_BENCH=1")
    flags += [f"-I{root / item}" for item in includes]
    sources = [root / "src/minininjas_native.cpp", root / "src/minininjas_worker_barrier.cpp",
               root / "tools/perf/test_native.cpp", root / "tools/perf/test_worker_barrier.cpp", reference]
    if args.switch:
        cxx = devkit / "devkitA64/bin/aarch64-none-elf-g++"
        flags += ["-march=armv8-a+crc+crypto", "-mtune=cortex-a57", "-mtp=soft",
                  "-ftls-model=local-exec", "-fPIE", "-D__SWITCH__", "-DNX", "-D_GNU_SOURCE",
                  "-DSPDLOG_NO_TZ_OFFSET", f"-I{devkit / 'libnx/include'}"]
        subprocess.run([cxx, *flags, *sources, f"-specs={devkit / 'libnx/switch.specs'}",
                        f"-L{devkit / 'libnx/lib'}", "-lnx", "-o", executable], check=True)
        subprocess.run([devkit / "tools/bin/elf2nro", executable, args.switch], check=True)
    else:
        cxx = os.environ.get("CXX") or shutil.which("clang++-19") or "clang++"
        subprocess.run([cxx, *flags, "-mssse3", "-msse4.1", "-pthread", *sources, "-o", executable], check=True)
        subprocess.run([executable], check=True, timeout=60 if not args.bench else 300)
