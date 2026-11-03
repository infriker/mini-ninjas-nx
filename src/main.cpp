#include "minininjas_init.h"
#include "minininjas_app.h"

extern "C" bool RexSwitchProfilerEnabled(void) { return false; }

REX_DEFINE_APP(minininjas, MiniNinjasApp::Create)
