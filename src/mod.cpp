// ============================================================================
// SkyGUI Platform - mod entry 
// ============================================================================
#include <windows.h>
#include "includes/htmodloader.h"
#include "imgui.h"
#include "skygui/skygui.h"

extern "C" HMODULE hModuleDll;
using namespace sg;

static HTHandle hKeyPanel, hKeyToggle;
static bool gShowPanel = false;
static Gui  gDemo;

static void HTMLAPI onPanelKey(HTKeyEvent* e) {
  if ((e->flags & HTKeyEventFlags_Mask) == HTKeyEventFlags_Down)
    gShowPanel = !gShowPanel;
}
static void HTMLAPI onToggleKey(HTKeyEvent* e) {
  if ((e->flags & HTKeyEventFlags_Mask) == HTKeyEventFlags_Down) {
    bool on = !platform::isEnabled();
    platform::setEnabled(on);
    HTTellText(on ? "§aSkyGUI enabled" : "§eSkyGUI disabled");
  }
}

static void mountDemo() {
  Widget root = Panel()
    .setId("skygui.demo.root")
    .size(pctW(40), pctH(30))
    .bg(Color::rgba8(0x101018C0))
    .add(Label("Tgui").setId("title").fg(Color(1.0f, 0.85f, 0.2f, 1)))
    .add(Label("可行性测试 - ThatSkyProJects").setId("sub").fg(Color(0.3f, 0.9f, 1.0f, 1)))
    .add(Button("OK").setId("ok").fg(Color(0.4f, 1.0f, 0.4f, 1))
                     .onClickDo([]{ HTTellText("§aOK clicked"); }));
  gDemo = Gui::create("skygui.demo", std::move(root));
}

extern "C" __declspec(dllexport) HTStatus HTMLAPI HTModOnInit(void*) {
  if (!platform::init()) {
    HTTellText("§cSkyGUI: engine init failed");
    return HT_FAIL;
  }
  hKeyPanel  = HTHotkeyRegister(hModuleDll, "SkyGUI debug panel", HTKey_F4);
  hKeyToggle = HTHotkeyRegister(hModuleDll, "SkyGUI Main gui", HTKey_F5);
  HTHotkeyListen(hKeyPanel, onPanelKey);
  HTHotkeyListen(hKeyToggle, onToggleKey);
  mountDemo();
  // HTTellText("§aSkyGUI Platform v%s ready. F5=enable, F4=panel.", SKYGUI_VERSION_NAME);
  return HT_SUCCESS;
}

extern "C" __declspec(dllexport) HTStatus HTMLAPI HTModOnEnable(void*) { return HT_SUCCESS; }

extern "C" __declspec(dllexport) void HTMLAPI HTModRenderGui(float dt, void*) {
  (void)dt;
  if (!gShowPanel) return;
  ImGui::SetNextWindowSize(ImVec2(420, 260), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("SkyGUI Platform", &gShowPanel)) { ImGui::End(); return; }
  ImGui::Text("ready      : %s", platform::isReady() ? "yes" : "no");
  ImGui::Text("enabled    : %s (F5)", platform::isEnabled() ? "YES" : "no");
  ImGui::Text("mounts     : %u", host::mountCount());
  ImGui::Separator();
  ImGui::Text("spawnBlock : %llu", (unsigned long long)engine::gSubmitCalls);
  ImGui::Text("build cb   : %llu", (unsigned long long)engine::gBuildInvokes);
  ImGui::SameLine();
  if (engine::gBuildInvokes) ImGui::TextColored(ImVec4(0.4f,1,0.4f,1), "[live]");
  ImGui::Text("lastBuilder: %p", (void*)engine::gLastBuilder);
  bool en = platform::isEnabled();
  if (ImGui::Checkbox("Enable SkyGUI", &en)) platform::setEnabled(en);
  bool async = (engine::gBlockFlag == 0);
  if (ImGui::Checkbox("Async block (submit-once, flag=0)", &async))
    engine::gBlockFlag = async ? 0 : 1;
  ImGui::TextDisabled("async=submit-once (persists); sync=per-frame (clean hide)");
  ImGui::End();
}
