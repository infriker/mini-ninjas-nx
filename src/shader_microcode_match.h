// Compare Xenos shaders while retaining the original fetch swizzles for rendering.
#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <rex/x360/shader_entry.h>

namespace minininjas::shaders {
inline bool SamePatchedShader(std::span<const uint32_t> runtime,
                              std::span<const uint32_t> original,
                              std::span<const rex::x360::ElementoVertice> elements) {
  if (runtime.size() != original.size()) return false;
  for (const auto& element : elements) {
    if (std::size_t(element.instruccion) * 3 + 2 >= runtime.size()) return false;
  }
  constexpr uint32_t masks[] = {0x0007FFFFu, 0x80000000u, 0x80000000u};
  for (std::size_t word = 0; word < runtime.size(); ++word) {
    uint32_t mask = 0xFFFFFFFFu;
    for (const auto& element : elements) {
      if (word / 3 == element.instruccion) {
        mask = masks[word % 3];
        break;
      }
    }
    if ((runtime[word] & mask) != (original[word] & mask)) return false;
  }
  return true;
}

// Compare a known region of independent r0.x vertex fetches by a bijection of
// unique destination registers, while requiring every non-fetch word to match.
inline bool SamePermutedFetchShader(std::span<const uint32_t> runtime,
                                    std::span<const uint32_t> original,
                                    std::span<const rex::x360::ElementoVertice> elements) {
  if (runtime.size() != original.size() || elements.empty() || elements.size() > 32) return false;
  uint64_t used = 0;
  for (const auto& src : elements) {
    const std::size_t i = std::size_t(src.instruccion) * 3;
    if (i + 2 >= original.size() || src.manual || (original[i] & 31) != 0 ||
        ((original[i] >> 5) & 63) != 0 || (original[i] >> 30) != 0) return false;
    std::size_t match = elements.size();
    for (std::size_t j = 0; j < elements.size(); ++j) {
      const std::size_t k = std::size_t(elements[j].instruccion) * 3;
      if (k + 2 >= runtime.size() || (runtime[k] & 31) != 0 ||
          ((runtime[k] >> 5) & 63) != 0 || (runtime[k] >> 30) != 0) return false;
      if ((runtime[k] & 0x0007FFFFu) == (original[i] & 0x0007FFFFu) &&
          (runtime[k + 1] & 0x80000000u) == (original[i + 1] & 0x80000000u) &&
          (runtime[k + 2] & 0x80000000u) == (original[i + 2] & 0x80000000u)) {
        if (match != elements.size()) return false;
        match = j;
      }
    }
    if (match == elements.size() || (used & (uint64_t{1} << match))) return false;
    used |= uint64_t{1} << match;
  }
  for (std::size_t i = 0; i < runtime.size(); ++i) {
    bool fetch = false;
    for (const auto& element : elements) fetch |= i / 3 == element.instruccion;
    if (!fetch && runtime[i] != original[i]) return false;
  }
  return true;
}

// Metadata equivalence for verified, title-specific aliases. Shader bytecode
// equivalence and the exact container IDs must be checked by the caller.
inline bool SameAliasMetadata(const rex::x360::EntradaShader& a, const rex::x360::EntradaShader& b) {
  if (!a.vertices || !b.vertices || !a.ubicaciones_por_orden || !b.ubicaciones_por_orden ||
      a.microcodigo != b.microcodigo || a.elementos.size() != b.elementos.size() ||
      a.samplers.size() != b.samplers.size() || a.salidas != b.salidas || a.kills != b.kills ||
      a.descarta != b.descarta || a.sombra_minimo != b.sombra_minimo ||
      a.constantes_bytes != b.constantes_bytes || a.blendindices_float != b.blendindices_float ||
      a.glsl != b.glsl) return false;
  for (std::size_t i = 0; i < a.samplers.size(); ++i) {
    if (a.samplers[i].registro != b.samplers[i].registro || a.samplers[i].tipo != b.samplers[i].tipo ||
        a.samplers[i].mapa_sombras != b.samplers[i].mapa_sombras) return false;
  }
  for (std::size_t i = 0; i < a.elementos.size(); ++i) {
    const auto& x = a.elementos[i];
    const auto& y = b.elementos[i];
    if (x.instruccion != y.instruccion || x.uso != y.uso || x.manual != y.manual ||
        x.redondeado != y.redondeado || x.indice_valido != y.indice_valido ||
        x.indice_registro != y.indice_registro || x.indice_componente != y.indice_componente) return false;
    if (x.indice_uso != y.indice_uso &&
        !(i == 1 && x.uso == 10 && ((x.indice_uso == 0 && y.indice_uso == 3) ||
                                    (x.indice_uso == 3 && y.indice_uso == 0)))) return false;
    // Ordinal location assignment depends on the pattern of repeated semantics.
    for (std::size_t j = 0; j < i; ++j) {
      const bool same_a = a.elementos[j].uso == x.uso && a.elementos[j].indice_uso == x.indice_uso;
      const bool same_b = b.elementos[j].uso == y.uso && b.elementos[j].indice_uso == y.indice_uso;
      if (same_a != same_b) return false;
    }
  }
  return true;
}
}  // namespace minininjas::shaders
