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

// High-level: emit a native text label via the game's own Label builder
// (Tgui_Label 0x7559F0). `idstr` = stable reconcile identity. `rgba` (nullable)
// sets TextColor; `fontPts` (>0) sets FontSize in points. Builds the game's
// string ABI + _CallerScope + TextArgs internally.
void  drawLabel(Builder* b, const char* idstr, const char* utf8,
                const float* rgba = nullptr, float fontPts = 0.0f);

// diagnostics
extern volatile u64 gSubmitCalls;   // spawnBlock calls
extern volatile u64 gBuildInvokes;  // build callbacks actually fired
extern volatile void* gLastBuilder;
extern volatile u8  gBlockFlag;     // SpawnBlock a6 flag (toggle to test input)

} // namespace engine
} // namespace sg

#endif
