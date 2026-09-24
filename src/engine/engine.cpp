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
static const u32 RVA_ELEM_BGCOLOR    = 0x18C520;  // Tgui_Element_BackgroundColor(elem,Color*)
                                                  //  -> ModifyProperty "BackgroundColor";
                                                  //     fallback direct-writes elem+0x240 (4 floats)
                                                  //     and sets flag elem+0x3A |= 0x10
static const u32 RVA_ELEM_SIZE       = 0x18D4A0;  // Tgui_Element_Size(elem, AxisMeasure*)
                                                  //  -> "Size"; fallback writes elem+0x74 (2 Dims)
static const u32 RVA_SCOPE_POP       = 0x18D650;  // Tgui_CallerScope_pop(scope)
static const u32 RVA_NAME_BUILD      = 0x189910;  // Tgui_NameArg(outBuf~0x50, cstr) -> name desc
static const u32 RVA_CONTAINER       = 0x192710;  // Tgui_Builder_BeginContainer(builder,name,style,children)
                                                  //  -> spawns element + PUSHES a group (children nest)
static const u32 RVA_POP_GROUP       = 0x18A6A0;  // Tgui_Builder_PopGroup(builder)
static const u32 RVA_TEXTARGS        = 0x751B50;  // Tgui_TextArgs_fromString(TextArgs* out, StlStr*)
static const u32 RVA_CLICK_POLL      = 0x712160;  // Tgui_Builder_WasClicked(builder,0)->bool
                                                  //  lazily makes the current element clickable
                                                  //  (interface 0x338186D1) and returns its click bit
// ---- shared element property setters (each: fn(elem, valuePtr)) ------------
static const u32 RVA_ELEM_FILLCOLOR  = 0x18C1D0;  // FillColor      (Color 16B)
static const u32 RVA_ELEM_JUSTIFY    = 0x2D2770;  // JustifyItems   (u8 enum) -> elem+265, flag 0x08 @+0x34
static const u32 RVA_ELEM_CROSSALIGN = 0x2D2930;  // CrossAlignItems(u8 enum) -> elem+266, flag 0x10 @+0x34
static const u32 RVA_ELEM_LAYOUTDIR  = 0x2D4170;  // LayoutDir (flex main axis, u8) -> elem+264, flag 0x04 @+0x34
static const u32 RVA_ELEM_WRAPITEMS  = 0x2D5440;  // WrapItems (u8) -> elem+269, flag 0x80 @+0x34
static const u32 RVA_ELEM_WIDTH      = 0x18C6B0;  // Width          (Dimension 8B)
static const u32 RVA_ELEM_HEIGHT     = 0x1D8010;  // WRONG: this is the "Height" C-string, not a setter (hangs) -- use RVA_ELEM_SIZE
static const u32 RVA_ELEM_PADDING    = 0x1D70D0;  // Padding        (SideMeasure 32B = 4 Dims)
static const u32 RVA_ELEM_FILLALPHA  = 0x18CBA0;  // FillAlpha      (float)
static const u32 RVA_ELEM_BGALPHA    = 0x18DA80;  // BackgroundAlpha(float)
static const u32 RVA_ELEM_CORNER     = 0x18D190;  // FillGraphicCornerRadius (float)
static const u32 RVA_ELEM_BGSCALE    = 0x18D780;  // BackgroundGraphicScale  (float)
static const u32 RVA_ELEM_MARGINR    = 0x18B830;  // MarginRight    (Dimension 8B)
static const u32 RVA_ELEM_TOP        = 0x18DD50;  // Top            (Dimension 8B)
static const u32 RVA_ELEM_BOTTOM     = 0x18BC80;  // Bottom         (Dimension 8B)
static const u32 RVA_ELEM_FILLGRAPHIC= 0x18C010;  // FillGraphic    (Graphic 0x60)
static const u32 RVA_ELEM_BGGRAPHIC  = 0x18C360;  // BackgroundGraphic (Graphic 0x60)
static const u32 RVA_BUILD_GRAPHIC   = 0x1D69F0;  // Tgui_MakeGraphic(out0x60, iconSpec{name@0},
                                                  //   rect16, scale, flags): resolves the image
                                                  //   name via sub_1414B3B70 into out+8.

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
typedef void* (*PFN_ElemBgColor)(void* elem, void* color16); // BackgroundColor = 4 floats
typedef void* (*PFN_ElemSize)(void* elem, void* axis16);     // Size = 2 Dimensions (16B)
typedef void* (*PFN_ScopePop)(void* scope);
typedef void* (*PFN_NameArg)(void* outBuf, const char* cstr);
typedef void* (*PFN_Container)(void* builder, void* name, void* style, void* children);
typedef void* (*PFN_PopGroup)(void* builder);
typedef void* (*PFN_TextArgs)(void* out, void* stlStr);
typedef char  (*PFN_ClickPoll)(void* builder, i64 a2);
typedef void* (*PFN_ElemProp)(void* elem, void* value);   // generic (elem, valuePtr)
typedef void* (*PFN_BuildGraphic)(void* out, void* spec, void* rect, double scale, char flags);

