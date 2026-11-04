#pragma once

#include <rex/filesystem.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/memory.h>

#include <filesystem>
#include <memory>
#include <string>
#include <stdexcept>

#include "minininjas_gpu.h"
#include "minininjas_full_frame.h"
#include "minininjas_native.h"
#if MINININJAS_PGO_RECORD
#include "minininjas_pgo.h"
#endif

REXCVAR_DECLARE(bool, input_service_shortcuts);
REXCVAR_DECLARE(bool, minininjas_native_ramp);
REXCVAR_DECLARE(int32_t, nfsmw_io_resumen_s);
REXCVAR_DECLARE(int32_t, nfsmw_io_aviso_ms);

class MiniNinjasApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<MiniNinjasApp>(new MiniNinjasApp(ctx, "minininjas", PPCImageConfig));
  }

 protected:
  bool EnableBuiltinOverlays() const override { return false; }

#if MINININJAS_PGO_RECORD
  void OnWindowFocusChanged(bool focused) override {
    if (!focused) minininjas::pgo::Save();
  }
  bool OnWindowCloseRequested() override {
    minininjas::pgo::Save();
    return true;
  }
  void OnShutdown() override { minininjas::pgo::Save(); }
#endif

  void OnPostInitLogging() override { rex::SetAllLevels(spdlog::level::warn); }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    minininjas::cpu::EnableNativeRamp(REXCVAR_GET(minininjas_native_ramp));
    REXCVAR_SET(input_service_shortcuts, false);
    REXCVAR_SET(nfsmw_io_resumen_s, 0);
    REXCVAR_SET(nfsmw_io_aviso_ms, 0);
    if (!config.graphics) config.graphics = minininjas::gpu::CreateGraphicsSystem();
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty()) return;
    const auto folder = rex::filesystem::GetExecutableFolder();
    // Some standalone NRO loaders do not supply argv[0].
    paths.game_data_root = (folder.empty() ? std::filesystem::path("/switch/mini-ninjas") : folder) / "game";
  }

  void OnPostLoadXexImage() override {
    auto* mode = runtime()->memory()->TranslateVirtual<uint8_t*>(minininjas::full_frame::kModeAddress);
    const auto result = minininjas::full_frame::PatchMode({mode, minininjas::full_frame::kModeBytes});
    if (result == minininjas::full_frame::PatchResult::UnexpectedImage) {
      REXLOG_ERROR("[full-frame] Unsupported render-mode table at 826A3F60; no game data changed");
      throw std::runtime_error("Mini Ninjas full-frame mode signature mismatch");
    }
  }
};
