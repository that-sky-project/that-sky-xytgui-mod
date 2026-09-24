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

// Apply the shared element property setters that any element can take.
static void applyStyle(void* e, const Widget& w) {
  if (!e) return;
  if (w.hasFill) {
    float fc[4] = { w.fillColor.r, w.fillColor.g, w.fillColor.b, w.fillColor.a };
    engine::setFillColor(e, fc);
  }
  if (w.hasPadding)
    engine::setPadding(e, w.padL.value, (u8)w.padL.unit, w.padT.value, (u8)w.padT.unit,
                          w.padR.value, (u8)w.padR.unit, w.padB.value, (u8)w.padB.unit);
  if (w.hasMargin) {
    engine::setTop(e, w.mTop.value, (u8)w.mTop.unit);
    engine::setBottom(e, w.mBottom.value, (u8)w.mBottom.unit);
    engine::setMarginRight(e, w.mRight.value, (u8)w.mRight.unit);
  }
  if (w.flexDir >= 0 || w.flexJustify >= 0 || w.flexAlign >= 0)
    engine::setLayout(e, w.flexDir, w.flexJustify, w.flexAlign);
  if (w.cornerRadius >= 0.0f) engine::setCornerRadius(e, w.cornerRadius);
  if (w.bgAlpha    >= 0.0f)   engine::setBgAlpha(e, w.bgAlpha);
  if (w.fillAlphaV >= 0.0f)   engine::setFillAlpha(e, w.fillAlphaV);
  if (w.bgScaleV   >= 0.0f)   engine::setBgScale(e, w.bgScaleV);
}

// Walk one widget subtree, emitting native tgui calls on the live Builder.
static void emitWidget(Builder* b, const Widget& w) {
  if (!w.visible) return;

  if (w.type == WidgetType::Panel) {
    const char* id = w.id.empty() ? "panel" : w.id.c_str();
    float col[4] = { w.background.r, w.background.g, w.background.b, w.background.a };
    void* pe = engine::panelBegin(b, id, w.w.value, (u8)w.w.unit,
                                  w.h.value, (u8)w.h.unit,
                                  w.hasBackground ? col : nullptr);
    applyStyle(pe, w);
    for (const auto& c : w.children)
      emitWidget(b, c);
    engine::panelEnd(b);
    return;
  }

  // Image = a native element filled with a game image (resolved by name).
  if (w.type == WidgetType::Image) {
    if (!w.image.empty()) {
      const char* id = w.id.empty() ? w.image.c_str() : w.id.c_str();
      void* ie = engine::drawImage(b, id, w.image.c_str(),
                                   w.w.value, (u8)w.w.unit, w.h.value, (u8)w.h.unit);
      applyStyle(ie, w);
    }
    return;
  }

  const std::string* txt = &w.text;
  std::string bound;
  if (w.textBinding) { bound = w.textBinding(); txt = &bound; }

  switch (w.type) {
    case WidgetType::Label:
    case WidgetType::Button: {
      if (!txt->empty()) {
        const char* id = w.id.empty() ? txt->c_str() : w.id.c_str();
        float col[4] = { w.color.r, w.color.g, w.color.b, w.color.a };
        float fpt = (w.fontPts > 0.0f) ? w.fontPts : 20.0f;   // default 20pt (was 30, too big/cramped)
        void* le = engine::drawLabel(b, id, txt->c_str(), col, fpt);
        applyStyle(le, w);
      }
      break;
    }
    default:
      break;
  }

  for (const auto& c : w.children)
    emitWidget(b, c);
}

static void submitAll() {
  for (auto& kv : gMounts) {
    Mount& m = kv.second;
    Widget* root = m.root.get();
    engine::spawnBlock(m.name.c_str(), [root](Builder* b) {
      if (engine::isEnabled() && root)
        emitWidget(b, *root);
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
