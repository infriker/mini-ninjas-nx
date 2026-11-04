#include "minininjas_init.h"
#include "minininjas_app.h"

REXCVAR_DEFINE_BOOL(minininjas_native_ramp, false, "MiniNinjas", "Enable experimental native VMX scale-and-add");

#if MINININJAS_PGO_RECORD
extern "C" bool RexSwitchProfilerEnabled(void) { return true; }
extern "C" bool RexSwitchStackSamplingEnabled(void) { return true; }
#endif

REX_DEFINE_APP(minininjas, MiniNinjasApp::Create)