static u8*   gBase = nullptr;
static PFN_SpawnBlockWrap fn_SpawnBlockWrap = nullptr;
static PFN_SpawnElement   fn_SpawnElement   = nullptr;
static PFN_ElemSetText    fn_ElemSetText    = nullptr;
static PFN_Label          fn_Label          = nullptr;
static PFN_ScopeCtor      fn_ScopeCtor      = nullptr;
static PFN_ElemColor      fn_TextColor      = nullptr;
static PFN_ElemFontSize   fn_FontSize       = nullptr;
static PFN_ElemBgColor    fn_BgColor        = nullptr;
static PFN_ElemSize       fn_Size           = nullptr;
static PFN_ScopePop       fn_ScopePop       = nullptr;
static PFN_NameArg        fn_NameArg        = nullptr;
static PFN_Container      fn_Container      = nullptr;
static PFN_PopGroup       fn_PopGroup       = nullptr;
static PFN_TextArgs       fn_TextArgs       = nullptr;
static PFN_ClickPoll      fn_ClickPoll      = nullptr;
static PFN_ElemProp       fn_FillColor      = nullptr;
static PFN_ElemProp       fn_JustifyItems   = nullptr;
static PFN_ElemProp       fn_CrossAlign     = nullptr;
static PFN_ElemProp       fn_LayoutDir      = nullptr;
static PFN_ElemProp       fn_WrapItems      = nullptr;
static PFN_ElemProp       fn_Width          = nullptr;
static PFN_ElemProp       fn_Height         = nullptr;
static PFN_ElemProp       fn_Padding        = nullptr;
static PFN_ElemProp       fn_FillAlpha      = nullptr;
static PFN_ElemProp       fn_BgAlpha        = nullptr;
static PFN_ElemProp       fn_Corner         = nullptr;
static PFN_ElemProp       fn_BgScale        = nullptr;
static PFN_ElemProp       fn_MarginR        = nullptr;
static PFN_ElemProp       fn_Top            = nullptr;
static PFN_ElemProp       fn_Bottom         = nullptr;
static PFN_ElemProp       fn_FillGraphic    = nullptr;
static PFN_ElemProp       fn_BgGraphic      = nullptr;
static PFN_BuildGraphic   fn_BuildGraphic   = nullptr;

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
  fn_BgColor        = (PFN_ElemBgColor)rva(RVA_ELEM_BGCOLOR);
  fn_Size           = (PFN_ElemSize)rva(RVA_ELEM_SIZE);
  fn_ScopePop       = (PFN_ScopePop)rva(RVA_SCOPE_POP);
  fn_NameArg        = (PFN_NameArg)rva(RVA_NAME_BUILD);
  fn_Container      = (PFN_Container)rva(RVA_CONTAINER);
  fn_PopGroup       = (PFN_PopGroup)rva(RVA_POP_GROUP);
  fn_TextArgs       = (PFN_TextArgs)rva(RVA_TEXTARGS);
  fn_ClickPoll      = (PFN_ClickPoll)rva(RVA_CLICK_POLL);
  fn_FillColor      = (PFN_ElemProp)rva(RVA_ELEM_FILLCOLOR);
  fn_JustifyItems   = (PFN_ElemProp)rva(RVA_ELEM_JUSTIFY);
  fn_CrossAlign     = (PFN_ElemProp)rva(RVA_ELEM_CROSSALIGN);
  fn_LayoutDir      = (PFN_ElemProp)rva(RVA_ELEM_LAYOUTDIR);
  fn_WrapItems      = (PFN_ElemProp)rva(RVA_ELEM_WRAPITEMS);
  fn_Width          = (PFN_ElemProp)rva(RVA_ELEM_WIDTH);
  fn_Height         = nullptr;  // RVA_ELEM_HEIGHT is the "Height" STRING, not a setter -> calling it hangs; use setSize()
  fn_Padding        = (PFN_ElemProp)rva(RVA_ELEM_PADDING);
  fn_FillAlpha      = (PFN_ElemProp)rva(RVA_ELEM_FILLALPHA);
  fn_BgAlpha        = (PFN_ElemProp)rva(RVA_ELEM_BGALPHA);
  fn_Corner         = (PFN_ElemProp)rva(RVA_ELEM_CORNER);
  fn_BgScale        = (PFN_ElemProp)rva(RVA_ELEM_BGSCALE);
  fn_MarginR        = (PFN_ElemProp)rva(RVA_ELEM_MARGINR);
  fn_Top            = (PFN_ElemProp)rva(RVA_ELEM_TOP);
  fn_Bottom         = (PFN_ElemProp)rva(RVA_ELEM_BOTTOM);
  fn_FillGraphic    = (PFN_ElemProp)rva(RVA_ELEM_FILLGRAPHIC);
  fn_BgGraphic      = (PFN_ElemProp)rva(RVA_ELEM_BGGRAPHIC);
  fn_BuildGraphic   = (PFN_BuildGraphic)rva(RVA_BUILD_GRAPHIC);
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

