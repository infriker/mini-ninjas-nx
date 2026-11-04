#include "minininjas_pch.h"
#include "minininjas_native.h"

bool minininjas::cpu::local_registers_enabled = true;

REX_EXTERN(__imp__sub_82501EF0);
REX_EXTERN(__imp__sub_82417FF0);

namespace {
bool native_ramp_enabled = false;
bool VirtualRam(uint32_t start, uint32_t bytes) {
  return start >= 0x10000 && uint64_t(start) + bytes <= 0x7F000000;
}

using Vec = simde__m128;

Vec LoadVector(uint8_t* base, uint32_t address) {
  const auto* p = reinterpret_cast<const simde__m128i*>(REX_RAW_ADDR(address & ~15u));
#ifdef __aarch64__
  return vreinterpretq_f32_u8(vrev32q_u8(vld1q_u8(reinterpret_cast<const uint8_t*>(p))));
#else
  const auto order = simde_mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12);
  return simde_mm_castsi128_ps(simde_mm_shuffle_epi8(simde_mm_load_si128(p), order));
#endif
}

void StoreVector(uint8_t* base, uint32_t address, Vec value) {
  auto* p = reinterpret_cast<simde__m128i*>(REX_RAW_ADDR(address & ~15u));
#ifdef __aarch64__
  vst1q_u8(reinterpret_cast<uint8_t*>(p), vrev32q_u8(vreinterpretq_u8_f32(value)));
#else
  const auto order = simde_mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12);
  simde_mm_store_si128(p, simde_mm_shuffle_epi8(simde_mm_castps_si128(value), order));
#endif
}

void SaveRegister(float* reg, Vec value) {
  simde_mm_store_ps(reg, simde_mm_shuffle_ps(value, value, SIMDE_MM_SHUFFLE(0,1,2,3)));
}

Vec ScaleAdd(Vec input, Vec scale, Vec previous) {
  return simde_mm_add_ps(simde_mm_mul_ps(input, scale), previous);
}

// Two NaN operands can select different payloads when a compiler commutes an
// add or multiply. Prove the whole operation stays finite before the first
// store. Even the longest allowed ramp times a normalized input fits in a float.
bool BoundedInputs(uint8_t* base, uint32_t dst, uint32_t src, uint32_t bytes,
                   Vec first, Vec step) {
  const auto bits = simde_mm_or_si128(simde_mm_castps_si128(first), simde_mm_castps_si128(step));
  if (!simde_mm_testz_si128(bits, simde_mm_set1_epi32(0x40000000))) return false;
  // A clear exponent bit 6 in the first big-endian byte of each float proves |x| < 2.
  const auto* in = reinterpret_cast<const simde__m128i*>(REX_RAW_ADDR(src));
  const auto* out = reinterpret_cast<const simde__m128i*>(REX_RAW_ADDR(dst));
  auto any = simde_mm_setzero_si128();
  for (uint32_t i = 0; i < bytes / 16; ++i) {
    any = simde_mm_or_si128(any, simde_mm_or_si128(simde_mm_load_si128(in + i), simde_mm_load_si128(out + i)));
  }
  return simde_mm_testz_si128(any, simde_mm_set1_epi32(0x40));
}
}  // namespace

void minininjas::cpu::EnableNativeRamp(bool enabled) {
  native_ramp_enabled = enabled;
}

