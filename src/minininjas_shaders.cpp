// Mini Ninjas shader library, adapted from dantes-inferno-nx.
//
// Dead Space 2's containers are the 2008 format (XenosRecomp shader.h, signature 0x102A1100/1101): the shader
// header is at +24, the microcode is `size` bytes at `physicalOffset`
// of the physical part, the vertex elements follow the header at +0x24, the pixel shader outputs are at +0x1C.
// The constant table is the same. All fields are big-endian.

#include "minininjas_shaders.h"
#include "minininjas_spirv_input.h"
#include "shader_microcode_match.h"

#include "ds_shader_library.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <map>
#include <mutex>
#include <unordered_map>

#include <rex/logging.h>
#include <rex/cvar.h>
#include <rex/memory/utils.h>
#include <rex/system/xmemory.h>

// xxHash reads with memcpy under GCC's strict aliasing (see shaders/ds_shader_library.cpp).
#undef XXH_FORCE_MEMORY_ACCESS
#define XXH_FORCE_MEMORY_ACCESS 0
#define XXH_INLINE_ALL
#include <xxhash.h>
#include "minininjas_foliage_spirv.h"

REXCVAR_DECLARE(bool, minininjas_foliage_fast_alpha);

#if MINININJAS_FULL_FRAME
#include "minininjas_water_spirv.h"
#endif

