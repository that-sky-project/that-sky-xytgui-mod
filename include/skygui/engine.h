// ============================================================================
// SkyGUI Platform - tgui engine adapter (internal)
// ============================================================================
#ifndef SKYGUI_ENGINE_H
#define SKYGUI_ENGINE_H

#include "skygui/types.h"
#include <functional>

namespace sg {

// Opaque live tgui Builder handed to build callbacks by the reconcile pass.
using Builder = void;

namespace engine {

using BuildFn  = std::function<void(Builder*)>;
using SubmitFn = std::function<void()>;

// Resolve all tgui addresses against the game module base + install the
// per-frame submit hook (TguiBarn::Update). Returns false if base unknown.
bool init();
bool isReady();

// The host sets ONE submit handler; the engine calls it every frame from
// inside TguiBarn::Update, just before Reconcile. Inside it the host calls
// spawnBlock() for each GUI it wants live this frame.
void setSubmitHandler(SubmitFn fn);

// Master enable (nothing is submitted while disabled). Off by default.
void setEnabled(bool on);
bool isEnabled();

// Register a native UI block for this frame. `build` runs during Reconcile
// with a live Builder. Call ONLY from within the submit handler.
void spawnBlock(const char* name, BuildFn build);

// ---- low-level widget emit (operate on the Builder inside a BuildFn) -------
// These wrap the game's own widget/property builders. Best-effort v1; the
// exact arg layouts are still being tuned (injection plumbing is proven).
void* spawnElement(Builder* b);                 // create + push a child element
void  setText(void* elem, const char* utf8);    // element "Text" property
void  setBoolProp(void* elem, const char* prop, bool v);
void  setIntProp(void* elem, const char* prop, i32 v);
void  setBgColor(void* elem, const float* rgba); // element "BackgroundColor" (4 floats)

// High-level: emit a native text label via the game's own Label builder
// (Tgui_Label 0x7559F0). `idstr` = stable reconcile identity. `rgba` (nullable)
// sets TextColor; `fontPts` (>0) sets FontSize in points. Builds the game's
// string ABI + _CallerScope + TextArgs internally.
void* drawLabel(Builder* b, const char* idstr, const char* utf8,
                const float* rgba = nullptr, float fontPts = 0.0f);

// High-level: a background CONTAINER. `panelBegin` spawns a container element
// (sized colored box) and pushes a group so widgets emitted before the matching
// `panelEnd` nest INSIDE it. w/h are tgui Dimensions; `rgba` is the fill color.
void* panelBegin(Builder* b, const char* idstr,
                 float wVal, u8 wUnit, float hVal, u8 hUnit,
                 const float* rgba = nullptr);
void  panelEnd(Builder* b);

// High-level: emit an IMAGE element filled with a game image resolved by NAME
// (any UiMenu*/system_button*/... name). w/h are tgui Dimensions.
void* drawImage(Builder* b, const char* idstr, const char* imageName,
                float wVal, u8 wUnit, float hVal, u8 hUnit);

// ---- shared element property setters (operate on a live element handle
// returned by panelBegin/drawLabel/drawButton). Colors are 4 linear floats;
// dimension values pass a tgui unit (see sg::Unit). ------------------------
void  setFillColor(void* elem, const float* rgba);
void  setFillAlpha(void* elem, float a);
void  setBgAlpha(void* elem, float a);
void  setCornerRadius(void* elem, float r);
void  setBgScale(void* elem, float s);
void  setWidth(void* elem, float v, u8 unit);
void  setHeight(void* elem, float v, u8 unit);
void  setSize(void* elem, float wV, u8 wU, float hV, u8 hU);  // both axes (proven)
void  setMarginRight(void* elem, float v, u8 unit);
void  setTop(void* elem, float v, u8 unit);
void  setBottom(void* elem, float v, u8 unit);
void  setPadding(void* elem, float l, u8 lu, float t, u8 tu,
                 float r, u8 ru, float b, u8 bu);
// Flex layout (Yoga): dir 0=row/1=column; justify/align 0=start/1=center/2=end/
// 3=spaceBetween; pass <0 to leave unchanged.
void  setLayout(void* elem, int layoutDir, int justify, int align);

// diagnostics
extern volatile u64 gSubmitCalls;   // spawnBlock calls
extern volatile u64 gBuildInvokes;  // build callbacks actually fired
extern volatile void* gLastBuilder;
extern volatile u8  gBlockFlag;     // SpawnBlock a6 flag (toggle to test input)

} // namespace engine
} // namespace sg

#endif
