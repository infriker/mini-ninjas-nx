#include "minininjas_init.h"
#include "minininjas_app.h"

REXCVAR_DEFINE_BOOL(minininjas_native_ramp, false, "MiniNinjas", "Enable experimental native VMX scale-and-add");
REXCVAR_DEFINE_BOOL(minininjas_worker_wait, true, "MiniNinjas", "Wake audio workers when their guest barrier completes (restart required)");
REXCVAR_DEFINE_BOOL(minininjas_local_regs, true, "MiniNinjas", "Keep guest registers in host locals in hot game functions (restart required)");

#if MINININJAS_PGO_RECORD
extern "C" bool RexSwitchProfilerEnabled(void) { return true; }
extern "C" bool RexSwitchStackSamplingEnabled(void) { return true; }
#endif

REX_DEFINE_APP(minininjas, MiniNinjasApp::Create)
