// A standalone NRO has no hbloader heap override. Give it the available
// application memory, like sa2-nx's standalone-emulator startup, while
// preserving hbloader's heap on an actual console.
#include <switch.h>
#include <cstddef>

extern "C" {
extern size_t __nx_heap_size;
extern char* fake_heap_start;
extern char* fake_heap_end;

// Used only when the NRO loader does not provide argv[0].
const char* RexSwitchFallbackExecutablePath(void) {
  return "/switch/mini-ninjas/mini-ninjas.nro";
}

void __libnx_initheap(void) {
  void* address = nullptr;
  size_t size = 0;
  if (envHasHeapOverride()) {
    address = envGetHeapOverrideAddr();
    size = envGetHeapOverrideSize();
  } else {
    u64 total = 0, used = 0;
    const Result total_rc = svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    const Result used_rc = svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    if (R_SUCCEEDED(total_rc) && R_SUCCEEDED(used_rc) && total > used + 0x200000) {
      size = static_cast<size_t>((total - used - 0x200000) & ~u64(0x1FFFFF));
    }
    if (size < __nx_heap_size) size = __nx_heap_size;
    Result rc = svcSetHeapSize(&address, size);
    if (R_FAILED(rc) && size != __nx_heap_size) {
      size = __nx_heap_size;
      rc = svcSetHeapSize(&address, size);
    }
    if (R_FAILED(rc)) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
  }
  fake_heap_start = static_cast<char*>(address);
  fake_heap_end = fake_heap_start + size;
}
}
