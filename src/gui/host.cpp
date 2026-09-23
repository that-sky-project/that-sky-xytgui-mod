// ============================================================================
// SkyGUI Platform - host: mounts retained Widget trees, emits them per frame
// ============================================================================
#include "skygui/gui.h"
#include "skygui/engine.h"
#include <unordered_map>
#include <memory>

namespace sg {

struct Mount {
  std::string name;
  std::shared_ptr<Widget> root;
};

static std::unordered_map<u32, Mount> gMounts;
static u32 gNextId = 1;
static bool gInited = false;

// Walk one widget subtree, emitting native tgui calls on the live Builder.
static void emitWidget(Builder* b, const Widget& w) {
  if (!w.visible) return;

  // Resolve dynamic text binding if present.
  const std::string* txt = &w.text;
  std::string bound;
  if (w.textBinding) { bound = w.textBinding(); txt = &bound; }

  switch (w.type) {
    case WidgetType::Label:
    case WidgetType::Button: {
      if (!txt->empty()) {
        const char* id = w.id.empty() ? txt->c_str() : w.id.c_str();
        float col[4] = { w.color.r, w.color.g, w.color.b, w.color.a };
        float pts = (w.type == WidgetType::Button) ? 34.0f : 30.0f;
        engine::drawLabel(b, id, txt->c_str(), col, pts);
      }
      break;
    }
    default:
      break;   // Panel etc.: container -- just recurse for now
  }

  for (const auto& c : w.children)
    emitWidget(b, c);
}

static void submitAll() {
  for (auto& kv : gMounts) {
    Mount& m = kv.second;
    Widget* root = m.root.get();
    engine::spawnBlock(m.name.c_str(), [root](Builder* b) {
      if (engine::isEnabled() && root) emitWidget(b, *root);
    });
  }
}

namespace host {
  void init() {
    if (gInited) return;
    engine::setSubmitHandler(submitAll);
    gInited = true;
  }
  void shutdown() { gMounts.clear(); }
  u32 mountCount() { return (u32)gMounts.size(); }
}

// ---- Gui handle ----
Gui Gui::create(std::string name, Widget root) {
  Gui g;
  g.id_ = gNextId++;
  Mount m;
  m.name = std::move(name);
  m.root = std::make_shared<Widget>(std::move(root));
  gMounts[g.id_] = std::move(m);
  return g;
}
void Gui::update(Widget root) {
  auto it = gMounts.find(id_);
  if (it != gMounts.end()) it->second.root = std::make_shared<Widget>(std::move(root));
}
Widget* Gui::tree() {
  auto it = gMounts.find(id_);
  return it != gMounts.end() ? it->second.root.get() : nullptr;
}
void Gui::markDirty() { /* immediate-mode: re-emitted every frame anyway */ }
void Gui::destroy() {
  gMounts.erase(id_);
  id_ = 0;
}

} // namespace sg