namespace minininjas::shaders {
namespace {

using rex::x360::ElementoVertice;
using rex::x360::EntradaShader;
using rex::x360::SamplerShader;

// Bits of a vertex fetch instruction that D3D does not patch.
constexpr uint32_t kFetchConserva[3] = {0x0007FFFF, 0x80000FFF, 0x80000000};
// Words of a new shader object searched for the pointer to its microcode.
constexpr uint32_t kPalabrasObjeto = 48;
constexpr uint32_t kMaxAvisos = 24;

struct Contenedor {
  const std::vector<uint8_t>& o;
  bool Hay(size_t p, size_t n) const { return p <= o.size() && n <= o.size() - p; }
  uint32_t U32(size_t p) const {
    return uint32_t(o[p]) << 24 | uint32_t(o[p + 1]) << 16 | uint32_t(o[p + 2]) << 8 | o[p + 3];
  }
  uint16_t U16(size_t p) const { return uint16_t(uint32_t(o[p]) << 8 | o[p + 1]); }
};

// OpKill, OpTerminateInvocation, OpDemoteToHelperInvocation.
uint32_t ContarKills(std::span<const uint32_t> spirv) {
  if (spirv.size() < 5 || spirv[0] != 0x07230203u) return 2;
  uint32_t kills = 0;
  for (size_t i = 5; i < spirv.size();) {
    const uint32_t palabras = spirv[i] >> 16, codigo = spirv[i] & 0xFFFF;
    if (palabras == 0 || i + palabras > spirv.size()) return 2;
    if (codigo == 252 || codigo == 4416 || codigo == 5380) ++kills;
    i += palabras;
  }
  return kills;
}

// The 2008 container into an entry. Returns the reason if it cannot be read.
const char* Leer(const ds::Shader& shader, EntradaShader& e) {
  const Contenedor c{shader.original};
  if (!c.Hay(0, 36)) return "contenedor demasiado corto";
  const uint32_t virtuales = c.U32(4);
  const uint32_t tabla = c.U32(16);
  const uint32_t cabecera = c.U32(24);
  if (size_t(cabecera) + (shader.vertices ? 0x24 : 0x20) > virtuales) return "cabecera fuera de la parte virtual";
  const uint32_t desplazamiento = c.U32(cabecera);
  const uint32_t bytes = c.U32(cabecera + 4);
  if (!bytes || (bytes % 4) || !c.Hay(size_t(virtuales) + desplazamiento, bytes)) {
    return "microcodigo fuera del contenedor";
  }
  e.vertices = shader.vertices;
  e.ubicaciones_por_orden = shader.vertices;
  e.microcodigo.resize(bytes / 4);
  for (size_t i = 0; i < e.microcodigo.size(); ++i) {
    e.microcodigo[i] = c.U32(size_t(virtuales) + desplazamiento + i * 4);
  }

  if (e.vertices) {
    const uint32_t previos = c.U32(cabecera + 0x18);
    const uint32_t cantidad = c.U32(cabecera + 0x1C);
    const size_t comienzo = size_t(cabecera) + 0x24 + size_t(previos) * 4;
    if (cantidad > 64 || previos > 1024 || comienzo + size_t(cantidad) * 4 > virtuales) {
      return "elementos de vertices fuera de la parte virtual";
    }
    for (uint32_t i = 0; i < cantidad; ++i) {
      const uint32_t valor = c.U32(comienzo + size_t(i) * 4);
      ElementoVertice elemento;
      elemento.instruccion = uint16_t(valor & 0xFFF);
      elemento.uso = uint8_t((valor >> 12) & 0xF);
      elemento.indice_uso = uint8_t((valor >> 16) & 0xF);
      if ((size_t(elemento.instruccion) + 1) * 3 > e.microcodigo.size()) {
        return "elemento de vertices apunta fuera del microcodigo";
      }
      // A fetch whose index is not r0.x (the vertex's own index) is read by the shader itself (ElementoVertice::manual);
      // the translator (shaders/XenosRecomp) uses the same rule on the same word. The source register and its
      // component are not patched by D3D, but the mask below drops the component: read before it.
      const uint32_t w0 = e.microcodigo[size_t(elemento.instruccion) * 3];
      const uint32_t w1 = e.microcodigo[size_t(elemento.instruccion) * 3 + 1];
      elemento.indice_registro = uint8_t((w0 >> 5) & 0x3F);
      elemento.indice_componente = uint8_t((w0 >> 30) & 0x3);
      elemento.indice_valido = true;
      elemento.manual = elemento.indice_registro != 0 || elemento.indice_componente != 0;
      elemento.redondeado = (w1 >> 15) & 0x1;
      e.elementos.push_back(elemento);
      for (size_t j = 0; j < 3; ++j) e.microcodigo[size_t(elemento.instruccion) * 3 + j] &= kFetchConserva[j];
    }
  } else {
    e.salidas = c.U32(cabecera + 0x1C);
  }

  // Constant table: the float registers read and the samplers.
  if (!tabla || !c.Hay(size_t(tabla) + 4, 28)) return "sin tabla de constantes";
  const size_t base = size_t(tabla) + 4;
  const uint32_t constantes = c.U32(base + 12);
  const uint32_t info = c.U32(base + 16);
  if (constantes > 1024 || !c.Hay(base + info, size_t(constantes) * 20)) return "tabla de constantes fuera";
  uint32_t registros_float = 0;
  for (uint32_t i = 0; i < constantes; ++i) {
    const size_t p = base + info + size_t(i) * 20;
    if (c.U16(p + 4) == 2) {  // RegisterSet::Float4
      const uint32_t indice = c.U16(p + 6), cuantos = c.U16(p + 8);
      registros_float = std::max(registros_float, cuantos > 1 ? (e.vertices ? 256u : 224u) : indice + 1);
    }
  }
  e.constantes_bytes = std::min<uint32_t>(std::max<uint32_t>(registros_float, 1), 256) * 16;
  for (uint32_t i = 0; i < constantes; ++i) {
    const size_t p = base + info + size_t(i) * 20;
    if (c.U16(p + 4) != 3) continue;  // RegisterSet::Sampler
    SamplerShader sampler;
    sampler.registro = c.U16(p + 6);
    const uint32_t tipo = c.U32(p + 12);
    sampler.tipo = c.Hay(base + tipo, 4) ? c.U16(base + tipo + 2) : 0;
    e.samplers.push_back(sampler);
  }
  e.huella = XXH3_64bits(e.microcodigo.data(), e.microcodigo.size() * sizeof(uint32_t));
  return nullptr;
}

// The dimension each texture binding really has in the SPIR-V: the translator makes a sampler a cube when every fetch
// of it is a cube, whatever the constant table says (Dead Space 2's g_envSampler is declared sampler2D), and the
// renderer checks the fetch constant against the sampler's type. deko3d bindings: set 0, binding = register.
void TiposDesdeSpirv(std::span<const uint32_t> spirv, std::vector<SamplerShader>& samplers) {
  if (spirv.size() < 5 || spirv[0] != 0x07230203u || samplers.empty()) return;
  std::unordered_map<uint32_t, uint32_t> dimension;  // OpTypeImage id -> Dim
  std::unordered_map<uint32_t, uint32_t> apunta;     // OpTypePointer id -> pointee
  std::unordered_map<uint32_t, uint32_t> tipo_var;   // OpVariable id -> its pointer type
  std::unordered_map<uint32_t, uint32_t> binding, conjunto;
  for (size_t i = 5; i < spirv.size();) {
    const uint32_t palabras = spirv[i] >> 16, op = spirv[i] & 0xFFFF;
    if (!palabras || i + palabras > spirv.size()) return;
    const uint32_t* w = &spirv[i];
    if (op == 25 && palabras >= 4) dimension[w[1]] = w[3];           // OpTypeImage
    else if (op == 32 && palabras >= 4) apunta[w[1]] = w[3];         // OpTypePointer
    else if (op == 59 && palabras >= 4) tipo_var[w[2]] = w[1];       // OpVariable
    else if (op == 71 && palabras >= 4 && w[2] == 33) binding[w[1]] = w[3];   // OpDecorate Binding
    else if (op == 71 && palabras >= 4 && w[2] == 34) conjunto[w[1]] = w[3];  // OpDecorate DescriptorSet
    i += palabras;
  }
  for (const auto& [var, b] : binding) {
    const auto c = conjunto.find(var);
    if (c == conjunto.end() || c->second != 0) continue;
    const auto t = tipo_var.find(var);
    if (t == tipo_var.end()) continue;
    const auto p = apunta.find(t->second);
    if (p == apunta.end()) continue;
    const auto d = dimension.find(p->second);
    if (d == dimension.end()) continue;
    for (SamplerShader& sampler : samplers) {
      if (sampler.registro != b) continue;
      if (d->second == 3) sampler.tipo = 14;       // Dim Cube: SamplerCube
      else if (d->second == 2) sampler.tipo = 13;  // Dim 3D: Sampler3D
    }
  }
}

// The original and the corrected shader libraries share the same guest
// container hash. Inspect the actual SPIR-V input ABI before selecting Dk's
// integer versus float/normalized attribute representation.
const char* TipoIndicesDesdeSpirv(EntradaShader& e) {
  if (!e.vertices) return nullptr;
  const InputAbi selected=BlendIndicesAbi(e.spirv,e.elementos);
  if (selected==InputAbi::kMissingOrInvalid)
    return "SPIR-V BLENDINDICES input ABI missing, mixed or unsupported";
  e.blendindices_float=selected==InputAbi::kFloat4;
  return nullptr;
}

// These two exact Mini Ninjas container pairs differ only in COLOR usage index.
// Their exact SPIR-V was independently proven identical after removal of debug
// names. Check both code hashes and all
// renderer-relevant metadata before allowing either member as a representative.
bool VerifiedAlias(const EntradaShader& a, const EntradaShader& b) {
  auto expected = [](uint64_t container) -> uint64_t {
    switch (container) {
      case 0x47AEA392A438E5D0ULL: return 0xFA40E5E9E1A7F56DULL;
      case 0xCE83C1C4894AA17AULL: return 0xEE7436FB2178A125ULL;
      case 0x1047FAEDCEC37BF7ULL: return 0x5655BB31768CFE33ULL;
      case 0xE49D1E59E1D7A80AULL: return 0xEA7C65D9CE58142FULL;
      default: return 0;
    }
  };
  const auto lo = std::min(a.huella_contenedor, b.huella_contenedor);
  const auto hi = std::max(a.huella_contenedor, b.huella_contenedor);
  const bool pair = (lo == 0x47AEA392A438E5D0ULL && hi == 0xCE83C1C4894AA17AULL) ||
                    (lo == 0x1047FAEDCEC37BF7ULL && hi == 0xE49D1E59E1D7A80AULL);
  return pair && XXH3_64bits(a.spirv.data(), a.spirv.size_bytes()) == expected(a.huella_contenedor) &&
         XXH3_64bits(b.spirv.data(), b.spirv.size_bytes()) == expected(b.huella_contenedor) &&
         SameAliasMetadata(a, b);
}

}  // namespace

struct Biblioteca::Datos {
  ds::BibliotecaShaders biblioteca;
  std::vector<EntradaShader> entradas;
  std::unordered_map<const void*, uint32_t> por_shader;
  std::map<std::pair<bool, uint32_t>, std::vector<uint32_t>> candidatos;  // (vertices, words) -> entries
  bool cargada = false;

