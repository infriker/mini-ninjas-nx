// Mini Ninjas shader library, adapted from dantes-inferno-nx.
//
// The library (minininjas_shaders.dslib next to the NRO) is the one shader generation builds: each original
// container with its SPIR-V. Each entry becomes the rex::x360::EntradaShader the deko3d renderer draws with.
//
// Identification. A pixel shader arrives in the ring as it is in its container. A vertex shader does not: the
// XDK's Direct3D patches its vertex fetches for each vertex declaration, and sometimes copies it into the ring
// itself (IM_LOAD_IMMEDIATE). So each load is looked up, in order:
//   1. by address, when a game-specific creation hook registers the object and its container;
//   2. by the fingerprint of the patched microcode, learned from loads that step 1 identified (the patch is the
//      same each time for the same shader and declaration);
//   3. by the microcode with the fetch instructions masked (enough for pixel shaders);
// Similarity matching is disabled until validated against this game's shaders.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include <rex/x360/shader_entry.h>

namespace rex::memory {
class Memory;
}

namespace minininjas::shaders {

class Biblioteca {
 public:
  Biblioteca();
  ~Biblioteca();

  bool Cargar(const std::filesystem::path& archivo);
  bool cargada() const;

  // The entry of an original container (the create hooks, before the call).
  const rex::x360::EntradaShader* PorContenedor(std::span<const uint8_t> contenedor) const;

  // After D3DDevice_Create*Shader returned `objeto` (guest address) for `entrada`: finds where the object keeps
  // its microcode and remembers that address. Any thread.
  void RegistrarObjeto(rex::memory::Memory* memoria, uint32_t objeto, const rex::x360::EntradaShader* entrada);

  // The ring's view of an IM_LOAD (fisica = its physical address) or an IM_LOAD_IMMEDIATE (fisica = 0), with the
  // microcode in host byte order. Returns the unmodified XXH3 of those words in hash_crudo even if not identified
  // or the library isn't loaded; nullptr if no step identifies the shader. Ring thread.
  const rex::x360::EntradaShader* Identificar(bool vertices, uint32_t fisica, std::span<const uint32_t> microcodigo,
                                             uint64_t& hash_crudo);

 private:
  struct Datos;
  std::unique_ptr<Datos> datos_;
};

// The one the game uses: loaded by the graphics system, fed by the create hooks.
Biblioteca& Global();

}  // namespace minininjas::shaders
