#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>
#include <vector>
#include <xxhash.h>

namespace minininjas::foliage {
enum class Result { NotTarget, Applied, Rejected };

// Scatter grass: one diffuse fetch, vertex colour/alpha, then fog. Keep the
// original sample, alpha comparisons and colour arithmetic. Only move colour
// work after the alpha test and replace its switch with specialization-time
// selections: uam otherwise retains constant branches from the switch.
// This is shader discard, NOT early_fragment_tests (grass writes depth/stencil).
inline Result Patch(uint64_t container, std::span<const uint32_t> source,
                    std::vector<uint32_t>& output) {
  if (container != 0x39F26D870020FECCull) return Result::NotTarget;
  // Exact verified release module. Unknown/changed libraries retain their code.
  if (source.size() != 835 || source[0] != 0x07230203u || source[3] != 123 ||
      XXH3_64bits(source.data(), source.size_bytes()) != 0x590CFAE6D653CCD7ull)
    return Result::Rejected;

  constexpr uint32_t kFunction = 405, kRgb1 = 446, kAlpha = 459,
                     kRgb2 = 480, kTest = 615, kStore = 830;
  std::vector<uint32_t> patched;
  patched.reserve(source.size() + 64);
  auto copy = [&](uint32_t begin, uint32_t end) {
    patched.insert(patched.end(), source.begin() + begin, source.begin() + end);
  };
  auto emit = [&](uint32_t op, std::initializer_list<uint32_t> operands) {
    patched.push_back((uint32_t(operands.size() + 1) << 16) | op);
    patched.insert(patched.end(), operands.begin(), operands.end());
  };
  copy(0, kFunction);
  uint32_t next_id = source[3];
  std::array<uint32_t, 7> matches{};
  for (uint32_t comparison = 0; comparison < matches.size(); ++comparison) {
    uint32_t constant = 29;  // existing uint zero
    if (comparison) {
      constant = next_id++;
      emit(43, {17, constant, comparison << 16});  // OpConstant uint
    }
    matches[comparison] = next_id++;
    // OpSpecConstantOp bool IEqual(masked alpha function, comparison).
    // ID 47 already masks bits 16..18 of specialization constant zero.
    emit(52, {23, matches[comparison], 170, 47, constant});
  }
  copy(kFunction, kRgb1);  // entry, interpolators and the original texture fetch
  copy(kAlpha, kRgb2);    // alpha = texture.a * vertex.a, plus its output value

  emit(171, {23, 87, 46, 29});  // original alpha-test-enable condition
  emit(247, {88, 2});          // OpSelectionMerge, DontFlatten
  emit(250, {87, 89, 88});
  emit(248, {89});
  emit(65, {44, 100, 6, 22, 48, 29});  // shared constants v[17].x, alpha reference
  emit(61, {18, 101, 100});
  // Retain the exact ordered SPIR-V comparisons, including their NaN behaviour.
  constexpr std::array<uint32_t, 6> kOps{184, 180, 188, 186, 182, 190};
  constexpr std::array<uint32_t, 7> kValues{24, 102, 105, 108, 111, 114, 117};
  for (uint32_t i = 0; i < kOps.size(); ++i)
    emit(kOps[i], {23, kValues[i + 1], 64, 101});
  uint32_t pass = 30;  // ALWAYS; kValues[0] is false (NEVER)
  for (int i = 6; i >= 0; --i) {
    const uint32_t result = i ? next_id++ : 118;
    emit(169, {23, result, matches[i], kValues[i], pass});
    pass = result;
  }
  // Preserve the original +/-1 conversion before discard. A logical negation
  // lets uam invert floating comparisons, which changes unordered/NaN inputs.
  emit(169, {18, 119, 118, 34, 49});
  emit(184, {23, 120, 119, 19});
  emit(247, {121, 0});
  emit(250, {120, 122, 121});
  emit(248, {122});
  emit(252, {});  // OpKill; no derivatives or texture fetches follow it
  emit(248, {121});
  emit(249, {88});
  emit(248, {88});

  copy(kRgb1, kAlpha);
  copy(kRgb2, kTest);  // unchanged vertex colour, fog and output composition
  copy(kStore, uint32_t(source.size()));
  patched[3] = next_id;
  output = std::move(patched);
  return Result::Applied;
}
}  // namespace minininjas::foliage
