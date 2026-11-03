// Determine the scalar ABI of a single SPIR-V vertex input location.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>
#include <rex/x360/shader_entry.h>

namespace minininjas::shaders {
enum class InputAbi { kMissingOrInvalid, kFloat4, kUint4, kNotApplicable };

inline InputAbi VertexInputAbi(std::span<const uint32_t> spirv, uint32_t location) {
  if (spirv.size() < 5 || spirv[0] != 0x07230203u) return InputAbi::kMissingOrInvalid;
  struct Scalar { uint32_t opcode, width, signedness; };
  struct Vector { uint32_t scalar, count; };
  struct Pointer { uint32_t storage, pointee; };
  struct Variable { uint32_t type, storage; };
  std::unordered_map<uint32_t, Scalar> scalar;
  std::unordered_map<uint32_t, Vector> vector;
  std::unordered_map<uint32_t, Pointer> pointer;
  std::unordered_map<uint32_t, Variable> variable;
  std::unordered_map<uint32_t, uint32_t> locations;
  for (size_t i = 5; i < spirv.size();) {
    const uint32_t n=spirv[i]>>16, op=spirv[i]&0xffff;
    if (!n || i+n>spirv.size()) return InputAbi::kMissingOrInvalid;
    const uint32_t* w=spirv.data()+i;
    if (op==21 && n==4) scalar[w[1]]={op,w[2],w[3]};             // OpTypeInt
    else if (op==22 && n==3) scalar[w[1]]={op,w[2],0};           // OpTypeFloat
    else if (op==23 && n==4) vector[w[1]]={w[2],w[3]};           // OpTypeVector
    else if (op==32 && n==4) pointer[w[1]]={w[2],w[3]};          // OpTypePointer
    else if (op==59 && n>=4) variable[w[2]]={w[1],w[3]};         // OpVariable
    else if (op==71 && n==4 && w[2]==30) {                       // OpDecorate Location
      if (!locations.emplace(w[1],w[3]).second) return InputAbi::kMissingOrInvalid;
    }
    i+=n;
  }
  InputAbi found=InputAbi::kMissingOrInvalid;
  for (const auto& [id,var] : variable) {
    if (var.storage!=1) continue;                                 // Input
    const auto l=locations.find(id);
    if (l==locations.end() || l->second!=location) continue;
    if (found!=InputAbi::kMissingOrInvalid) return InputAbi::kMissingOrInvalid;
    const auto p=pointer.find(var.type);
    if (p==pointer.end() || p->second.storage!=1) return InputAbi::kMissingOrInvalid;
    const auto v=vector.find(p->second.pointee);
    if (v==vector.end() || v->second.count!=4) return InputAbi::kMissingOrInvalid;
    const auto s=scalar.find(v->second.scalar);
    if (s==scalar.end() || s->second.width!=32) return InputAbi::kMissingOrInvalid;
    if (s->second.opcode==22) found=InputAbi::kFloat4;
    else if (s->second.opcode==21 && !s->second.signedness) found=InputAbi::kUint4;
    else return InputAbi::kMissingOrInvalid;
  }
  return found;
}

inline InputAbi BlendIndicesAbi(std::span<const uint32_t> spirv,
                                std::span<const rex::x360::ElementoVertice> elements) {
  std::vector<std::pair<uint8_t,uint8_t>> semantics;
  InputAbi selected=InputAbi::kNotApplicable;
  for (const auto& el : elements) {
    const auto semantic=std::make_pair(el.uso,el.indice_uso);
    auto it=std::find(semantics.begin(),semantics.end(),semantic);
    if (it==semantics.end()) { semantics.push_back(semantic); it=semantics.end()-1; }
    const uint32_t location=uint32_t(it-semantics.begin());
    if (el.uso!=2 || el.manual) continue;
    const InputAbi abi=VertexInputAbi(spirv,location);
    if (abi==InputAbi::kMissingOrInvalid) return abi;
    if (selected!=InputAbi::kNotApplicable && selected!=abi) return InputAbi::kMissingOrInvalid;
    selected=abi;
  }
  return selected;
}
}  // namespace minininjas::shaders
