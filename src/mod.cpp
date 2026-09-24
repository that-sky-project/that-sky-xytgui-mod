// ============================================================================
// SkyGUI Platform - mod entry 
// ============================================================================
#include <windows.h>
#include "includes/htmodloader.h"
#include "imgui.h"
#include "skygui/skygui.h"
#include "skygui/esc.h"

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
    HTTellText(on ? "§aSkyGUI shown" : "§eSkyGUI hidden");
  }
}

static void mountDemo1() {
  Widget root = Label("Tgui SkyGUI test")
    .setId("skygui.demo.title")
    .fg(Color(1.0f, 0.85f, 0.2f, 1));

  gDemo = Gui::create("skygui.demo", std::move(root));
}

static void mountDemo() {
  Widget root = Panel()
    .setId("skygui.demo.root")
    .size(px(420), px(280))
    .bg(Color::rgba8(0x161A28F0))       // 深蓝底
    .center()                           // 居中
    .add(Label("ThatGameUI 可行性测试 · ThatSkyProjects").setId("sub").font(16.0f).fg(Color(0.45f, 0.85f, 1.0f, 1)));

  gDemo = Gui::create("skygui.demo", std::move(root));
}

extern "C" __declspec(dllexport) HTStatus HTMLAPI HTModOnInit(void*) {
  if (!platform::init()) {
    HTTellText("§cSkyGUI: engine init failed");
    return HT_FAIL;
  }
  hKeyPanel  = HTHotkeyRegister(hModuleDll, "SkyGUI debug panel", HTKey_F4);
  hKeyToggle = HTHotkeyRegister(hModuleDll, "SkyGUI show/hide our UI", HTKey_F5);
  HTHotkeyListen(hKeyPanel, onPanelKey);
  HTHotkeyListen(hKeyToggle, onToggleKey);

  esc::init();
  esc::add(esc::Button()
    .id("skygui_button")
    .cloneIcon("UiMenuStarScan")   
    .text("Test")
    .child(esc::Button().id("skygui_ex1").cloneIcon("UiOutfitBodyAP11Ancestor").text("示例 1")
           .onClick([]{ HTTellText("§aButtion 1"); }))
    .child(esc::Button().id("skygui_ex2").cloneIcon("UiSocialArmWrestle").text("Who is gay?")
           .onClick([]{ HTTellText("§aCheck ColorSky §c§;is Gay!!!"); }))
    );

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
  ImGui::Text("mounts     : %u", host::mountCount());
  ImGui::Separator();
  ImGui::Text("spawnBlock : %llu", (unsigned long long)engine::gSubmitCalls);
  ImGui::Text("build cb   : %llu", (unsigned long long)engine::gBuildInvokes);
  ImGui::SameLine();
  if (engine::gBuildInvokes) ImGui::TextColored(ImVec4(0.4f,1,0.4f,1), "[live]");
  ImGui::Text("lastBuilder: %p", (void*)engine::gLastBuilder);
  bool en = platform::isEnabled();
  if (ImGui::Checkbox("Enable SkyGUI", &en)) platform::setEnabled(en);

  ImGui::Separator();
  ImGui::End();
}
