# Rebuilding and relinking dependencies

The NRO links the ReXGlue Switch SDK, its selected FFmpeg libraries and
libmspack. The precise SDK base and local modifications are provided in
`patches/`; `scripts/prepare-sdk.sh` obtains the base and initializes the SDK's
Git submodules at the commits recorded by that base. Switch-specific FFmpeg
configuration and source changes are in the SDK's `switch/overlay/`, copied
by `sdk/tools/switch_setup.py` when `scripts/build-switch.sh` runs.

For a modified dependency, first prepare the clean SDK as documented in
[building.md](building.md), then make the modification inside its checked-out
source tree. Rebuild the host tool first if its input has changed, generate the
game translation from your supported executable, and run the Switch build.
The build compiles the SDK and dependencies with the application; this avoids
requiring prebuilt object files from another machine. Preserve your local
source changes and corresponding dependency licences when redistributing a
modified binary.

FFmpeg's `COPYING.LGPLv2.1` and libmspack's `COPYING.LIB` are reproduced in
`LICENSES/`. SDK and other dependency notices are listed in `NOTICE` and
`LICENSES/`; their full original source trees and their own notices are fetched
at the pinned revisions. Nintendo Switch toolchain components are supplied by
devkitPro, separately from this repository. The shader package and original
game assets are separate inputs and are not required to compile or relink the
NRO, but the application needs those inputs at runtime.

For version 1.0.0, the pinned SDK patch series, host generator, game code
generation and Switch NRO build were verified from the supplied source and a
supported executable. This establishes build and relink availability; it does
not by itself establish correct behavior on physical hardware.
