#include "skygui/skygui.h"
#include "includes/htmodloader.h"

using namespace sg;

static Gui gPanel;
static int gCounter = 0;

extern "C" __declspec(dllexport) HTStatus HTMLAPI HTModOnInit(void*) {
  // Build a small panel with a live-bound counter label and a button.
  Widget root = Panel()
    .setId("hello.root")
    .size(pctW(30), px(160))
    .bg(Color::rgba8(0x1E1E28E0))
    .add(Label().setId("count")
                .bindText([]{ return "clicks: " + std::to_string(gCounter); }))
    .add(Button("Click me").setId("btn")
                .onClickDo([]{ gCounter++; }));

  gPanel = Gui::create("example.hello", std::move(root));
  return HT_SUCCESS;
}

extern "C" __declspec(dllexport) HTStatus HTMLAPI HTModOnEnable(void*) {
  return HT_SUCCESS;
}
