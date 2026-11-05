#!/usr/bin/env python3
"""Exercise the SDK's actual clear/snapshot control flow with a CPU image backend.

This checks image lifetime and pixel contents, not deko3d execution or GPU timing.
Run after prepare-sdk.sh: python3 tools/test_gpu_targets.py
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "sdk/src/x360/deko/destinos.cpp").read_text()
header = (root / "sdk/include/rex/x360/deko/render.h").read_text()


def method(name):
    pattern = r"^(?:void|bool|uint32_t|const ImagenDk\*) RenderDk::" + re.escape(name) + r"\([^\n]*\).*?^}"
    found = re.findall(pattern, source, re.M | re.S)
    assert len(found) == 1, (name, len(found))
    return found[0]


methods = [method(name) for name in (
    "BorrarProfundidad", "HacerBorradoPendiente", "AltoDeBorrado", "BorrarFilas",
    "CompletarBanda", "MaterializarFrontal", "BorradoRapidoD3D", "DescartarMuestras")]
texture_read = re.search(r"^const ImagenDk\* RenderDk::TexturaResuelta\(uint32_t direccion\) \{.*?^}", source, re.M | re.S)
assert texture_read
methods.append(texture_read[0])
before_write = re.search(r"  void AntesDeCambiarDestino\(.*?\n  }", header, re.S)
assert before_write
# The control flow before the hardware copy is also production code. Only the
# shader submission itself is replaced by a CPU rectangle copy below.
copy_prefix = source.split("bool RenderDk::Copiar3D(", 1)[1].split("  // A 4x resolve", 1)[0]
copy_prefix = "bool RenderDk::Copiar3D(" + copy_prefix
front_guard = re.search(r"      const bool frontal =.*?;", source, re.S)[0]
depth_guard = re.search(r"  const bool entero =.*?;", source, re.S)[0]

cpp = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <vector>
#define REXCVAR_GET(x) true
namespace gr { enum { XE_GPU_REG_RB_DEPTH_INFO, XE_GPU_REG_RB_SURFACE_INFO }; }
namespace xenos {
enum class Endian { None };
enum class FetchConstantType { kVertex = 3 };
uint32_t GpuSwap(uint32_t v, Endian) { return v; }
}
constexpr unsigned kRegFetch = 2, DkColorMask_RGBA = 15;
struct Pixels { unsigned w = 8, h = 16; std::vector<float> data = std::vector<float>(128, -1); };
struct DkImage { Pixels* pixels = nullptr; };
struct DkImageView { Pixels* pixels; };
struct DkScissor { uint32_t x, y, width, height; };
struct DkImageRect { uint32_t x, y, z, width, height, depth; };
struct Gpu { Pixels* target = nullptr; DkScissor scissor{}; unsigned clears = 0, copies = 0; };
using DkCmdBuf = Gpu*;
struct Nucleo { Gpu gpu; DkCmdBuf comandos() { return &gpu; } };
DkImageView VistaCompleta(const DkImage& i) { return {i.pixels}; }
void dkImageViewDefaults(DkImageView* v, const DkImage* i) { *v = VistaCompleta(*i); }
void dkCmdBufBindRenderTarget(DkCmdBuf c, const DkImageView* v, const void*) { c->target = v->pixels; }
void dkCmdBufBindRenderTargets(DkCmdBuf c, const void*, unsigned, const DkImageView* v) { c->target = v->pixels; }
void dkCmdBufSetScissors(DkCmdBuf c, unsigned, const DkScissor* s, unsigned) { c->scissor = *s; }
void clear(DkCmdBuf c, float value) {
  assert(c->target);
  const auto s = c->scissor;
  for (unsigned y = s.y; y < std::min(s.y + s.height, c->target->h); ++y)
    for (unsigned x = s.x; x < std::min(s.x + s.width, c->target->w); ++x)
      c->target->data[y * c->target->w + x] = value;
  ++c->clears;
}
void dkCmdBufClearColorFloat(DkCmdBuf c, unsigned, unsigned, float r, float, float, float) { clear(c, r); }
void dkCmdBufClearDepthStencil(DkCmdBuf c, bool, float z, unsigned, unsigned) { clear(c, z); }
void copy(Gpu& gpu, const Pixels& a, Pixels& b, unsigned x, unsigned y, unsigned w, unsigned h, unsigned dx, unsigned dy) {
  for (unsigned row = 0; row < h; ++row)
    for (unsigned col = 0; col < w; ++col)
      b.data.at((dy + row) * b.w + dx + col) = a.data.at((y + row) * a.w + x + col);
  ++gpu.copies;
}
void dkCmdBufCopyImage(DkCmdBuf c, const DkImageView* a, const DkImageRect* ar,
                       const DkImageView* b, const DkImageRect* br, unsigned) {
  copy(*c, *a->pixels, *b->pixels, ar->x, ar->y, ar->width, ar->height, br->x, br->y);
}
struct ImagenDk {
  DkImage imagen; uint64_t serie = 1;
  uint32_t ancho = 8, alto = 16, alto_usado = 8, alto_borrado = 0;
  uint32_t edram_base = 0, edram_pitch = 8, ms_filas = 0;
  bool profundidad = false, viva = true, edram_64 = false;
  int formato = 0; bool intercambio_rb = false;
  bool contenido_invalido = false, borrado_pendiente = false, banda_sucia = false;
  bool ms_ligada = false, ms_nueva = false, s_nueva = false;
  float borrado_z = 1, borrado_color[4] = {1,1,1,1}; uint8_t borrado_estencil = 0;
};
struct RegistrosCopia { uint32_t rb_depth_info = 0, rb_surface_info = 0, rb_depth_clear = 0xFFFFFF00; };
struct Resuelta { ImagenDk imagen; bool escrita = true; };
struct MotivoMs { MotivoMs(unsigned&, unsigned) {} };
struct Memory {
  std::vector<float> vertices;
  const uint8_t* TranslatePhysical(unsigned) { return reinterpret_cast<const uint8_t*>(vertices.data()); }
};
struct RenderDk {
  Nucleo nucleo_; Memory memory; Memory* memoria_ = &memory;
  ImagenDk* depth = nullptr;
  unsigned ms_motivo_ = 0;
  bool rop_sin_barrera_ = false;
  struct { bool valido = true, pase_valido = true; } g_;
  struct { bool pendiente = false; uint32_t base = 0; ImagenDk* destino = nullptr;
           Resuelta* resuelta = nullptr; uint32_t ancho = 0, alto = 0; } frontal_;
  struct EstadoBorrado { uint64_t valor = 0, dibujos = UINT64_MAX; };
  std::unordered_map<uint64_t, EstadoBorrado> borrado_previo_;
  std::unordered_map<uint64_t, uint64_t> propietario_;
  std::unordered_map<uint64_t, ImagenDk> destinos_, ms_de_;
  std::unordered_map<uint32_t, Resuelta> resueltas_;
  uint64_t dibujados_ = 0, borrados_saltados_ = 0, borrados_profundidad_ = 0, borrados_aplazados_ = 0;
  uint64_t bandas_completadas_ = 0, frontales_copiados_ = 0, borrados_rapidos_ajenos_ = 0;
  uint64_t borrados_rapidos_aplazados_ = 0, ms_borrados_directos_ = 0, borrados_rapidos_ = 0, ms_descartados_ = 0;
  enum { kGpuSubPendiente, kGpuSubBanda, kGpuCopias, kGpuSubBorradoRapido };
  void AsegurarTrabajo() {} void TerminarPase() {} void AntesDe3D() {} void AntesDeTransferir() {}
  void OlvidarEstado() {} void MarcarGpu(unsigned) {} void SombraMinimoAntesDeBorrar(ImagenDk&) {}
  void EscribirMuestras(ImagenDk&) {}
  ImagenDk* DestinoProfundidad(unsigned, unsigned, unsigned) { return depth; }
  bool Copiar3D(const ImagenDk&, uint32_t, uint32_t, uint32_t, uint32_t, ImagenDk&, uint32_t, uint32_t,
                int32_t = 0, float = 1, uint32_t = 1);
  void BorrarProfundidad(const RegistrosCopia&, uint32_t);
  void HacerBorradoPendiente(ImagenDk&);
  uint32_t AltoDeBorrado(const ImagenDk&) const;
  void BorrarFilas(ImagenDk&, uint32_t, uint32_t, uint32_t);
  void CompletarBanda(ImagenDk&, uint32_t, bool, uint32_t);
  void MaterializarFrontal();
  bool BorradoRapidoD3D(const uint32_t*, uint32_t);
  bool DescartarMuestras(ImagenDk&, uint32_t, uint32_t);
  const ImagenDk* TexturaResuelta(uint32_t);
''' + before_write[0] + "\n};\n" + "\n".join(methods) + "\n" + copy_prefix + r'''
  copy(nucleo_.gpu, *origen.imagen.pixels, *destino.imagen.pixels, x0, y0, ancho, alto, dx, dy);
  return true;
}
void rows(const Pixels& p, unsigned first, unsigned last, float expected) {
  for (unsigned y = first; y < last; ++y)
    for (unsigned x = 0; x < p.w; ++x) {
      if (p.data.at(y * p.w + x) != expected)
        std::cerr << "Pixel " << x << ',' << y << " expected " << expected
                  << " got " << p.data.at(y * p.w + x) << '\n';
      assert(p.data.at(y * p.w + x) == expected);
    }
}
int main() {
  RenderDk r; Pixels color; ImagenDk im; im.imagen.pixels = &color;
  im.borrado_pendiente = true;
  assert(r.nucleo_.gpu.clears == 0);
  r.HacerBorradoPendiente(im);
  rows(color, 0, 8, 1); rows(color, 8, 16, -1);
  r.CompletarBanda(im, 9, false, 0);
  rows(color, 0, 16, 1);
  assert(r.nucleo_.gpu.clears == 2 && !im.banda_sucia);
  r.CompletarBanda(im, 16, false, 0);
  assert(r.nucleo_.gpu.clears == 2);

  Pixels z; ImagenDk depth; depth.imagen.pixels = &z; depth.profundidad = true; depth.serie = 2;
  r.depth = &depth; RegistrosCopia reg;
  r.BorrarProfundidad(reg, 8); r.BorrarProfundidad(reg, 8);
  assert(r.borrados_profundidad_ == 1 && r.borrados_saltados_ == 1);
  r.HacerBorradoPendiente(depth);
  ++r.dibujados_; r.BorrarProfundidad(reg, 8);
  assert(r.borrados_profundidad_ == 2);
  r.Copiar3D(im, 0, 0, 8, 16, depth, 0, 0);
  r.BorrarProfundidad(reg, 8);
  assert(r.borrados_profundidad_ == 3); // a copy must invalidate duplicate-clear proof

  // Partial D3D clear reaches rows outside the earlier clear's useful area.
  // Those new pixels must survive the next draw, and untouched rows retain the
  // earlier full clear's value rather than the partial clear's colour.
  RenderDk fast; Pixels pixels; ImagenDk target; target.imagen.pixels = &pixels;
  target.borrado_pendiente = true; fast.destinos_[1] = target;
  fast.borrado_previo_[1] = {0xFFFFFFFFFFFFFFFFull, 0};
  fast.memory.vertices = {0,8,0,0,0,0,0, 8,8,0,0,0,0,0, 8,12,0,0,0,0,0};
  uint32_t regs[4] = {0,8,3,21u << 2};
  assert(fast.BorradoRapidoD3D(regs, 3));
  auto& cleared = fast.destinos_.at(1);
  fast.CompletarBanda(cleared, 16, false, 0);
  rows(pixels, 0, 8, 1); rows(pixels, 8, 12, 0); rows(pixels, 12, 16, 1);
  assert(fast.borrado_previo_.empty());

  // A partial copy into a previously unused band must survive its later use.
  RenderDk copied; Pixels input, output;
  std::fill(input.data.begin(), input.data.end(), 0.5f);
  ImagenDk from, to; from.imagen.pixels = &input; to.imagen.pixels = &output;
  to.borrado_pendiente = true;
  copied.Copiar3D(from,0,0,8,4,to,0,8);
  copied.CompletarBanda(to,16,false,0);
  rows(output,0,8,1); rows(output,8,12,0.5f); rows(output,12,16,1);

  // Full fast clears can coalesce while pending, but must invalidate the last
  // resolve-clear proof even though no ordinary draw was counted.
  fast.borrado_previo_[1] = {0xFFFFFFFFFFFFFFFFull, 0};
  fast.memory.vertices = {0,0,0,0,0,0,0, 8,0,0,0,0,0,0, 8,16,0,0,0,0,0};
  assert(fast.BorradoRapidoD3D(regs,3));
  assert(cleared.borrado_pendiente && fast.borrado_previo_.empty());
  fast.HacerBorradoPendiente(cleared);
  rows(pixels,0,16,0);

  // A direct front presentation keeps a snapshot alive for a subsequent read
  // or clear. The read gets the previous colour before the target is cleared.
  RenderDk front; Pixels shown, saved;
  std::fill(shown.data.begin(), shown.data.end(), 0.25f);
  ImagenDk scene; scene.imagen.pixels = &shown; scene.alto_usado = 16;
  auto& resolved = front.resueltas_[0x1000]; resolved.imagen.imagen.pixels = &saved;
  front.frontal_ = {true,0x1000,&scene,&resolved,8,16};
  scene.borrado_pendiente = true;
  front.HacerBorradoPendiente(scene);
  rows(saved,0,16,0.25f); rows(shown,0,16,1);
  assert(!front.frontal_.pendiente && front.frontales_copiados_ == 1);
  front.frontal_ = {true,0x1000,&scene,&resolved,8,16};
  assert(front.TexturaResuelta(0x1000) == &resolved.imagen);
  rows(saved,0,16,1);
  assert(!front.frontal_.pendiente && front.frontales_copiados_ == 2);

  // Native 4x targets track rows in sample space, with half-height scissor.
  Pixels ms; ms.w = 4; ms.h = 8; ms.data.assign(32,-1);
  im.imagen.pixels = &ms; im.ms_ligada = true;
  auto view = VistaCompleta(im.imagen);
  dkCmdBufBindRenderTarget(r.nucleo_.comandos(), &view, nullptr);
  r.BorrarFilas(im,0,0,8);
  rows(ms,0,4,1); rows(ms,4,8,-1);

  // Compile the real fast-path guards too: no MSAA, format conversion, crop,
  // scaling, or read-back may accidentally use an identity-copy shortcut.
  ImagenDk source, destination;
  Resuelta texture; texture.imagen = destination;
  const ImagenDk* destino_render = &source;
  const ImagenDk* profundidad = &source;
  Resuelta* resuelta = &texture;
  bool frontal_perezoso = true, encoger = false, transforma = false, se_lee = false;
  uint32_t muestras = 1, base_resuelta = 4096, ultimo_frontal_ = 4096;
  uint32_t x0 = 0, y0 = 0, dx = 0, dy = 0, ancho = 8, alto = 16;
  auto can_front = [&] {
''' + front_guard + r'''
    return frontal;
  };
  auto can_depth_swap = [&] {
''' + depth_guard + r'''
    return entero;
  };
  assert(can_front() && can_depth_swap());
  muestras = 2; assert(!can_front() && !can_depth_swap()); muestras = 1;
  texture.imagen.formato = 1; assert(!can_front() && !can_depth_swap()); texture.imagen.formato = 0;
  texture.imagen.intercambio_rb = true; assert(!can_front()); texture.imagen.intercambio_rb = false;
  for (auto* offset : {&x0,&y0,&dx,&dy}) {
    *offset = 1; assert(!can_front() && !can_depth_swap()); *offset = 0;
  }
  --ancho; assert(!can_front() && !can_depth_swap()); ++ancho;
  --alto; assert(!can_front() && !can_depth_swap()); ++alto;
  for (auto* disallow : {&encoger,&transforma,&se_lee}) {
    *disallow = true; assert(!can_front()); *disallow = false;
  }
  ultimo_frontal_ = 8192; assert(!can_front()); ultimo_frontal_ = base_resuelta;
  frontal_perezoso = false; assert(!can_front()); frontal_perezoso = true;
  source.alto = 32; assert(can_front() && !can_depth_swap()); // front may crop unused lower rows
  std::cout << "GPU target lifecycle: deferred/duplicate clears, band growth, copy writes, partial fast clears, front snapshots and 4x rows passed\n";
  std::cout << "Copy guards: compatible 1x surfaces accepted; MSAA, conversions, partial depth resolves and front read-back rejected\n";
}
'''
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder)
    (path / "test.cpp").write_text(cpp)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-O1", "-g",
                    "-fsanitize=address,undefined", str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True, timeout=30)
