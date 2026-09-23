// ============================================================================
// SkyGUI Platform - core types
// ============================================================================
#ifndef SKYGUI_TYPES_H
#define SKYGUI_TYPES_H

#include <cstdint>
#include <cstddef>

namespace sg {

using u8  = uint8_t;   using i8  = int8_t;
using u16 = uint16_t;  using i16 = int16_t;
using u32 = uint32_t;  using i32 = int32_t;
using u64 = uint64_t;  using i64 = int64_t;
using f32 = float;     using f64 = double;

// tgui Dimension units (from tgui::Dimension::Convert switch; 1-based).
enum class Unit : u8 {
  Points  = 1,   // base unit
  Pixels  = 2,   // * DPI
  Inches  = 3,
  PctW    = 4,   // percent of a reference axis
  PctH    = 5,
  PctMinW = 6,
  PctMinH = 7,
  PctOwner = 8,  // percent of owner size
};

// A tgui Dimension = {float value; u8 unit} (8 bytes on the wire).
struct Dim {
  f32  value = 0.0f;
  Unit unit  = Unit::Points;

  constexpr Dim() = default;
  constexpr Dim(f32 v, Unit u) : value(v), unit(u) {}
};

// User-literal-ish helpers (mirror tgui _px/_pct/_aut).
constexpr Dim px(f32 v)   { return Dim(v, Unit::Pixels); }
constexpr Dim pts(f32 v)  { return Dim(v, Unit::Points); }
constexpr Dim pctW(f32 v) { return Dim(v, Unit::PctW); }
constexpr Dim pctH(f32 v) { return Dim(v, Unit::PctH); }
constexpr Dim owner(f32 v){ return Dim(v, Unit::PctOwner); }

// tgui Color = 4x f32 linear RGBA. Constructors accept sRGB 0-255 or hex.
struct Color {
  f32 r = 1, g = 1, b = 1, a = 1;
  constexpr Color() = default;
  constexpr Color(f32 R, f32 G, f32 B, f32 A = 1) : r(R), g(G), b(B), a(A) {}

  // 0xRRGGBBAA (sRGB) -> linear.
  static Color rgba8(u32 hex);
};

struct Vec2 { f32 x = 0, y = 0; };

} // namespace sg

#endif
