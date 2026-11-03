#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace minininjas::full_frame {

constexpr uint32_t kModeAddress = 0x826A3F60;
constexpr size_t kModeBytes = 80;
// Original image: width, height, MSAA enum, count, four D3DRECTs.
// sub_8213D370 sizes the surfaces from these rects, while sub_821688B8
// passes the same count/rects to XDK BeginTiling (sub_82440298).
constexpr std::array<uint32_t, 20> kOriginalMode = {
    1280, 720, 2, 4,
    0,   0, 320, 720,
    320, 0, 640, 720,
    640, 0, 960, 720,
    960, 0, 1280, 720};

inline uint32_t ReadWord(std::span<const uint8_t> bytes, size_t word) {
  const auto* p = bytes.data() + word * 4;
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}

inline void WriteWord(std::span<uint8_t> bytes, size_t word, uint32_t value) {
  auto* p = bytes.data() + word * 4;
  for (size_t i = 0; i < 4; ++i) p[i] = uint8_t(value >> (24 - 8 * i));
}

enum class PatchResult { Applied, AlreadyApplied, UnexpectedImage };

inline PatchResult PatchMode(std::span<uint8_t> bytes) {
  if (bytes.size() < kModeBytes) return PatchResult::UnexpectedImage;
  bool original = true, patched = true;
  for (size_t i = 0; i < kOriginalMode.size(); ++i) {
    const uint32_t expected = i == 3 ? 1 : i == 6 ? 1280 : kOriginalMode[i];
    const uint32_t actual = ReadWord(bytes, i);
    original &= actual == kOriginalMode[i];
    patched &= actual == expected;
  }
  if (patched) return PatchResult::AlreadyApplied;
  if (!original) return PatchResult::UnexpectedImage;
  // Keep MSAA, full resolution, and unused records intact. The guest builds
  // full-sized color/depth surfaces and chooses its own one-tile XDK path.
  WriteWord(bytes, 3, 1);
  WriteWord(bytes, 6, 1280);
  return PatchResult::Applied;
}

}  // namespace minininjas::full_frame
