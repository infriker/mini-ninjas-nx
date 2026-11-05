#!/usr/bin/env python3
"""Check the real SDK selection rules for the two opt-in fragment optimizations.

Runs the extracted production branches against representative Xenos states.
This checks selection, not deko3d rasterization or Switch performance.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "sdk/src/x360/deko/dibujos.cpp").read_text()
drop = source.split("  // No color to write:", 1)[1].split("  if (control_profundidad & 0x3)", 1)[0]
drop = drop[drop.index("  if (!mascaras && ps)"):]
early = source.split("  if (ps && claves[4] && mascaras && REXCVAR_GET(nfsmw_nativo_z_temprana))", 1)[1]
early = "  if (ps && claves[4] && mascaras && REXCVAR_GET(nfsmw_nativo_z_temprana))" + early.split("  compartidas[64]", 1)[0]
declare = re.search(r"^void DeclararZTemprana\(.*?^}", source, re.M | re.S)[0]

cpp = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
constexpr bool nfsmw_nativo_sin_ps_sin_color = true;
bool nfsmw_nativo_z_temprana = true;
constexpr bool nfsmw_sombras_sin_vegetacion = false, nfsmw_nativo_ps_solo_alfa = false;
#define REXCVAR_GET(x) (x)
namespace gr { constexpr unsigned XE_GPU_REG_RB_COLORCONTROL = 0; }
struct Shader { bool descarta = false; uint32_t salidas = 1; };
constexpr uint32_t kVarianteZTemprana = 1;
bool DropsShader(const Shader* input, uint32_t mascaras, uint32_t color_control) {
  const auto* ps = input;
  const uint32_t r[] = {color_control};
''' + drop + r'''
  return input && !ps;
}
bool Early(const Shader* ps, uint32_t control_profundidad = 2, uint32_t especializacion = 2,
           bool depth_target = true, uint32_t mascaras = 15, bool full_screen = false) {
  uint64_t claves[5] = {0,0,0,0,uint64_t(depth_target)};
  struct { bool pantalla_completa_sin_z; } clase{full_screen};
  uint32_t variante_ps = 0, dibujos_z_temprana_ = 0;
''' + early + r'''
  assert(dibujos_z_temprana_ == unsigned(bool(variante_ps & kVarianteZTemprana)));
  return variante_ps & kVarianteZTemprana;
}
''' + declare + r'''
int main() {
  Shader regular, discard{true,1}, writes_depth{false,0x11};
  assert(DropsShader(&regular,0,0)); // depth/stencil can proceed without the normal PS
  assert(!DropsShader(&regular,15,0));
  assert(!DropsShader(&discard,0,0));
  assert(!DropsShader(&writes_depth,0,0));
  assert(!DropsShader(nullptr,0,0));
  for (unsigned comparison = 0; comparison < 8; ++comparison) {
    assert(DropsShader(&regular,0,comparison)); // alpha testing disabled
    assert(DropsShader(&regular,0,8|comparison) == (comparison == 7)); // only ALWAYS can be ignored
  }
  for (unsigned mask = 1; mask < 0x10000; ++mask) assert(!DropsShader(&regular,mask,0));
  for (unsigned comparison = 0; comparison < 8; ++comparison) {
    const auto test = 2u | (comparison << 4);
    assert(Early(&regular,test)); // alpha-tested, depth read-only
    assert(Early(&discard,test,0)); // shader discard, depth read-only
    assert(!Early(&regular,test|4)); // depth writes
    assert(!Early(&regular,test|1)); // stencil enabled
    assert(!Early(&writes_depth,test));
  }
  assert(!Early(&regular,0));
  assert(!Early(&regular,4)); // depth write bit alone does not enable the test
  assert(!Early(nullptr));
  assert(!Early(&regular,2,2,false));
  assert(!Early(&regular,2,2,true,0));
  assert(!Early(&regular,2,2,true,15,true));
  assert(!Early(&regular,2,0)); // no discard/alpha: allow the hardware's normal early path
  nfsmw_nativo_z_temprana = false; assert(!Early(&regular));
  // All variant compilation paths use this production GLSL declaration helper.
  std::string glsl = "#version 450\n#extension GL_ARB_separate_shader_objects : enable\nlayout(location=0) out vec4 color;\nvoid main() { color=vec4(1); }\n";
  DeclararZTemprana(glsl);
  const auto extension = glsl.find("#extension");
  const auto early_pos = glsl.find("layout(early_fragment_tests) in;");
  const auto output = glsl.find("layout(location=0)");
  assert(extension < early_pos && early_pos < output);
  std::string empty = "#version 450\nvoid main() {}\n";
  DeclararZTemprana(empty);
  assert(empty.find("layout(early_fragment_tests)") < empty.find("void main"));
  std::cout << "Shader guards passed: colour masks, all alpha/depth comparisons, discard, depth/stencil writes, no-depth/full-screen passes and GLSL declaration order\n";
}
'''
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder)
    (path / "test.cpp").write_text(cpp)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-O1", "-g",
                    "-fsanitize=address,undefined", str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True, timeout=30)