// Set an element's BackgroundColor (4 floats RGBA, linear). Uses the game's own
// setter (ModifyProperty "BackgroundColor"; falls back to elem+0x240 + flag).
void setBgColor(void* elem, const float* rgba) {
  if (!fn_BgColor || !elem || !rgba) return;
  float col[4] = { rgba[0], rgba[1], rgba[2], rgba[3] };
  fn_BgColor(elem, col);
}

// Emit a native text label using the game's own Tgui_Label(scope, StlStr*).
// A _CallerScope must be constructed first (Tgui_CallerScope_ctor) so the
// builder's scope-depth stack is pushed; Tgui_Label pops it at the end.
// `idstr` gives the widget a stable reconcile identity (Builder source str).
void* drawLabel(Builder* b, const char* idstr, const char* utf8,
               const float* rgba, float fontPts) {
  if (!fn_Label || !fn_ScopeCtor || !b || !utf8) return nullptr;
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
  if (!elem) return nullptr;

  // Style the returned element (Element property setters; work post-pop).
  if (fontPts > 0.0f && fn_FontSize) {
    struct { float v; u8 unit; u8 pad[3]; } dim = { fontPts, 1 /*Points*/, {0,0,0} };
    fn_FontSize(elem, &dim);
  }
  if (rgba && fn_TextColor) {
    float col[4] = { rgba[0], rgba[1], rgba[2], rgba[3] };
    fn_TextColor(elem, col);
  }
  return elem;
}

// tgui Dimension on the wire: { f32 value; u8 unit; pad[3] } (8 bytes).
struct WireDim { float value; u8 unit; u8 pad[3]; };

