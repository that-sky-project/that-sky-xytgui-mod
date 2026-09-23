// ============================================================================
// SkyGUI Platform - umbrella header 
// ============================================================================
#ifndef SKYGUI_H
#define SKYGUI_H

#include "skygui/types.h"
#include "skygui/gui.h"
#include "skygui/engine.h"

namespace sg {

#define SKYGUI_VERSION      000100   // 0.1.0
#define SKYGUI_VERSION_NAME "0.1.0"

// Platform lifecycle (the SkyGUI core mod calls these; downstream mods just
// build Widgets and call sg::Gui::create()).
namespace platform {
  // Resolve engine, install hooks, wire host. Safe to call once.
  bool init();
  // Master switch: nothing is drawn until enabled.
  void setEnabled(bool on);
  bool isEnabled();
  bool isReady();
}

} // namespace sg

#endif
