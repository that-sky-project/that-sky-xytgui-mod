// ============================================================================
// SkyGUI Platform - retained widget tree (public API)
// ============================================================================
#ifndef SKYGUI_WIDGET_H
#define SKYGUI_WIDGET_H

#include "skygui/types.h"
#include <string>
#include <vector>
#include <functional>

namespace sg {

enum class WidgetType {
  Panel, Label, Button, Toggle, Slider, ScrollRegion, Image, Custom
};

struct Widget {
  WidgetType  type = WidgetType::Panel;
  std::string id;                 // stable id -> reconcile path
  std::string text;               // Label/Button caption ("$name" = bound)
  std::string image;              // Image widget: game image name (UiMenu*, ...)
  Dim         w, h;               // size (0 => auto)
  Color       color;              // fg/text color
  f32         fontPts = 0.0f;     // Label font size in points (0 => host default)
  Color       background;         // panel bg
  bool        hasBackground = false;
  Color       fillColor;          // inner fill (FillColor)
  bool        hasFill = false;
  Dim         padL, padT, padR, padB;   // padding (SideMeasure)
  bool        hasPadding = false;
  i32         flexDir = -1, flexJustify = -1, flexAlign = -1;  // flex (-1 = unset)
  f32         cornerRadius = -1.0f;     // FillGraphic corner radius (<0 = unset)
  f32         bgAlpha = -1.0f;          // background alpha (<0 = unset)
  f32         fillAlphaV = -1.0f;       // fill alpha (<0 = unset)
  f32         bgScaleV = -1.0f;         // background-graphic scale (<0 = unset)
  Dim         mTop, mBottom, mRight;    // margins
  bool        hasMargin = false;
  bool        visible = true;
  i32         zOrder = 0;
  std::vector<Widget> children;

  // code-behind hooks
  std::function<void()>            onClick;
  std::function<std::string()>     textBinding;   // dynamic text ($...)

  // ---- fluent builders ----
  Widget& setId(std::string v)          { id = std::move(v); return *this; }
  Widget& setText(std::string v)        { text = std::move(v); return *this; }
  Widget& img(std::string v)            { image = std::move(v); return *this; }
  Widget& size(Dim W, Dim H)            { w = W; h = H; return *this; }
  Widget& fg(Color c)                   { color = c; return *this; }
  Widget& font(f32 pts)                 { fontPts = pts; return *this; }   // Label text size (points)
  Widget& bg(Color c)                   { background = c; hasBackground = true; return *this; }
  Widget& fill(Color c)                 { fillColor = c; hasFill = true; return *this; }
  Widget& pad(Dim l, Dim t, Dim r, Dim b) { padL=l; padT=t; padR=r; padB=b; hasPadding=true; return *this; }
  Widget& pad(Dim all)                  { return pad(all, all, all, all); }
  Widget& row()                         { flexDir = 2; return *this; }   // Row (horizontal), Yoga=2
  Widget& col()                         { flexDir = 0; return *this; }   // Column (vertical), Yoga=0
  Widget& justify(i32 v)                { flexJustify = v; return *this; }   // main axis: 0=start 1=center 2=end 3=between
  Widget& align(i32 v)                  { flexAlign = v; return *this; }     // cross axis: 1=start 2=center 3=end 4=stretch
  Widget& center()                      { flexJustify = 1; flexAlign = 2; return *this; }  // main=center, cross=center(2)
  Widget& corner(f32 r)                 { cornerRadius = r; return *this; }
  Widget& bgA(f32 a)                    { bgAlpha = a; return *this; }
  Widget& fillA(f32 a)                  { fillAlphaV = a; return *this; }
  Widget& bgScale(f32 s)                { bgScaleV = s; return *this; }
  Widget& margin(Dim t, Dim r, Dim b)   { mTop=t; mRight=r; mBottom=b; hasMargin=true; return *this; }
  Widget& z(i32 v)                      { zOrder = v; return *this; }
  Widget& shown(bool v)                 { visible = v; return *this; }
  Widget& onClickDo(std::function<void()> f) { onClick = std::move(f); return *this; }
  Widget& bindText(std::function<std::string()> f) { textBinding = std::move(f); return *this; }
  Widget& add(Widget c)                 { children.push_back(std::move(c)); return *this; }
};

// factory helpers
inline Widget Panel()                    { Widget w; w.type = WidgetType::Panel; return w; }
inline Widget Label(std::string s = "")  { Widget w; w.type = WidgetType::Label; w.text = std::move(s); return w; }
inline Widget Button(std::string s = "") { Widget w; w.type = WidgetType::Button; w.text = std::move(s); return w; }
inline Widget Image(std::string name = ""){ Widget w; w.type = WidgetType::Image; w.image = std::move(name); return w; }
inline Widget Toggle()                   { Widget w; w.type = WidgetType::Toggle; return w; }
inline Widget Slider()                   { Widget w; w.type = WidgetType::Slider; return w; }

// ---- mount handle ----------------------------------------------------------
// A mounted GUI is submitted to the game every frame until destroyed.
class Gui {
public:
  Gui() = default;
  // Register a new native UI block named `name` rendering `root`.
  static Gui create(std::string name, Widget root);
  // Replace the whole tree (e.g. after data changes).
  void update(Widget root);
  // Access the live tree to mutate in place, then call markDirty().
  Widget* tree();
  void markDirty();
  // Stop rendering + unregister.
  void destroy();
  bool valid() const { return id_ != 0; }
  u32  id() const { return id_; }
private:
  u32 id_ = 0;
};

// platform host lifecycle (called by the platform core, not downstream mods)
namespace host {
  void init();                 // wire engine submit handler
  void shutdown();
  u32  mountCount();
}

} // namespace sg

#endif