  std::mutex mutex;  // the maps below: the create hooks run on game threads, IM_LOAD on the ring thread
  std::unordered_map<uint32_t, const EntradaShader*> por_direccion;  // physical address of the microcode
  std::unordered_map<uint64_t, const EntradaShader*> aprendidas;     // Huella(patched microcode)
  std::unordered_map<uint64_t, const EntradaShader*> por_microcodigo;  // step 3 results, nullptr included
  std::vector<uint32_t> temporal;
  std::vector<uint32_t> foliage_spirv;  // owns the patched span for the renderer's lifetime
#if MINININJAS_FULL_FRAME
  // EntradaShader stores spans: each patched module must outlive the renderer.
  std::array<std::vector<uint32_t>, 2> water_spirv;
#endif
  uint32_t avisos_objetos = 0, avisos_cargas = 0, avisos_direccion = 0;
};

Biblioteca::Biblioteca() : datos_(std::make_unique<Datos>()) {}
Biblioteca::~Biblioteca() = default;

bool Biblioteca::cargada() const { return datos_->cargada; }

bool Biblioteca::Cargar(const std::filesystem::path& archivo) {
  Datos& d = *datos_;
  // Pointers to entries are retained by the renderer. Do not invalidate them
  // by reloading the library after the GPU has started.
  if (d.cargada) return true;
  try {
    d.biblioteca.Cargar(archivo);
  } catch (const std::exception& e) {
    REXLOG_WARN("[shaders] library not available ({}): {}", archivo.string(), e.what());
    return false;
  }
  const auto& shaders = d.biblioteca.shaders();
  d.entradas.clear();
  d.entradas.reserve(shaders.size());
  uint32_t ignorados = 0;
  for (uint32_t i = 0; i < shaders.size(); ++i) {
    EntradaShader e;
    e.juego = &shaders[i];
    e.spirv = shaders[i].spirv;
    e.huella_contenedor = shaders[i].huella;
    e.numero = i;
    if (const char* motivo = Leer(shaders[i], e)) {
      if (++ignorados <= 8) REXLOG_WARN("[shaders] container {} skipped: {}", i, motivo);
      continue;
    }
    if (!e.vertices && REXCVAR_GET(minininjas_foliage_fast_alpha)) {
      const auto result = foliage::Patch(e.huella_contenedor, e.spirv, d.foliage_spirv);
      if (result == foliage::Result::Applied) {
        e.spirv = d.foliage_spirv;  // its SPIR-V hash also selects a separate DKSH cache entry
      } else if (result == foliage::Result::Rejected) {
        REXLOG_WARN("[shaders] foliage shader differs from the verified version; retaining original");
      }
    }
#if MINININJAS_FULL_FRAME
    if (!e.vertices && (e.huella_contenedor == 0x6A90B5F4EDDA9DA3ull ||
                        e.huella_contenedor == 0x1F62A0E8A64AA329ull)) {
      auto& patched = d.water_spirv[e.huella_contenedor == 0x6A90B5F4EDDA9DA3ull ? 0 : 1];
      if (water::Patch(e.huella_contenedor, e.spirv, patched) == water::Result::Applied) {
        e.spirv = patched;
      } else {
        REXLOG_WARN("[shaders] refraction shader {:016X} differs from the verified version; retaining original",
                    e.huella_contenedor);
      }
    }
#endif
    if (const char* motivo = TipoIndicesDesdeSpirv(e)) {
      if (++ignorados <= 8) REXLOG_WARN("[shaders] container {} skipped: {}", i, motivo);
      continue;
    }
    TiposDesdeSpirv(e.spirv, e.samplers);
    if (!e.vertices) {
      e.kills = ContarKills(e.spirv);
      e.descarta = e.kills != 1;  // 1 = only the alpha test's
    }
    d.entradas.push_back(std::move(e));
  }
  for (uint32_t i = 0; i < d.entradas.size(); ++i) {
    const EntradaShader& e = d.entradas[i];
    d.candidatos[{e.vertices, uint32_t(e.microcodigo.size())}].push_back(i);
    d.por_shader[e.juego] = i;
  }
  d.cargada = !d.entradas.empty();
  return d.cargada;
}

const EntradaShader* Biblioteca::PorContenedor(std::span<const uint8_t> contenedor) const {
  const Datos& d = *datos_;
  if (!d.cargada) return nullptr;
  const ds::Shader* shader = d.biblioteca.Buscar(contenedor);
  if (!shader) return nullptr;
  const auto it = d.por_shader.find(shader);
  return it != d.por_shader.end() ? &d.entradas[it->second] : nullptr;
}

void Biblioteca::RegistrarObjeto(rex::memory::Memory* memoria, uint32_t objeto, const EntradaShader* entrada) {
  Datos& d = *datos_;
  std::lock_guard<std::mutex> lock(d.mutex);

  if (!entrada) {
    return;
  }
  if (!memoria || !objeto) {
    return;
  }
  // The microcode as it is in the container (big-endian, unpatched): D3D has just copied it, or points at it.
  const ds::Shader& shader = *static_cast<const ds::Shader*>(entrada->juego);
  const Contenedor c{shader.original};
  const uint32_t virtuales = c.U32(4), cabecera = c.U32(24);
  const uint32_t desplazamiento = c.U32(cabecera);  // of the microcode in the physical part
  const uint32_t bytes = uint32_t(entrada->microcodigo.size() * 4);
  const uint32_t comparar = std::min<uint32_t>(bytes, 64);
  auto legible = [&](uint32_t direccion) {
    rex::memory::BaseHeap* heap = memoria->LookupHeap(direccion);
    uint32_t proteccion = 0;
    return heap && heap->QueryProtect(direccion, &proteccion) && (proteccion & rex::memory::kMemoryProtectRead);
  };
  if (objeto > UINT32_MAX - kPalabrasObjeto * 4 || !legible(objeto) ||
      !legible(objeto + kPalabrasObjeto * 4 - 1)) {
    return;
  }
  const uint8_t* palabras = memoria->TranslateVirtual<const uint8_t*>(objeto);
  // The object points either at the microcode or at the start of its copy of the physical part (Dead Space 2: word 6,
  // with the microcode `physicalOffset` bytes in).
  for (uint32_t k = 0; k < kPalabrasObjeto * 2; ++k) {
    const uint32_t i = k / 2;
    const uint32_t puntero = rex::memory::load_and_swap<uint32_t>(palabras + size_t(i) * 4);
    if (!puntero || (puntero & 0x3) || ((k & 1) && !desplazamiento)) continue;
    const uint32_t suma = (k & 1) ? desplazamiento : 0;
    if (puntero > UINT32_MAX - suma || puntero + suma > UINT32_MAX - comparar + 1) continue;
    const uint32_t v = puntero + suma;
    if (!legible(v) || !legible(v + comparar - 1)) continue;
    // With a vertex shader, D3D may already have patched the fetches: compare only what it leaves alone.
    const uint8_t* copia = memoria->TranslateVirtual<const uint8_t*>(v);
    bool igual = true;
    for (uint32_t w = 0; w < comparar / 4 && igual; ++w) {
      uint32_t a = rex::memory::load_and_swap<uint32_t>(copia + w * 4);
      uint32_t b = c.U32(size_t(virtuales) + c.U32(cabecera) + w * 4);
      if (entrada->vertices) {
        for (const ElementoVertice& el : entrada->elementos) {
          if (w / 3 == el.instruccion) {
            a &= kFetchConserva[w % 3];
            b &= kFetchConserva[w % 3];
          }
        }
      }
      igual = a == b;
    }
    if (!igual) continue;
    d.por_direccion[memoria->GetPhysicalAddress(v) & 0x1FFFFFFF] = entrada;
    return;
  }

  if (d.avisos_objetos++ < 4) {
    REXLOG_WARN("[shaders] {} object {:08X}: no pointer to its microcode",
                entrada->vertices ? "VS" : "PS", objeto);
  }
}

const EntradaShader* Biblioteca::Identificar(bool vertices, uint32_t fisica,
                                             std::span<const uint32_t> microcodigo, uint64_t& hash_crudo) {
  hash_crudo = XXH3_64bits(microcodigo.data(), microcodigo.size_bytes());
  Datos& d = *datos_;
  if (!d.cargada) return nullptr;
  std::lock_guard<std::mutex> lock(d.mutex);

  const uint64_t huella = hash_crudo ^ (uint64_t(microcodigo.size()) << 1) ^ uint64_t(vertices);
  // 1. An address registered by a creation hook is accepted only when the full
  // microcode matches after masking known D3D-patched vertex fetch bits.
  if (fisica) {
    if (auto it = d.por_direccion.find(fisica & 0x1FFFFFFF); it != d.por_direccion.end() &&
        it->second->vertices == vertices && it->second->microcodigo.size() == microcodigo.size()) {
      const EntradaShader& e = *it->second;
      d.temporal.assign(microcodigo.begin(), microcodigo.end());
      for (const ElementoVertice& el : e.elementos) {
        for (size_t j = 0; j < 3; ++j) d.temporal[size_t(el.instruccion) * 3 + j] &= kFetchConserva[j];
      }
      size_t distintas = 0;
      for (size_t w = 0; w < microcodigo.size(); ++w) distintas += d.temporal[w] != e.microcodigo[w];
      if (!distintas) {
        if (vertices) d.aprendidas[huella] = it->second;
        return it->second;
      }

      if (d.avisos_direccion++ < kMaxAvisos) {
        REXLOG_WARN("[shaders] {} at {:08X} is no longer n{}: {} of {} words differ", vertices ? "VS" : "PS",
                    fisica, e.numero, distintas, microcodigo.size());
      }
    }
  }
  // 2. Learned patched microcode.
  if (auto it = d.aprendidas.find(huella); it != d.aprendidas.end()) {
    return it->second;
  }
  // 3. The microcode with each candidate's fetches masked (cached, misses too).
  const EntradaShader* elegido = nullptr;
  if (auto it = d.por_microcodigo.find(huella); it != d.por_microcodigo.end()) {
    elegido = it->second;
  } else {
    if (auto c = d.candidatos.find({vertices, uint32_t(microcodigo.size())}); c != d.candidatos.end()) {
      for (uint32_t indice : c->second) {
        const EntradaShader& e = d.entradas[indice];
        const bool exact = SamePatchedShader(microcodigo, e.microcodigo, e.elementos);
        // XDK reordered this one observed shader's independent r0.x fetches.
        // Keep the path bound to the exact original container and observed raw
        // program, then verify the complete fetch bijection and all other words.
        const bool permuted = !exact && vertices && e.huella_contenedor == 0x28F7D45985D435BEULL &&
                              hash_crudo == 0x23F1ACCCABB34986ULL &&
                              SamePermutedFetchShader(microcodigo, e.microcodigo, e.elementos);
        if (exact || permuted) {
          // Two containers can share code yet have different interface metadata.
          // Only independently verified aliases can share a representative.
          if (elegido) {
            if (!VerifiedAlias(*elegido, e)) {
              elegido = nullptr;
              break;
            }
          } else {
            elegido = &e;
          }
        }
      }
    }
    d.por_microcodigo.emplace(huella, elegido);
    if (!elegido && d.avisos_cargas++ < kMaxAvisos) {
      REXLOG_WARN("[shaders] unknown {} load: {} words at {:08X}", vertices ? "VS" : "PS", microcodigo.size(),
                  fisica);
    }
  }
  return elegido;
}


Biblioteca& Global() {
  static Biblioteca biblioteca;
  return biblioteca;
}

}  // namespace minininjas::shaders
