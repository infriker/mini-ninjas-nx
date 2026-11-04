#pragma once

namespace minininjas::cpu {
// Set during startup, before guest threads can enter the hook.
void EnableNativeRamp(bool enabled);
}