// VMX scale-and-add with a four-lane ramp. Keep vectors in host registers
// between iterations. A PPC context, including volatile registers, is observable
// after a return. The original also rounds each multiply before its add.
REX_EXTERN(sub_82501EF0) {
  if (!native_ramp_enabled) return __imp__sub_82501EF0(ctx, base);
  const uint32_t groups = ctx.r7.u32 >> 2;
  const uint32_t blocks = (groups + 3) / 4;
  const uint64_t dst = ctx.r3.u64, src = ctx.r4.u64;
  const uint32_t to_base = ctx.r3.u32 & ~15u, from_base = ctx.r4.u32 & ~15u;
  const uint32_t bytes = blocks * 64;
  if (!blocks || blocks > 0x100000 || !VirtualRam(to_base, bytes) || !VirtualRam(from_base, bytes) ||
      !VirtualRam(ctx.r5.u32 & ~15u, 16) || !VirtualRam(ctx.r6.u32 & ~15u, 16) ||
      (to_base > from_base && to_base - from_base < bytes)) {
    return __imp__sub_82501EF0(ctx, base);
  }

  const Vec step = LoadVector(base, ctx.r6.u32);
  Vec a = LoadVector(base, ctx.r5.u32);
  if (!BoundedInputs(base, to_base, from_base, bytes, a, step)) return __imp__sub_82501EF0(ctx, base);
  ctx.fpscr.enableFlushMode();
  const Vec twice = simde_mm_add_ps(step, step);
  Vec b = simde_mm_add_ps(a, step);
  const Vec triple = simde_mm_add_ps(twice, step);
  const Vec stride = simde_mm_add_ps(twice, twice);
  Vec c = simde_mm_add_ps(a, twice), d = simde_mm_add_ps(a, triple);
  Vec old1, input2, input3, out0, out1, out2, out3;
  for (uint32_t offset = 0; offset < blocks * 64; offset += 64) {
    const uint32_t to = uint32_t(dst) + offset, from = uint32_t(src) + offset;
    const Vec old0 = LoadVector(base, to);
    out0 = ScaleAdd(LoadVector(base, from), a, old0);
    old1 = LoadVector(base, to + 16);
    const Vec old2 = LoadVector(base, to + 32);
    a = simde_mm_add_ps(a, stride);
    const Vec old3 = LoadVector(base, to + 48);
    const Vec input1 = LoadVector(base, from + 16);
    input2 = LoadVector(base, from + 32);
    out1 = ScaleAdd(input1, b, old1);
    input3 = LoadVector(base, from + 48);
    out2 = ScaleAdd(input2, c, old2);
    out3 = ScaleAdd(input3, d, old3);
    b = simde_mm_add_ps(b, stride);
    c = simde_mm_add_ps(c, stride);
    d = simde_mm_add_ps(d, stride);
    // All eight loads precede these stores, including when the arrays overlap.
    StoreVector(base, to, out0);
    StoreVector(base, to + 16, out1);
    StoreVector(base, to + 32, out2);
    StoreVector(base, to + 48, out3);
  }
  SaveRegister(ctx.v0.f32, a);
  SaveRegister(ctx.v4.f32, old1);
  SaveRegister(ctx.v5.f32, out1);
  SaveRegister(ctx.v6.f32, out0);
  SaveRegister(ctx.v7.f32, input2);
  SaveRegister(ctx.v8.f32, out2);
  SaveRegister(ctx.v9.f32, input3);
  SaveRegister(ctx.v10.f32, out3);
  SaveRegister(ctx.v11.f32, d);
  SaveRegister(ctx.v12.f32, c);
  SaveRegister(ctx.v13.f32, b);
  SaveRegister(ctx.v61.f32, triple);
  SaveRegister(ctx.v62.f32, twice);
  SaveRegister(ctx.v63.f32, stride);
  ctx.cr6.compare<uint32_t>(groups, 0, ctx.xer);
  ctx.r4.s64 = -16;
  ctx.r5.s64 = 16;
  ctx.r6.s64 = ctx.r7.s64 = -32;
  ctx.r8.s64 = -48;
  ctx.r9.u64 = dst - src;
  ctx.r10.u64 = dst + 48 + blocks * 64;
  ctx.r11.u64 = src + 32 + blocks * 64;
  ctx.ctr.u64 = 0;
}

// The aligned large-fill path in the audio worker. Preserve the original
// access widths, byte order and sequence, including its unusual final CTR.
REX_EXTERN(sub_82417FF0) {
  const uint32_t addr = ctx.r3.u32, size = ctx.r5.u32;
  if ((addr & 3) || size < 64 || !VirtualRam(addr, size)) {
    return __imp__sub_82417FF0(ctx, base);
  }
  const uint8_t byte = ctx.r4.u8;
  const uint32_t word = byte * UINT32_C(0x01010101);
  auto* data = reinterpret_cast<volatile uint32_t*>(REX_RAW_ADDR(addr));
  for (uint32_t n = 0; n < size / 16; ++n, data += 4) {
    data[0] = word;
    data[1] = word;
    data[2] = word;
    data[3] = word;
  }
  for (uint32_t n = 0; n < ((size >> 2) & 3); ++n) data[n] = word;
  const uint32_t whole = size & ~3u, tail = size & 3;
  for (uint32_t n = 0; n < tail; ++n) REX_STORE_U8(addr + whole + n, byte);
  ctx.r4.u64 = (ctx.r4.u64 & UINT64_C(0xffffffff00000000)) | word;
  ctx.r6.u64 = ctx.r3.u64 + whole;
  ctx.r0.u64 = tail;
  ctx.cr0.compare<int32_t>(tail, 0, ctx.xer);
  ctx.ctr.u64 = tail == 3 ? 1 : 0;
}
