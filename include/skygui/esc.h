// ============================================================================
// SkyGUI Platform - ESC / pause "system button" bar (public registration API)
// ============================================================================
#ifndef SKYGUI_ESC_H
#define SKYGUI_ESC_H

#include "skygui/types.h"
#include <functional>
#include <string>
#include <vector>

namespace sg { namespace esc {

using ClickFn = std::function<void()>;

// One registered system button. Built fluently:
//   esc::Button().id("x").cloneIcon("system_button_support").onClick([]{...})
// - id       : our button's identity (reconcile key).
// - cloneIcon: id of a STOCK system button whose icon to reuse (e.g.
//              "system_button_support"); "" => clone the first stock button.
// - text     : OPTIONAL display label. If set, we use it; if empty, the game
//              resolves the label from the id via its internal localization.
// - onClick  : invoked when the button is pressed (ignored if it has children).
// - children : if non-empty, pressing this button opens a SUB-MENU showing the
//              children (plus an automatic Back button); onClick is not used.
struct Button {
  std::string         id_;
  std::string         cloneIcon_;
  std::string         text_;
  ClickFn             onClick_;
  std::vector<Button> children_;
  ClickFn             effectiveClick_;   // engine-computed each frame (internal)

  Button& id(std::string v)        { id_ = std::move(v);        return *this; }
  Button& cloneIcon(std::string v) { cloneIcon_ = std::move(v); return *this; }
  Button& text(std::string v)      { text_ = std::move(v);      return *this; }
  Button& onClick(ClickFn f)       { onClick_ = std::move(f);   return *this; }
  Button& child(Button b)          { children_.push_back(std::move(b)); return *this; }
};

// Install the GatherSystemButtons hook. Safe to call once at startup.
bool init();
bool isReady();

// Register / remove a system button. `add` returns a handle (0 = failure).
// Buttons appear the next time the bar is (re)built.
u32  add(Button b);
void remove(u32 handle);

// diagnostics
extern volatile u64 gGatherCalls;  // times GatherSystemButtons ran
extern volatile u64 gInjected;     // total buttons appended so far

}} // namespace sg::esc

#endif
