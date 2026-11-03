#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>
#include <xxhash.h>

namespace minininjas::water {
// Correct only the final X coordinates of the original refraction samples.
// The quarter-width Xenos sampling assumed a four-strip scene; the full-frame
// mode resolves the input as one 1280-wide image. Do not change the cube,
// normal-map coordinates, Y or the bump calculation before its clamp.
enum class Result { NotTarget, Applied, Rejected };

inline Result Patch(uint64_t container, std::span<const uint32_t> source,
                    std::vector<uint32_t>& output) {
  struct Sample { uint32_t offset, result, image, coord; };
  struct Spec {
    uint64_t container, spirv_hash;
    uint32_t words, bound, scalar_type, vector_type;
    std::array<Sample, 2> samples;
    uint32_t count;
  };
  // Hashes identify the unmodified SPIR-V from the verified release library.
  static constexpr Spec kSpecs[] = {
    {0x6A90B5F4EDDA9DA3ull, 0x48CE6C991D0DE734ull, 2866, 426, 37, 47,
      {{{2218, 322, 321, 320}, {2244, 327, 326, 325}}}, 2},
    {0x1F62A0E8A64AA329ull, 0xF9B47FCDFD94247Aull, 1295, 186, 27, 38,
      {{{755, 99, 98, 97}, {0, 0, 0, 0}}}, 1},
  };
  const Spec* spec = nullptr;
  for (const auto& candidate : kSpecs) {
    if (candidate.container == container) { spec = &candidate; break; }
  }
  if (!spec) return Result::NotTarget;
  if (source.size() != spec->words || source.size() < 5 || source[0] != 0x07230203u ||
      source[3] != spec->bound || XXH3_64bits(source.data(), source.size_bytes()) != spec->spirv_hash)
    return Result::Rejected;

  size_t function_at = 0;
  std::array<bool, 2> found{};
  for (size_t i = 5; i < source.size();) {
    const uint32_t count = source[i] >> 16, op = source[i] & 0xFFFF;
    if (!count || count > source.size() - i) return Result::Rejected;
    if (op == 54 && !function_at) function_at = i;  // OpFunction
    for (uint32_t j = 0; j < spec->count; ++j) {
      const auto& s = spec->samples[j];
      if (i != s.offset) continue;
      // OpImageSampleImplicitLod with the exact expected source coordinate.
      if (op != 87 || count != 6 || source[i + 2] != s.result ||
          source[i + 3] != s.image || source[i + 4] != s.coord || source[i + 5] != 0)
        return Result::Rejected;
      found[j] = true;
    }
    i += count;
  }
  if (!function_at) return Result::Rejected;
  for (uint32_t j = 0; j < spec->count; ++j)
    if (!found[j] || spec->samples[j].offset <= function_at) return Result::Rejected;

  std::vector<uint32_t> patched;
  patched.reserve(source.size() + 4 + 16 * spec->count);
  patched.insert(patched.end(), source.begin(), source.begin() + 5);
  const uint32_t constant = spec->bound;
  patched[3] = spec->bound + 1 + 3 * spec->count;
  for (size_t i = 5; i < source.size();) {
    const uint32_t count = source[i] >> 16;
    if (i == function_at) {
      patched.insert(patched.end(), {uint32_t((4u << 16) | 43u), spec->scalar_type,
                                     constant, 0x40800000u});  // OpConstant float 4
    }
    bool replaced = false;
    for (uint32_t j = 0; j < spec->count; ++j) {
      const auto& s = spec->samples[j];
      if (i != s.offset) continue;
      const uint32_t x = constant + 1 + j * 3, scaled = x + 1, coord = x + 2;
      patched.insert(patched.end(), {
          uint32_t((5u << 16) | 81u), spec->scalar_type, x, s.coord, 0,  // OpCompositeExtract
          uint32_t((5u << 16) | 133u), spec->scalar_type, scaled, x, constant,  // OpFMul
          uint32_t((6u << 16) | 82u), spec->vector_type, coord, scaled, s.coord, 0  // OpCompositeInsert
      });
      patched.insert(patched.end(), source.begin() + i, source.begin() + i + count);
      patched[patched.size() - count + 4] = coord;
      replaced = true;
    }
    if (!replaced) patched.insert(patched.end(), source.begin() + i, source.begin() + i + count);
    i += count;
  }
  output = std::move(patched);
  return Result::Applied;
}
}  // namespace minininjas::water
