// The differential test runs real translated instructions against bounded guest
// RAM. Calls out of the selected region deliberately observe the whole context.
#include "minininjas_pch.h"
#include "minininjas_native.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace reg_test {
constexpr uint32_t kSize = 1u << 20;
using Memory = std::array<uint8_t, kSize + 256>;
alignas(64) Memory memory{}, initial{}, expected_memory{};
bool reference_run = true;
const char* current = nullptr;
unsigned trial = 0;
unsigned gas = 0;
uint64_t path_hash = 0;
size_t event = 0;
struct Limit {};
struct Observation {
  uint32_t target;
  std::array<uint8_t, sizeof(PPCContext)> context;
};
std::vector<Observation> observed;

void Mark(uint32_t label) {
  if (!gas--) throw Limit{};
  path_hash = (path_hash ^ label) * UINT64_C(1099511628211);
}

uint8_t* Address(uint8_t* base, uint32_t addr) {
  if (!gas--) throw Limit{};
  return base + (addr & (kSize - 1));
}

template<typename T> T Read(uint8_t* base, uint32_t addr) {
  T value;
  std::memcpy(&value, Address(base, addr), sizeof(T));
  if constexpr (sizeof(T) == 8) return __builtin_bswap64(value);
  if constexpr (sizeof(T) == 4) return __builtin_bswap32(value);
  if constexpr (sizeof(T) == 2) return __builtin_bswap16(value);
  return value;
}

template<typename T> void Put(uint8_t* base, uint32_t addr, T value) {
  if constexpr (sizeof(T) == 8) value = __builtin_bswap64(value);
  if constexpr (sizeof(T) == 4) value = __builtin_bswap32(value);
  if constexpr (sizeof(T) == 2) value = __builtin_bswap16(value);
  std::memcpy(base + (addr & (kSize - 1)), &value, sizeof(T));
}

void Different(const char* where, const void* a, const void* b, size_t size) {
  if (!std::memcmp(a,b,size)) return;
  size_t i = 0;
  while (static_cast<const uint8_t*>(a)[i] == static_cast<const uint8_t*>(b)[i]) ++i;
  std::fprintf(stderr,"%s input %u event %zu: %s differs at byte %zu\n",current,trial,event,where,i);
  std::abort();
}

void Boundary(uint32_t target, PPCContext& ctx, uint8_t* base) {
  if (event >= 300) throw Limit{};
  if (reference_run) {
    observed.emplace_back();
    observed.back().target = target;
    std::memcpy(observed.back().context.data(),&ctx,sizeof(ctx));
  } else {
    if (event >= observed.size() || observed[event].target != target) std::abort();
    Different("state visible to callee",&ctx,observed[event].context.data(),sizeof(ctx));
  }
  ++event;
  // Include nonvolatile, partial-register and vector clobbers that an unknown
  // target may legally expose to this translation at a shared entry point.
  ctx.r3.u64 = ((trial + event) & 1) ? 0x19000 : 0;
  ctx.r8.u64 ^= UINT64_C(0x6758491a00000000);
  ctx.r18.u64 ^= UINT64_C(0x2357abcf00000000);
  ctx.v62.u32[1] ^= target;
  ctx.f30.f64 = double(int(trial % 9) - 4);
  ctx.xer.so ^= 1;
  ctx.cr4.set_raw(unsigned(event) & 15);
  base[0xfc000 + (event % 64)] ^= uint8_t(target);
}

void InitRegisters(PPCContext& ctx, unsigned seed); // emitted by test_local_regs.py
bool Compare(PPCFunc* original, PPCFunc* fast, uint32_t address, unsigned seed) {
  trial = seed;
  PPCContext before{}, after{}, actual{};
  InitRegisters(before,seed);
  constexpr uint32_t obj=0x10000, data=0x20000, desc=0x30000;
  before.r1.u64=0xd0000; before.r3.u64=obj; before.r4.u64=desc;
  before.r5.u64=(seed%17)*4; before.r6.u64=0x42000; before.r7.u64=seed%4;
  before.r12.u64=0; before.r13.u64=0x86000;
  before.ctr.u64=3; before.lr=0x82222220;
  before.xer.ca=seed&1; before.xer.so=(seed>>1)&1;
  before.fpscr.InitHost(); before.fpscr.storeFromGuest(seed%4);
  initial.fill(0);
  Put<uint32_t>(initial.data(),obj+4,data);
  Put<uint32_t>(initial.data(),obj+12,seed);
  Put<uint32_t>(initial.data(),obj+56,seed%10);
  Put<uint32_t>(initial.data(),data+8,(seed&1)?0x80024000u:0);
  initial[data+20]=(seed&2)?1:0;
  for (unsigned i=0;i<64;++i) initial[desc+i]=uint8_t(i+seed);
  if(address==0x824185e4) before.r12.s64=-int64_t((seed%4)*4096+1);
  if(address==0x82147db8 || address==0x821476b0) {
    before.r4.u64=seed%4; before.r5.u64=0x44000; before.r6.u64=seed%8;
  }
  if(address==0x82417ac0 || address==0x8251c668) before.r3.u64+=seed%16;
  std::memcpy(&after,&before,sizeof(before)); std::memcpy(&actual,&before,sizeof(before));
  minininjas::cpu::local_registers_enabled=false;
  gas=40000; path_hash=0; event=0; observed.clear(); reference_run=true; memory=initial;
  before.fpscr.setcsr(before.fpscr.csr);
  try { original(after,memory.data()); } catch (Limit&) { return false; }
  const auto expected_path=path_hash;
  expected_memory=memory;
  minininjas::cpu::local_registers_enabled=true;
  gas=40000; path_hash=0; event=0; reference_run=false; memory=initial;
  before.fpscr.setcsr(before.fpscr.csr);
  try { fast(actual,memory.data()); } catch (Limit&) { std::abort(); }
  if (path_hash!=expected_path || event!=observed.size()) std::abort();
  Different("returned context",&after,&actual,sizeof(after));
  Different("guest RAM",expected_memory.data(),memory.data(),memory.size());
  return true;
}
}  // namespace reg_test

#undef REX_RAW_ADDR
#undef REX_LOAD_U8
#undef REX_LOAD_U16
#undef REX_LOAD_U32
#undef REX_LOAD_U64
#undef REX_STORE_U8
#undef REX_STORE_U16
#undef REX_STORE_U32
#undef REX_STORE_U64
#undef REX_CALL_INDIRECT_FUNC
#define REX_RAW_ADDR(a) reg_test::Address(base,(a))
#define REX_LOAD_U8(a) reg_test::Read<uint8_t>(base,(a))
#define REX_LOAD_U16(a) reg_test::Read<uint16_t>(base,(a))
#define REX_LOAD_U32(a) reg_test::Read<uint32_t>(base,(a))
#define REX_LOAD_U64(a) reg_test::Read<uint64_t>(base,(a))
#define REX_STORE_U8(a,v) reg_test::Put<uint8_t>(base,(a),(v))
#define REX_STORE_U16(a,v) reg_test::Put<uint16_t>(base,(a),(v))
#define REX_STORE_U32(a,v) reg_test::Put<uint32_t>(base,(a),(v))
#define REX_STORE_U64(a,v) reg_test::Put<uint64_t>(base,(a),(v))
#define REX_CALL_INDIRECT_FUNC(x) reg_test::Boundary((x),ctx,base)
#define ppc_trap(context,mem,x) reg_test::Boundary(0xf0000000u+(x),(context),(mem))
