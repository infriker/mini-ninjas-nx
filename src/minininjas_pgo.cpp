#include "minininjas_pgo.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory_resource>
#include <mutex>
#include <string>

#include <rex/filesystem.h>
#include <rex/logging.h>

extern "C" {
#include <gcov.h>
extern const gcov_info* const __mini_gcov_begin[];
extern const gcov_info* const __mini_gcov_end[];
}

namespace minininjas::pgo {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct Stream {
  std::FILE* file;
  std::pmr::monotonic_buffer_resource scratch;
};

void Write(const void* bytes, unsigned size, void* context) {
  auto* stream = static_cast<Stream*>(context);
  std::fwrite(bytes, 1, size, stream->file);
}

void Filename(const char* filename, void* context) {
  // GCC's prefix-path option gives us ./CMakeFiles#...gcda. Store only the
  // flat filename so gcov-tool always extracts into the chosen directory.
  const auto name = fs::path(filename).filename().string();
  __gcov_filename_to_gcfn(name.c_str(), Write, context);
}

void* Allocate(unsigned size, void* context) {
  return static_cast<Stream*>(context)->scratch.allocate(size);
}

fs::path NewSession(std::error_code& error) {
  auto folder = rex::filesystem::GetExecutableFolder();
  if (folder.empty()) folder = "/switch/mini-ninjas";
  const auto root = folder / "pgo";
  fs::create_directories(root, error);
  for (unsigned run = 1; !error; ++run) {
    const auto session = root / ("run-" + std::to_string(run));
    // Some libnx filesystems report a successful mkdir for an existing folder.
    if (fs::exists(session, error)) continue;
    if (error) break;
    if (fs::create_directory(session, error)) return session;
  }
  return {};
}
}  // namespace

void Save() {
  static std::mutex mutex;
  static fs::path session;
  static unsigned completed = 0;
  const std::lock_guard lock(mutex);
  std::error_code error;
  if (session.empty()) session = NewSession(error);
  if (error) {
    REXLOG_ERROR("PGO: cannot create recording directory: {}", error.message());
    return;
  }
  const auto temporary = session / "writing.tmp";
  auto* file = std::fopen(temporary.c_str(), "wb");
  if (!file) {
    REXLOG_ERROR("PGO: cannot open {}", temporary.string());
    return;
  }
  std::setvbuf(file, nullptr, _IOFBF, 64 * 1024);
  Stream stream{file, {}};
  for (auto item = __mini_gcov_begin; item != __mini_gcov_end; ++item) {
    __gcov_info_to_gcda(*item, Filename, Write, Allocate, &stream);
    stream.scratch.release();
  }
  const bool failed = std::ferror(file) != 0;
  if (std::fclose(file) != 0 || failed) {
    REXLOG_ERROR("PGO: incomplete write to {}", temporary.string());
    return;
  }
  const auto snapshot = [](unsigned n) { return session / ("profile-" + std::to_string(n) + ".pgo"); };
  fs::rename(temporary, snapshot(completed + 1), error);
  if (error) {
    REXLOG_ERROR("PGO: cannot finish recording: {}", error.message());
    return;
  }
  // Snapshots are cumulative. Publish the new one before retiring the old one.
  if (completed) fs::remove(snapshot(completed), error);
  ++completed;
}

void OnFrame() {
  using namespace std::chrono_literals;
  // Called only by the GPU ring thread. The first checkpoint also covers startup.
  static auto next = Clock::now() + 30s;
  if (Clock::now() >= next) {
    Save();
    next = Clock::now() + 2min;
  }
}
}  // namespace minininjas::pgo