// Begin a background CONTAINER: spawns a container element via the game's
// BeginContainer (0x192710, which PUSHES a group so widgets emitted afterwards
// nest INSIDE it), then sets its Size + BackgroundColor. Must be balanced with
// panelEnd(). Returns the container element (nullptr on failure).
void* panelBegin(Builder* b, const char* idstr,
                 float wVal, u8 wUnit, float hVal, u8 hUnit, const float* rgba) {
  if (!fn_Container || !fn_NameArg || !b) return nullptr;
  u8* bb = (u8*)b;
  *(const char**)(bb + OFF_BUILDER_SRCSTR) = idstr ? idstr : "panel";
  *(bb + OFF_BUILDER_SCOPEFLAG) = 1;

  u8 nameBuf[0x60];
  memset(nameBuf, 0, sizeof(nameBuf));
  fn_NameArg(nameBuf, idstr ? idstr : "panel");

  static u64 emptyChildren[16] = { 0 };          // count@+0x40 == 0
  void* elem = fn_Container(b, nameBuf, nullptr, &emptyChildren);
  if (elem) {
    // A container with the DEFAULT (unset) flex config CANNOT lay out 2+ children
    // -> the layout pass loops forever (runtime-confirmed: sized container + 1
    // child renders fine, + 2 children froze the game). Give every container an
    // explicit, valid LayoutDir + WrapItems so multi-child layout converges.
    // (Downstream .row()/.col() overrides LayoutDir later via setLayout.)
    // Flex direction enum (confirmed from game style builder sub_1402D5190,
    // which sets LayoutDir = 2*isLandscape): 0=Column(top->bottom), 2=Row,
    // 1=ColumnReverse, 3=RowReverse. 0 is used all over the game (NOT a freeze
    // -- the earlier freeze was a background-graphic change, not this). Default
    // to Column so children stack top-to-bottom in add() order.
    if (fn_LayoutDir) { u8 d = 0; fn_LayoutDir(elem, &d); }   // 0 = Column
    if (fn_WrapItems) { u8 w0 = 0; fn_WrapItems(elem, &w0); } // 0 = no-wrap
    if (fn_Size) {
      struct { WireDim w, h; } sz;
      memset(&sz, 0, sizeof(sz));
      sz.w.value = wVal; sz.w.unit = wUnit;
      sz.h.value = hVal; sz.h.unit = hUnit;
      fn_Size(elem, &sz);
    }
    if (rgba && fn_BgColor) {
      // A plain sized container + BackgroundColor renders a correct solid box
      // (confirmed). Do NOT touch the graphic here.
      float col[4] = { rgba[0], rgba[1], rgba[2], rgba[3] };
      fn_BgColor(elem, col);
    } else {
      u8 emptyGraphic[0x60]; memset(emptyGraphic, 0, sizeof(emptyGraphic));
      if (fn_BgGraphic)   fn_BgGraphic(elem, emptyGraphic);
      if (fn_FillGraphic) fn_FillGraphic(elem, emptyGraphic);
    }
  }
  return elem;
}

// End the container opened by panelBegin (pops the group).
void panelEnd(Builder* b) {
  if (fn_PopGroup && b) fn_PopGroup(b);
}

void* drawImage(Builder* b, const char* idstr, const char* imageName,
                float wVal, u8 wUnit, float hVal, u8 hUnit) {
  if (!fn_ScopeCtor || !fn_SpawnElement || !fn_ScopePop ||
      !fn_BuildGraphic || !fn_FillGraphic || !b || !imageName) return nullptr;
  u8* bb = (u8*)b;
  *(const char**)(bb + OFF_BUILDER_SRCSTR) = idstr ? idstr : imageName;
  *(bb + OFF_BUILDER_SCOPEFLAG) = 1;

  u8 scope[64];
  memset(scope, 0, sizeof(scope));
  fn_ScopeCtor(scope, b);
  void* elem = spawnElement(b);
  if (elem) {
    u8 spec[0x60]; memset(spec, 0, sizeof(spec));
    *(const char**)spec = imageName;              // icon spec: image name @ +0
    float rect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };   // full UV (best-effort)
    u8 graphic[0x60]; memset(graphic, 0, sizeof(graphic));
    fn_BuildGraphic(graphic, spec, rect, 1.0, 0); // resolves name -> graphic
    fn_FillGraphic(elem, graphic);
    if (fn_Size) {
      struct { WireDim w, h; } sz;
      memset(&sz, 0, sizeof(sz));
      sz.w.value = wVal; sz.w.unit = wUnit;
      sz.h.value = hVal; sz.h.unit = hUnit;
      fn_Size(elem, &sz);
    }
  }
  fn_ScopePop(scope);
  return elem;
}

