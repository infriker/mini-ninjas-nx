// Requires a separately obtained release shader package; no game shaders are
// embedded here. Writes the two modules for SPIR-V and compiler validation.
#define XXH_FORCE_MEMORY_ACCESS 0
#define XXH_INLINE_ALL
#include "minininjas_foliage_spirv.h"
#include "ds_shader_library.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
  assert(argc == 3);
  ds::BibliotecaShaders library;
  library.Cargar(std::filesystem::path(argv[1]));
  const std::filesystem::path out(argv[2]);
  std::filesystem::create_directories(out);
  using namespace minininjas::foliage;
  unsigned changed = 0;
  for (const auto& shader : library.shaders()) {
    std::vector<uint32_t> patched{0xBAD};
    const auto result = Patch(shader.huella, shader.spirv, patched);
    if (shader.huella != 0x39F26D870020FECCull) {
      assert(result == Result::NotTarget && patched == std::vector<uint32_t>{0xBAD});
      continue;
    }
    assert(!shader.vertices && result == Result::Applied);
    ++changed;
    assert(XXH3_64bits(shader.spirv.data(), shader.spirv.size() * 4) !=
           XXH3_64bits(patched.data(), patched.size() * 4));  // distinct DKSH key
    for (size_t i = 0; i < shader.spirv.size(); ++i) {
      auto corrupt = shader.spirv;
      corrupt[i] ^= 1;
      std::vector<uint32_t> sentinel{1, 2, 3};
      assert(Patch(shader.huella, corrupt, sentinel) == Result::Rejected);
      assert((sentinel == std::vector<uint32_t>{1, 2, 3}));
      assert(Patch(shader.huella, std::span(shader.spirv).first(i), sentinel) == Result::Rejected);
      assert((sentinel == std::vector<uint32_t>{1, 2, 3}));
    }
    std::vector<uint32_t> sentinel{1, 2, 3};
    assert(Patch(shader.huella, patched, sentinel) == Result::Rejected);  // not applied twice
    auto write = [&](const char* name, const std::vector<uint32_t>& words) {
      std::ofstream file(out / name, std::ios::binary);
      file.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
      assert(file);
    };
    write("grass-original.spv", shader.spirv);
    write("grass-patched.spv", patched);
  }
  assert(changed == 1);
  std::cout << "Foliage patch guards passed across " << library.shaders().size()
            << " release shaders; only the verified grass shader changes\n";
}
