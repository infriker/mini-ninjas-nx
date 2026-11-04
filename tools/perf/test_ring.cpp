#include "minininjas_gpu_hotpath.h"

#include <array>
#include <cassert>
#include <vector>

using namespace minininjas::gpu::hotpath;

int main() {
  static_assert(ReadPointerStep(0) == 256 && ReadPointerStep(6) == 32);
  static_assert(ReadPointerStep(10) == 256 && ReadPointerStep(32) == 256);
  // Ring wraps from 127 to 0. Payload lengths include a nested IB packet.
  // The fake handler marks all nested work finished before returning.
  const std::array<uint32_t, 4> sizes{17, 18, 24, 5};
  auto run = [&](uint32_t end, uint32_t step, bool replace_at_checkpoint) {
    Reader ring{nullptr, 127, 115, end};
    size_t packets = 0;
    std::vector<uint32_t> writes;
    auto execute = [&](Reader& r) {
      assert(packets < sizes.size());
      if (r.Pending() < sizes[packets]) return false;
      r.Skip(sizes[packets++]);
      return true;
    };
    DrainPrimary(ring, step, execute, [&](uint32_t read) {
      assert(packets && read == ring.pos);
      writes.push_back(read);
      return !replace_at_checkpoint;
    });
    return writes;
  };
  assert((run(51, 32, false) == std::vector<uint32_t>{22, 51}));
  assert((run(50, 32, false) == std::vector<uint32_t>{22, 46})); // partial last packet
  assert((run(51, 0, false) == std::vector<uint32_t>{51}));
  assert((run(51, 32, true) == std::vector<uint32_t>{22}));
  Reader partial{nullptr, 127, 125, 2};
  unsigned publications = 0;
  DrainPrimary(partial, 32, [](Reader&) { return false; }, [&](uint32_t pos) {
    ++publications;
    assert(pos == 125);
    return true;
  });
  assert(publications == 1);
}