// ---- shared element property setters (operate on a live element handle) -----
void setFillColor(void* elem, const float* rgba) {
  if (!fn_FillColor || !elem || !rgba) return;
  float c[4] = { rgba[0], rgba[1], rgba[2], rgba[3] };
  fn_FillColor(elem, c);
}
void setFillAlpha(void* elem, float a) {
  if (!fn_FillAlpha || !elem) return;
  fn_FillAlpha(elem, &a);
}
void setBgAlpha(void* elem, float a) {
  if (!fn_BgAlpha || !elem) return;
  fn_BgAlpha(elem, &a);
}
void setCornerRadius(void* elem, float r) {
  if (!fn_Corner || !elem) return;
  fn_Corner(elem, &r);
}
void setBgScale(void* elem, float s) {
  if (!fn_BgScale || !elem) return;
  fn_BgScale(elem, &s);
}
void setMarginRight(void* elem, float v, u8 unit) {
  if (!fn_MarginR || !elem) return;
  WireDim d = { v, unit, {0,0,0} }; fn_MarginR(elem, &d);
}
void setTop(void* elem, float v, u8 unit) {
  if (!fn_Top || !elem) return;
  WireDim d = { v, unit, {0,0,0} }; fn_Top(elem, &d);
}
void setBottom(void* elem, float v, u8 unit) {
  if (!fn_Bottom || !elem) return;
  WireDim d = { v, unit, {0,0,0} }; fn_Bottom(elem, &d);
}

void setLayout(void* elem, int layoutDir, int justify, int align) {
  if (!elem) return;
  // LayoutDir (FlexDirection) now via the game's own setter (0x2D4170): value
  // @elem+264, flag 0x04 @+0x34, through Tgui_Builder_SetBoolProp. A container
  // with the DEFAULT (unset) LayoutDir cannot lay out 2+ children -> the layout
  // loops forever (runtime-confirmed: sized container + 1 child OK, + 2 children
  // froze). Setting a valid LayoutDir fixes multi-child containers.
  if (layoutDir >= 0 && fn_LayoutDir)  { u8 v = (u8)layoutDir; fn_LayoutDir(elem, &v); }
  if (justify   >= 0 && fn_JustifyItems) { u8 v = (u8)justify; fn_JustifyItems(elem, &v); }
  if (align     >= 0 && fn_CrossAlign)   { u8 v = (u8)align;   fn_CrossAlign(elem, &v); }
}
void setWidth(void* elem, float v, u8 unit) {
  if (!fn_Width || !elem) return;
  WireDim d = { v, unit, {0,0,0} };
  fn_Width(elem, &d);
}
void setHeight(void* elem, float v, u8 unit) {
  if (!fn_Height || !elem) return;
  WireDim d = { v, unit, {0,0,0} };
  fn_Height(elem, &d);
}

void setSize(void* elem, float wV, u8 wU, float hV, u8 hU) {
  if (!fn_Size || !elem) return;
  struct { WireDim w, h; } sz = { { wV, wU, {0,0,0} }, { hV, hU, {0,0,0} } };
  fn_Size(elem, &sz);
}
// Padding = a SideMeasure of 4 Dimensions (left, top, right, bottom), 32 bytes.
void setPadding(void* elem, float l, u8 lu, float t, u8 tu,
                float r, u8 ru, float bt, u8 bu) {
  if (!fn_Padding || !elem) return;
  struct { WireDim l, t, r, b; } sm = {
    { l, lu, {0,0,0} }, { t, tu, {0,0,0} },
    { r, ru, {0,0,0} }, { bt, bu, {0,0,0} } };
  fn_Padding(elem, &sm);
}

}} // namespace sg::engine
