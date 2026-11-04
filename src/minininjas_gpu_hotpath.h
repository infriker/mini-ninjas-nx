#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <rex/graphics/registers.h>
#include <rex/memory/utils.h>

namespace minininjas::gpu::hotpath {

// Reads either the power-of-two wrapping ring or a linear indirect buffer.
struct Reader {
  const uint8_t* base = nullptr;
  uint32_t mask = 0, pos = 0, end = 0;
  uint32_t Pending() const { return mask ? ((end - pos) & mask) : (end - pos); }
  uint32_t Peek() const { return rex::memory::load_and_swap<uint32_t>(base + size_t(pos) * 4); }
  uint32_t Read() { const uint32_t value = Peek(); Skip(1); return value; }
  void Skip(uint32_t words) { pos = mask ? ((pos + words) & mask) : (pos + words); }
};

// CP_RB_CNTL's block setting uses the same byte-to-dword conversion as ReXGlue.
constexpr uint32_t ReadPointerStep(uint32_t block_log2) {
  if (block_log2 < 2 || block_log2 >= 32) return 256;
  const uint32_t words = 1u << (block_log2 - 2);
  return words < 32 ? 32 : words;
}

template <typename Execute, typename Publish>
inline void DrainPrimary(Reader& reader, uint32_t step, Execute&& execute, Publish&& publish) {
  uint32_t published = reader.pos;
  while (reader.Pending() && execute(reader)) {
    if (step && reader.pos != reader.end && ((reader.pos - published) & reader.mask) >= step) {
      if (!publish(reader.pos)) return;  // the ring was reinitialized
      published = reader.pos;
    }
  }
  publish(reader.pos);  // includes a short final batch or the start of a partial packet
}

struct Generations {
  uint64_t& vs;
  uint64_t& ps;
  uint64_t& fetch;
  uint64_t& framing;
};

inline bool IsFraming(uint32_t index) {
  if (index - 0x2080 > 0x2302 - 0x2080) return false;
  return index <= 0x2082 || (index >= 0x210F && index <= 0x2114) || (index >= 0x2204 && index <= 0x2206) ||
         index == 0x2302;
}

// Every scratch write still reaches the callback, even for the same value.
template <typename Scratch>
inline void WriteRegister(uint32_t* registers, uint32_t count, uint32_t index, uint32_t value,
                          Generations generations, Scratch&& scratch) {
  namespace g = rex::graphics;
  if (index >= count) return;
  if (index == g::XE_GPU_REG_COHER_STATUS_HOST) value |= UINT32_C(0x80000000);
  const bool is_scratch = index >= g::XE_GPU_REG_SCRATCH_REG0 && index <= g::XE_GPU_REG_SCRATCH_REG7;
  const bool changed = registers[index] != value;
  if (!changed && !is_scratch) return;
  if (changed) {
    if (index >= 0x4000 && index < 0x4800) ++(index < 0x4400 ? generations.vs : generations.ps);
    else if (index >= 0x4800 && index <= 0x48BF) ++generations.fetch;
    else if (IsFraming(index)) ++generations.framing;
  }
  registers[index] = value;
  if (is_scratch) scratch(index, value);
}

// Return false without reading or writing anything if the entire consecutive
// range isn't within one side-effect-free register class. A generation is an
// identity token observed only between packets, not a count of modifications.
template <typename Read>
inline bool WriteDense(uint32_t* registers, uint32_t index, uint32_t count, Generations generations, Read&& read) {
  if (!count) return false;
  uint32_t end = 0;
  uint64_t* generation = nullptr;
  if (index >= 0x4000 && index < 0x4400) { end = 0x4400; generation = &generations.vs; }
  else if (index >= 0x4400 && index < 0x4800) { end = 0x4800; generation = &generations.ps; }
  else if (index >= 0x4800 && index < 0x48C0) { end = 0x48C0; generation = &generations.fetch; }
  else return false;
  if (count > end - index) return false;
  bool changed = false;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t value = read(i);
    changed |= registers[index + i] != value;
    registers[index + i] = value;
  }
  if (changed) ++*generation;
  return true;
}

inline void PublishVertexCode(std::vector<uint32_t>& current, std::vector<uint32_t>& scratch) {
  current.swap(scratch);
}
}  // namespace minininjas::gpu::hotpath
