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
  Dim         w, h;               // size (0 => auto)
  Color       color;              // fg/text color
  Color       background;         // panel bg
  bool        hasBackground = false;
  bool        visible = true;
  i32         zOrder = 0;
  std::vector<Widget> children;

  // code-behind hooks
  std::function<void()>            onClick;
  std::function<std::string()>     textBinding;   // dynamic text ($...)

  // ---- fluent builders ----
  Widget& setId(std::string v)          { id = std::move(v); return *this; }
  Widget& setText(std::string v)        { text = std::move(v); return *this; }
  Widget& size(Dim W, Dim H)            { w = W; h = H; return *this; }
  Widget& fg(Color c)                   { color = c; return *this; }
  Widget& bg(Color c)                   { background = c; hasBackground = true; return *this; }
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

