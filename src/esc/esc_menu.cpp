// ============================================================================
// SkyGUI Platform - ESC system-button bar engine (hook + registry)
//
// Owns the reverse-engineered machinery; contains NO button definitions.
// Callers register buttons via esc::add(); the GatherSystemButtons detour
// appends each registered button to the game's output vector.
// ============================================================================
#include <windows.h>
#include <cstring>
#include <mutex>
#include <vector>
#include <unordered_map>
#include "includes/htmodloader.h"
#include "skygui/esc.h"

extern "C" HMODULE hModuleDll;

namespace sg { namespace esc {

// ---- RVAs (IDA-confirmed, base 0x140000000) --------------------------------
// GatherSystemButtons(rcx=controller, rdx=stringTables, r8=outVec<SystemButton>)
static const u32 RVA_GATHER     = 0x98FB80;
// std::vector<SystemButton>::grow_insert(vec, insertPos) -> new element ptr.
// Always reallocates (x1.5), move-ctors existing elems, default-constructs the
// inserted one (valid vtable/empty strings/default icon/null cbs). Stride 0x180.
static const u32 RVA_VEC_INSERT = 0x9A6900;
// std::string assign: sub_1401D7420(dstStr, srcStr) -- deep copy.
static const u32 RVA_STR_ASSIGN = 0x1D7420;

// SystemButton(0x180) layout (IDA-confirmed via the grow/copy ctor):
//   +0x00 vtable  +0x10 std::string id  +0xA0.. icon/visual POD
//   +0xF8 onClick std::function (0x40B tgc SSO; its __f_ ptr lands at +0x130)
//   +0x138 second std::function (__f_ at +0x170)
static const u32 BTN_STRIDE = 0x180;
static const u32 OFF_IDDESC = 0x10;   // 4-string id descriptor (sub_1401D7420 dst)
static const u32 OFF_IDSTR  = 0x20;   // the id std::string within the descriptor
static const u32 OFF_ICON   = 0xA0;
static const u32 ICON_BYTES = 0x50;   // POD icon+visual span the game copies
static const u32 OFF_CB     = 0xF8;   // onClick std::function object

// tgc std::function manager helpers (shared with engine.cpp's fabrication).
static const u32 RVA_STDFN_THROW   = 0x1A33F0;
static const u32 RVA_STDFN_DESTROY = 0x1A3400;
static const u32 RVA_STDFN_GETPTR  = 0x1A3420;

// Game std::string ABI: data@+0, size@+0x10, cap@+0x18; long form when cap>=0x10.
struct GameStr { const char* data; u64 _pad; u64 size; u64 cap; };  // 0x20 bytes
// The button's id is a 0x90-byte "4-string descriptor" at element+0x10 (copied
// by sub_1402834B0 / sub_1401D7420). String #1 (the id) is at descriptor+0x10 =
// element+0x20; strings #2..#4 are unused (empty) for stock buttons.
struct IdDesc { u64 h0; u64 h1; GameStr s[4]; };                    // 0x90 bytes

typedef void* (*PFN_Gather)(void*, void*, void*);
typedef void* (*PFN_VecInsert)(void** vec, void* insertPos);
typedef void  (*PFN_StrAssign)(void* dstStr, const void* srcStr);

static u8*           gBase         = nullptr;
static PFN_Gather    fn_origGather = nullptr;
static PFN_VecInsert fn_VecInsert  = nullptr;
static PFN_StrAssign fn_StrAssign  = nullptr;

volatile u64 gGatherCalls = 0;
volatile u64 gInjected    = 0;

// ---- tgc std::function fabrication (same 0x40B SSO ABI as engine.cpp) -------
// The button's onClick is a tgc std::function at slot+0xF8. We overwrite it
// with our own object whose invoke slot calls a stable ClickFn*.
struct StdFn { void* vtable; void* ctx; u64 pad[5]; void* self; };
static void* gClickVt[6];

static void* click_clone(void* self, void* out) {
  StdFn* s = (StdFn*)self; StdFn* o = (StdFn*)out;
  o->vtable = s->vtable; o->ctx = s->ctx; o->self = o;
  return o;
}
// Invoked on click. Signature is (self, ...); we ignore any extra args and
// just fire the registered ClickFn stashed in ctx.
static void click_invoke(void* self, void*) {
  ClickFn* fn = (ClickFn*)((StdFn*)self)->ctx;
  if (fn && *fn) (*fn)();
}

static void initClickVt() {
  gClickVt[0] = (void*)click_clone;
  gClickVt[1] = (void*)click_clone;
  gClickVt[2] = (void*)click_invoke;
  gClickVt[3] = (void*)(gBase + RVA_STDFN_THROW);
  gClickVt[4] = (void*)(gBase + RVA_STDFN_DESTROY);
  gClickVt[5] = (void*)(gBase + RVA_STDFN_GETPTR);
}

// Write a fabricated onClick std::function into slot+0xF8 pointing at `fn`
// (which must outlive the button -- we pass a pointer into the registry).
static void installClick(u8* slot, ClickFn* fn) {
  StdFn* cb = (StdFn*)(slot + OFF_CB);
  cb->vtable = gClickVt;
  cb->ctx    = fn;
  cb->self   = cb;               // SSO: __f_ (lands at slot+0x130) points to cb
}

// ---- registry + navigation -------------------------------------------------
static std::mutex gRegMutex;
static std::unordered_map<u32, Button> gReg;    // root buttons (stable storage)
static u32 gNextHandle = 1;

// Current sub-menu path: chain of opened buttons (pointers into gReg/children_,
// which are stable for the process lifetime). Empty => root bar.
static std::vector<Button*> gPath;
static Button gBack;                            // synthetic Back button (stable)

// Persistent icon cache: id -> the 0x50-byte POD icon block, harvested from the
// stock buttons whenever we see them at root. Lets sub-menu buttons (built on
// frames where we skip the stock gather) still show a real icon.
struct IconBytes { u8 b[ICON_BYTES]; };
static std::unordered_map<std::string, IconBytes> gIconCache;

u32 add(Button b) {
  std::lock_guard<std::mutex> lk(gRegMutex);
  u32 h = gNextHandle++;
  gReg.emplace(h, std::move(b));
  return h;
}
void remove(u32 handle) {
  std::lock_guard<std::mutex> lk(gRegMutex);
  gReg.erase(handle);
}

// ---- append logic ----------------------------------------------------------
static std::string readId(u8* elem) {
  u8* sp = elem + OFF_IDSTR;
  u64 size = *(u64*)(sp + 0x10);
  u64 cap  = *(u64*)(sp + 0x18);
  const char* data = (cap >= 0x10) ? *(const char**)sp : (const char*)sp;
  if (!data || size == 0 || size > 256) return {};
  return std::string(data, size);
}

// Set the button's id via the game's 0x90-byte 4-string descriptor copy
// (sub_1401D7420). Only string #1 is filled; the game resolves the label from it.
static void setId(u8* slot, const std::string& id) {
  if (!fn_StrAssign || id.empty()) return;
  IdDesc d; memset(&d, 0, sizeof(d));
  d.s[0].data = id.c_str();
  d.s[0].size = id.size();
  d.s[0].cap  = 0x1F;
  fn_StrAssign(slot + OFF_IDDESC, &d);
}

// Harvest icons from the stock buttons currently in the vector into gIconCache.
static void cacheStockIcons(void** vec) {
  u8* begin = (u8*)vec[0];
  u8* end   = (u8*)vec[1];
  for (u8* p = begin; p && p < end; p += BTN_STRIDE) {
    std::string id = readId(p);
    if (!id.empty()) memcpy(gIconCache[id].b, p + OFF_ICON, ICON_BYTES);
  }
}

// Append one button, giving it an icon from the cache + id/label + click.
static void appendOne(void** vec, Button& b) {
  u8* slot = (u8*)fn_VecInsert(vec, (void*)vec[1]);
  if (!slot) return;
  auto it = gIconCache.find(b.cloneIcon_);
  if (it != gIconCache.end()) memcpy(slot + OFF_ICON, it->second.b, ICON_BYTES);
  const std::string& idText = !b.text_.empty()     ? b.text_
                            : !b.cloneIcon_.empty() ? b.cloneIcon_
                            :                         b.id_;
  setId(slot, idText);
  if (b.effectiveClick_) installClick(slot, &b.effectiveClick_);
  gInjected++;
}

// Called from the detour. `stockGathered` = whether the vector holds the real
// stock buttons (root frame) or is empty (we skipped the gather for a sub-menu).
static void appendAll(void** vec, bool stockGathered) {
  std::lock_guard<std::mutex> lk(gRegMutex);

  if (stockGathered) cacheStockIcons(vec);   // keep the cache fresh from stock

  const bool submenu = !gPath.empty();

  // Resolve the list of buttons to show this frame.
  std::vector<Button*> list;
  if (!submenu) { for (auto& kv : gReg) list.push_back(&kv.second); }
  else          { for (auto& c : gPath.back()->children_) list.push_back(&c); }

  for (Button* pb : list) {
    if (!pb->children_.empty()) {              // has a sub-menu -> navigate in
      Button* target = pb;
      pb->effectiveClick_ = [target] {
        std::lock_guard<std::mutex> lk(gRegMutex);
        gPath.push_back(target);
      };
    } else {
      pb->effectiveClick_ = pb->onClick_;      // leaf -> the user's handler
    }
    appendOne(vec, *pb);
  }

  if (submenu) {                               // automatic Back button
    if (gBack.cloneIcon_.empty()) gBack.cloneIcon_ = "system_button_quit";
    if (gBack.text_.empty())      gBack.text_ = "返回";
    gBack.effectiveClick_ = [] {
      std::lock_guard<std::mutex> lk(gRegMutex);
      if (!gPath.empty()) gPath.pop_back();
    };
    appendOne(vec, gBack);
  }
}

static void* hook_Gather(void* controller, void* strTables, void* outVec) {
  // At root we let the game build the stock buttons (and harvest their icons);
  // inside a sub-menu we SKIP the stock gather so only our buttons show.
  bool submenu;
  { std::lock_guard<std::mutex> lk(gRegMutex); submenu = !gPath.empty(); }

  void* ret = nullptr;
  if (!submenu) ret = fn_origGather(controller, strTables, outVec);
  gGatherCalls++;
  if (outVec) appendAll((void**)outVec, /*stockGathered=*/!submenu);
  return ret;
}

static HTAsmFunction sfn = { "Tgui_GatherSystemButtons", nullptr, nullptr, nullptr };

bool init() {
  HTGameStatus st; memset(&st, 0, sizeof(st));
  HTGetGameStatus(&st);
  gBase = (u8*)st.baseAddr;
  if (!gBase) return false;

  fn_VecInsert = (PFN_VecInsert)(gBase + RVA_VEC_INSERT);
  fn_StrAssign = (PFN_StrAssign)(gBase + RVA_STR_ASSIGN);
  initClickVt();

  sfn.fn     = gBase + RVA_GATHER;
  sfn.detour = (void*)hook_Gather;
  if (HTAsmHookCreate(hModuleDll, &sfn) != HT_SUCCESS) return false;
  fn_origGather = (PFN_Gather)sfn.origin;
  HTAsmHookEnable(hModuleDll, sfn.fn);
  return true;
}

bool isReady() { return fn_origGather != nullptr; }

}} // namespace sg::esc
