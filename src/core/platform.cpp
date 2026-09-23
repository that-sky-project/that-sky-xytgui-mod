// ============================================================================
// SkyGUI Platform 
// ============================================================================
#include "skygui/skygui.h"
#include <cmath>

namespace sg {

// sRGB 0-255 (0xRRGGBBAA) -> linear RGBA, matching tgui's gamma-2.4 colours.
Color Color::rgba8(u32 hex) {
  auto srgb = [](f32 c) {
    return std::pow(c, 2.4f); // tgui uses pow(x, 2.4) (see ColorSpec::White)
  };
  f32 r = (f32)((hex >> 24) & 0xFF) / 255.0f;
  f32 g = (f32)((hex >> 16) & 0xFF) / 255.0f;
  f32 b = (f32)((hex >> 8)  & 0xFF) / 255.0f;
  f32 a = (f32)( hex        & 0xFF) / 255.0f;
  return Color(srgb(r), srgb(g), srgb(b), a);
}

namespace platform {

static bool gInited = false;

bool init() {
  if (gInited) return true;
  if (!engine::init()) return false;
  host::init();
  gInited = true;
  return true;
}
void setEnabled(bool on) { engine::setEnabled(on); }
bool isEnabled() { return engine::isEnabled(); }
bool isReady() { return gInited && engine::isReady(); }

} // namespace platform
} // namespace sg
