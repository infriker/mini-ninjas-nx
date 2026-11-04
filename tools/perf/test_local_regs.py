"""Compare the localized functions to their actual generated counterparts on a host."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("generated", type=Path)
args = parser.parse_args()
generated = args.generated.resolve()
roots = json.loads((root / "tools/perf/local_regs.json").read_text())

def labels(text):
    return re.sub(r"^(\s*loc_([A-F0-9]+):)[ \t]*$", r"\1 reg_test::Mark(0x\2);", text, flags=re.M)

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    bodies, dispatch = tmp / "regs.cpp", tmp / "dispatch.cpp"
    subprocess.run(["node",root/"tools/perf/localize_regs.cjs",generated,bodies,dispatch],check=True)
    localized = bodies.read_text()
    names = re.findall(r"^(?:static REX_FUNC|REX_EXTERN)\(local_regs_(sub_\w+)\);$",localized,re.M)
    originals = {}
    for unit in generated.glob("minininjas_recomp.*.cpp"):
        for m in re.finditer(r"^DEFINE_REX_FUNC\((\w+)\) \{\n(.*?)^}",unit.read_text(),re.M|re.S):
            if m[1] in names:
                originals[m[1]] = m[2]
    assert len(originals)==len(names) and set(roots)<=set(names)
    parts = ['#include "test_local_regs.h"', '#include "minininjas_perf_helpers.h"',
             'bool minininjas::cpu::local_registers_enabled = true;']
    for name in names:
        parts.append(f"static REX_FUNC(reference_{name});")
    externs = {n for n in re.findall(r"^REX_EXTERN\((\w+)\);$",localized,re.M)
               if not n.startswith("local_regs_")}
    for name in sorted(externs):
        parts.append(f"REX_EXTERN({name});")
    def reference_call(m):
        target = m[1].removeprefix("__imp__")
        return f"reference_{target}(ctx, base);" if target in names else m[0]
    for name,body in originals.items():
        baseline = re.sub(r"\b((?:__imp__)?sub_[A-F0-9]{8})\(ctx, base\);",reference_call,body)
        parts.append(f"static REX_FUNC(reference_{name}) {{\n{labels(baseline)}\n}}")
    for i,name in enumerate(sorted(externs)):
        inner = name.removeprefix("__imp__")
        if name in roots: continue  # strong entry is defined by the generated dispatcher
        action = f"reference_{inner}(ctx,base)" if inner in names else f"reg_test::Boundary({i+1},ctx,base)"
        parts.append(f"REX_EXTERN({name}) {{ {action}; }}")
    parts.extend([labels(localized),dispatch.read_text()])
    parts.append("void reg_test::InitRegisters(PPCContext& ctx, unsigned seed) {")
    for i in range(32):
        parts.append(f"  ctx.r{i}.u64=(uint64_t(seed+{i})<<32) | (0x1000+{i}*64);")
        parts.append(f"  ctx.f{i}.f64=(int(seed%13)-6)*0.5 + {i}*0.125;")
    for i in range(128):
        parts.append(f"  for(unsigned j=0;j<4;++j) ctx.v{i}.f32[j]=float(int(seed%9)-4)+{i}*0.03125f+j;")
    for i in range(8):
        parts.append(f"  ctx.cr{i}.set_raw((seed+{i})&15);")
    parts.append("}\nint main() { unsigned all=0, boundaries=0; bool covered=true;")
    for name in names:
        count = 112 if name in roots else 24
        fast = name if name in roots else "local_regs_"+name
        baseline = name if name in roots else "reference_"+name
        parts.append(f'''  {{ unsigned ok=0; reg_test::current="{name}";
  for(unsigned seed=0; seed<{count}; ++seed) {{
    if(reg_test::Compare({baseline},{fast},0x{name[4:]}u,seed)) {{ ++ok; boundaries+=reg_test::event; }}
  }}
  if(ok!={count}) {{ std::fprintf(stderr,"Incomplete cases for {name}\\n"); covered=false; }}
  std::printf("{name} %u/{count}\\n",ok); all+=ok; }}''')
    parts.append('if(!covered) return 1; std::printf("PASS: %u complete comparisons, %u full-context call boundaries\\n",all,boundaries); }')
    cpp=tmp/"test.cpp";cpp.write_text("\n".join(parts))
    inc=[generated,root/"src",root/"tools/perf",root/"sdk/include",root/"build/host/include",root/"sdk",
         *[root/"sdk/thirdparty"/p for p in ("simde","simde/simde","fmt/include","spdlog/include","utfcpp/source","tomlplusplus/include")]]
    exe=tmp/"test"
    subprocess.run(["clang++-19","-std=c++23","-O2","-ffp-contract=off","-fno-strict-aliasing","-msse4.1",
                    *["-I"+str(p) for p in inc],str(cpp),"-o",str(exe)],check=True)
    subprocess.run(["stdbuf","-oL",exe],check=True,timeout=150)
