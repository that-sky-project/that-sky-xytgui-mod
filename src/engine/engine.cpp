// ============================================================================
// SkyGUI Platform - tgui engine adapter implementation
// ============================================================================
#include <windows.h>
#include <deque>
#include <string>
#include <unordered_map>
#include "includes/htmodloader.h"
#include "skygui/engine.h"

extern "C" HMODULE hModuleDll;

namespace sg { namespace engine {

// ---- RVAs (IDA-confirmed, base 0x140000000, match running Sky.exe) ---------
static const u32 RVA_G_UIBARN        = 0x24B9588;
static const u32 RVA_SPAWNBLOCK_WRAP = 0x715D10;
static const u32 RVA_BARN_UPDATE     = 0x716C40;
static const u32 RVA_STDFN_THROW     = 0x1A33F0;
static const u32 RVA_STDFN_DESTROY   = 0x1A3400;
static const u32 RVA_STDFN_GETPTR    = 0x1A3420;
static const u32 RVA_SPAWN_ELEMENT   = 0x714DD0;
static const u32 RVA_ELEM_SETTEXT    = 0x1D6A80;
static const u32 RVA_SET_BOOL_PROP   = 0x71BE70;
static const u32 RVA_SET_INT_PROP    = 0x71D090;
static const u32 RVA_LABEL           = 0x7559F0;  // Tgui_Label(scope, StlStr*)
static const u32 RVA_SCOPE_CTOR      = 0x189F80;  // Tgui_CallerScope_ctor(scope,builder)
static const u32 RVA_ELEM_TEXTCOLOR  = 0x1D6DC0;  // Tgui_Element_TextColor(elem,Color*)
static const u32 RVA_ELEM_FONTSIZE   = 0x1D6F40;  // Tgui_Element_FontSize(elem,Dim*)

// Builder field offsets (IDA-confirmed).
static const u32 OFF_BUILDER_SRCSTR  = 4600;      // +0x11F8 caller source string
static const u32 OFF_BUILDER_SCOPEFLAG = 4752;    // +0x1270 scope flag (=1)

// ---- resolved function pointers --------------------------------------------
typedef void (*PFN_SpawnBlockWrap)(char*, char, void*, void*);
typedef void* (*PFN_SpawnElement)(void*, void*);
typedef void* (*PFN_ElemSetText)(void*, void*);
typedef void* (*PFN_Label)(void* scope, void* stlStr);
typedef void* (*PFN_ScopeCtor)(void* scopeOut, void* builder);
typedef void* (*PFN_ElemColor)(void* elem, void* color16);   // Color = 4 floats
typedef void* (*PFN_ElemFontSize)(void* elem, void* dim8);   // Dim = {f32,u8}

static u8*   gBase = nullptr;
static PFN_SpawnBlockWrap fn_SpawnBlockWrap = nullptr;
static PFN_SpawnElement   fn_SpawnElement   = nullptr;
static PFN_ElemSetText    fn_ElemSetText    = nullptr;
static PFN_Label          fn_Label          = nullptr;
static PFN_ScopeCtor      fn_ScopeCtor      = nullptr;
static PFN_ElemColor      fn_TextColor      = nullptr;
static PFN_ElemFontSize   fn_FontSize       = nullptr;

// Game's std::string ABI as read by Tgui_TextArgs_fromString:
//   size @ +0x10 ([2]);  cap @ +0x18 ([3]);  data = (cap>=0x10 ? *[0] : &[0]).
struct StlStr { const char* data; u64 _pad; u64 size; u64 cap; };

static SubmitFn gSubmit;
static volatile bool gEnabled = false;

volatile u64  gSubmitCalls  = 0;
volatile u64  gBuildInvokes = 0;
volatile void* gLastBuilder = nullptr;
// SpawnBlock a6 flag. Reconcile semantics (IDA-confirmed, TguiBarn_Reconcile 0x140721830):
//   1 = SYNC : node's alive-bit (node+4) is cleared after each build, so the block
//              only survives if RE-SUBMITTED every frame. Stop submitting => the
//              node is not re-marked alive => Reconcile's removal path frees it
//              (calls result cb, destroys callbacks, erases). Clean HIDE for free.
//   0 = ASYNC: submit ONCE; the node stays "running" across frames WITHOUT any
//              resubmission -- that is exactly why re-submitting asserts "Async
//              function already running" @0x140722548. It persists until it
//              completes or ALL blocks are cleared. There is NO
//              clean per-name close exposed by the engine.
volatile u8   gBlockFlag = 1;   // sync per-frame (clean hide on stop); async(0) has no per-name close
static volatile bool gArmed = false;   // async: already submitted this enable-run?

static inline void* rva(u32 off) { return gBase ? (gBase + off) : nullptr; }

// ---- tgc std::function object (0x40 bytes, libc++-style SSO) ----------------
struct StdFn {
  void* vtable;   // +0x00
  void* ctx;      // +0x08  (we stash a BuildFn* here)
  u64   pad[5];   // +0x10..+0x37
  void* self;     // +0x38  __f_ (points to &this when SSO)
};

static void* gBuildVt[6];
static void* gNoopVt[6];

// ---- std::function manager callbacks ---------------------------------------
static void* my_clone(void* self, void* out) {
  StdFn* s = (StdFn*)self;
  StdFn* o = (StdFn*)out;
  o->vtable = s->vtable;
  o->ctx    = s->ctx;
  return o;   // wrapper stores this as the clone's __f_
}

// Build invoke: ctx = BuildFn*; call it with the live Builder.
static void my_build_invoke(void* self, void* builder) {
  gBuildInvokes++;
  gLastBuilder = builder;
  BuildFn* fn = (BuildFn*)((StdFn*)self)->ctx;
  if (fn && *fn) (*fn)((Builder*)builder);
}

// Result invoke: ignore (no result handling yet).
static void my_noop_invoke(void* self, void* arg) { (void)self; (void)arg; }

// Persistent per-block storage of build lambdas. Keyed by block name so the
// address of each entry is STABLE (unordered_map guarantees reference
// stability across inserts/rehash). The barn's Reconcile may re-invoke a
// node's stored callback even on frames we don't re-submit, so the ctx must
// stay valid across frames -- a per-frame deque would dangle.
static std::unordered_map<std::string, BuildFn> gBlockFns;
static StdFn gCbBuild;
static StdFn gCbNoop;

static void initVtables() {
  gBuildVt[0] = (void*)my_clone;
  gBuildVt[1] = (void*)my_clone;
  gBuildVt[2] = (void*)my_build_invoke;
  gBuildVt[3] = rva(RVA_STDFN_THROW);
  gBuildVt[4] = rva(RVA_STDFN_DESTROY);
  gBuildVt[5] = rva(RVA_STDFN_GETPTR);
  gNoopVt[0] = (void*)my_clone;
  gNoopVt[1] = (void*)my_clone;
  gNoopVt[2] = (void*)my_noop_invoke;
  gNoopVt[3] = gBuildVt[3];
  gNoopVt[4] = gBuildVt[4];
  gNoopVt[5] = gBuildVt[5];
}

void spawnBlock(const char* name, BuildFn build) {
  if (!fn_SpawnBlockWrap) return;
  gSubmitCalls++;
  BuildFn& slot = gBlockFns[name];   // stable address (kept across frames)
  slot = std::move(build);

  gCbBuild.vtable = gBuildVt; gCbBuild.ctx = &slot;    gCbBuild.self = &gCbBuild;
  gCbNoop.vtable  = gNoopVt;  gCbNoop.ctx  = nullptr;  gCbNoop.self  = &gCbNoop;

  // `gBlockFlag` selects SYNC(1)/ASYNC(0) reconcile behavior (see decl above).
  fn_SpawnBlockWrap((char*)name, (char)gBlockFlag, &gCbBuild, &gCbNoop);
}

// ---- per-frame submit hook on TguiBarn::Update -----------------------------
typedef void* (*PFN_BarnUpdate)(void*);
static PFN_BarnUpdate fn_origUpdate = nullptr;
static HTAsmFunction sfn_Update = { "TguiBarn::Update", nullptr, nullptr, nullptr };

static void* hook_Update(void* a1) {
  // Submit ONLY while enabled. Before the first enable there is no node at all
  // => zero interference with the game's own tgui input/nav.
  //   SYNC (flag=1): re-submit every frame; stopping (disable) auto-removes it.
  //   ASYNC(flag=0): submit exactly ONCE per enable-run; re-submitting an
  //                  already-running async block asserts, so we arm-latch it.
  if (gEnabled && gSubmit) {
    if (gBlockFlag != 0) {
      gSubmit();                        // sync: per-frame resubmit
    } else if (!gArmed) {
      gSubmit();                        // async: submit once
      gArmed = true;
    }
  }
  if (!gEnabled)
    gArmed = false;                     // re-arm for the next enable
  return fn_origUpdate(a1);
}

bool isReady() { return fn_SpawnBlockWrap != nullptr && fn_origUpdate != nullptr; }
void setSubmitHandler(SubmitFn fn) { gSubmit = std::move(fn); }
void setEnabled(bool on) { gEnabled = on; }
bool isEnabled() { return gEnabled; }

bool init() {
  HTGameStatus st; memset(&st, 0, sizeof(st));
  HTGetGameStatus(&st);
  gBase = (u8*)st.baseAddr;
  if (!gBase) return false;

  fn_SpawnBlockWrap = (PFN_SpawnBlockWrap)rva(RVA_SPAWNBLOCK_WRAP);
  fn_SpawnElement   = (PFN_SpawnElement)rva(RVA_SPAWN_ELEMENT);
  fn_ElemSetText    = (PFN_ElemSetText)rva(RVA_ELEM_SETTEXT);
  fn_Label          = (PFN_Label)rva(RVA_LABEL);
  fn_ScopeCtor      = (PFN_ScopeCtor)rva(RVA_SCOPE_CTOR);
  fn_TextColor      = (PFN_ElemColor)rva(RVA_ELEM_TEXTCOLOR);
  fn_FontSize       = (PFN_ElemFontSize)rva(RVA_ELEM_FONTSIZE);
  initVtables();

  sfn_Update.fn = rva(RVA_BARN_UPDATE);
  sfn_Update.detour = (void*)hook_Update;
  if (HTAsmHookCreate(hModuleDll, &sfn_Update) != HT_SUCCESS) return false;
  fn_origUpdate = (PFN_BarnUpdate)sfn_Update.origin;
  HTAsmHookEnable(hModuleDll, sfn_Update.fn);
  return true;
}

// ---- widget emit (best-effort v1; arg layouts tuned later) -----------------
void* spawnElement(Builder* b) {
  if (!fn_SpawnElement || !b) return nullptr;
  // a4 (of Element_Create_impl) is a "children descriptor": u32 count @ +0x40,
  // child element ptrs at +0. A zeroed buffer => count 0 => leaf, no crash.
  static u64 emptyChildren[16] = { 0 };   // 128 bytes, count@+0x40 == 0
  return fn_SpawnElement(b, &emptyChildren);
}
void setText(void* elem, const char* utf8) {
  // TODO: build a proper TextArgs from utf8; wiring present, args to tune.
  (void)elem; (void)utf8;
}
void setBoolProp(void* elem, const char* prop, bool v) { (void)elem;(void)prop;(void)v; }
void setIntProp(void* elem, const char* prop, i32 v)   { (void)elem;(void)prop;(void)v; }

// Emit a native text label using the game's own Tgui_Label(scope, StlStr*).
// A _CallerScope must be constructed first (Tgui_CallerScope_ctor) so the
// builder's scope-depth stack is pushed; Tgui_Label pops it at the end.
// `idstr` gives the widget a stable reconcile identity (Builder source str).
void drawLabel(Builder* b, const char* idstr, const char* utf8,
               const float* rgba, float fontPts) {
  if (!fn_Label || !fn_ScopeCtor || !b || !utf8) return;
  StlStr s;
  s.data = utf8; s._pad = 0; s.size = 0; s.cap = 0x1F;
  for (const char* p = utf8; *p; ++p) s.size++;

  u8* bb = (u8*)b;
  *(const char**)(bb + OFF_BUILDER_SRCSTR) = idstr ? idstr : utf8;
  *(bb + OFF_BUILDER_SCOPEFLAG) = 1;

  u8 scope[64];
  memset(scope, 0, sizeof(scope));
  fn_ScopeCtor(scope, b);                 // push scope + reconcile path hash
  void* elem = fn_Label(scope, &s);       // spawn element + set text; pops scope
  if (!elem) return;

  // Style the returned element (Element property setters; work post-pop).
  if (fontPts > 0.0f && fn_FontSize) {
    struct { float v; u8 unit; u8 pad[3]; } dim = { fontPts, 1 /*Points*/, {0,0,0} };
    fn_FontSize(elem, &dim);
  }
  if (rgba && fn_TextColor) {
    float col[4] = { rgba[0], rgba[1], rgba[2], rgba[3] };
    fn_TextColor(elem, col);
  }
}

}} // namespace sg::engine
