// Native settings for Returnal, included by dlss_mfg_sm86.cpp.
//
// Returnal is a modular Unreal build: its engine DLLs export the UObject, UMG
// and FText functions by name, so the game's own setting widgets can be created
// and driven directly (technique as in Returnal Native Gyro, MIT). The Graphics
// tab's "DLSS Frame Gen" row (Off / On) gets a value widget of the game's own
// spinner class with Off / 2x / 3x / 4x / Auto: the choice is written as Mode=0
// (the in-game setting) with GeneratedFrames 1-3 or Auto (NVIDIA Dynamic MFG),
// and the game's own
// On / Off spinner, kept off screen, is set and reported to the tab as if the
// player had changed it. Like the game's settings, a change applies at once.
// The Reflex row is kept editable with frame generation on (see Reflex). Any
// missing export, property or widget leaves the menu as shipped. [Menu]
// Enabled=0 turns it off.

namespace menu {

typedef void* Obj;
typedef uint64_t Name;
struct Str { wchar_t* data; int count, capacity; };
struct Text { alignas(8) unsigned char bytes[24]; };

static struct {
    void (*makeName)(Name*, const wchar_t*, int);
    Obj (*findObject)(Obj, Obj, const wchar_t*, bool);
    Obj (*property)(Obj, Name);
    int (*offset)(Obj);
    Obj (*function)(Obj, Name, int);
    void (*event)(Obj, Obj, void*);
    void (*copyValue)(Obj, void*, const void*);
    Obj (*createWidget)(Obj, Obj, Name);
    Obj (*parent)(Obj);
    int (*childIndex)(Obj, Obj);
    int (*count)(Obj);
    Obj (*child)(Obj, int);
    bool (*removeChild)(Obj, Obj);
    Obj (*addChild)(Obj, Obj);
    Obj (*setContent)(Obj, Obj);
    unsigned char (*getVisibility)(Obj);
    void (*setVisibility)(Obj, unsigned char);
    Text* (*textFrom)(Text*, const Str&);
    Text* (*textAssign)(Text*, const Text&);
    void (*textDestroy)(Text*);
    void (*arrayClear)(void*, Obj);
    int (*arrayAdd)(void*, Obj, const void*);
    Obj objects;
} a;

template <class T> static T Read(Obj object, size_t offset) { T value; memcpy(&value, (char*)object + offset, sizeof(T)); return value; }
template <class T> static void Write(Obj object, size_t offset, T value) { memcpy((char*)object + offset, &value, sizeof(T)); }

static Name MakeName(const wchar_t* text, int add) { Name name = 0; a.makeName(&name, text, add); return name; }
static Obj ClassOf(Obj object) { return Read<Obj>(object, 0x10); }
static Obj Property(Obj object, const wchar_t* name) { return a.property(ClassOf(object), MakeName(name, 0)); }
// Byte offset of a reflected property, or -1.
static int Offset(Obj object, const wchar_t* name) {
    Obj property = Property(object, name);
    int at = property ? a.offset(property) : -1;
    return at >= 0 && at < 16384 ? at : -1;
}
static Obj Field(Obj object, const wchar_t* name) {
    int at = object ? Offset(object, name) : -1;
    return at < 0 ? NULL : Read<Obj>(object, at);
}
static bool Call(Obj object, const wchar_t* method, const void* parameters, size_t size) {
    Obj function = a.function(ClassOf(object), MakeName(method, 0), 1);
    if (!function || size > 256) return false;
    alignas(16) unsigned char buffer[256] = {};
    if (parameters) memcpy(buffer, parameters, size);
    a.event(object, function, buffer);
    return true;
}
static void Copy(Obj target, Obj source, const wchar_t* name) {
    Obj to = Property(target, name), from = Property(source, name);
    if (to && from) a.copyValue(to, (char*)target + a.offset(to), (char*)source + a.offset(from));
}
// Widgets live in the engine's object array; a closed menu's widgets are
// collected, so every tracked pointer is checked against its slot first.
struct Tracked { Obj object; int index, serial; };
static char* Item(int index) {
    int count = Read<int>(a.objects, 0x24);
    if (index < 0 || index >= count) return NULL;
    char** chunks = Read<char**>(a.objects, 0x10);
    return chunks[index / 65536] ? chunks[index / 65536] + (index % 65536) * 24 : NULL;
}
static Tracked Track(Obj object) {
    int index = Read<int>(object, 12);
    char* slot = Item(index);
    return slot && Read<Obj>(slot, 0) == object ? Tracked{object, index, Read<int>(slot, 16)} : Tracked{};
}
static bool Valid(const Tracked& tracked) {
    char* slot = tracked.object ? Item(tracked.index) : NULL;
    return slot && Read<Obj>(slot, 0) == tracked.object && Read<int>(slot, 16) == tracked.serial
        && !(Read<unsigned>(slot, 8) & 0x30000000) && !(Read<unsigned>(tracked.object, 8) & 0x30);
}

static const wchar_t* const kValues[] = {L"2x", L"3x", L"4x", L"Auto"};
static const wchar_t* const kLabels[] = {L"Off", L"2x", L"3x", L"4x", L"Auto"};
static const int kAuto = 3;
static Obj g_tabClass, g_spinnerClass;
static Tracked g_pending, g_tab;
static Obj g_frameGen, g_gameSpinner, g_spinner; // the game's row and On / Off spinner, ours
static int g_index = -1, g_selected = -1;
static volatile LONG g_failed = 0;
static bool g_offSkipped; // Reflex's Off is skipped (frame generation on)

static void Fail(const char* reason) {
    if (!InterlockedExchange(&g_failed, 1)) Log("Menu disabled: %s", reason);
}

// The current configuration as a menu index: Auto for Mode 1, else the count.
static int CurrentIndex() {
    if (g_mode == 1) return kAuto;
    LONG frames = g_requested;
    return frames >= 1 && frames <= 3 ? frames - 1 : kAuto;
}

// The in-game choice is the setting: Mode 0 with it as GeneratedFrames.
static void Apply(int index) {
    WCHAR count[2] = {(WCHAR)(L'1' + index), 0};
    WritePrivateProfileStringW(L"FrameGeneration", L"Mode", L"0", g_config);
    WritePrivateProfileStringW(L"FrameGeneration", L"GeneratedFrames", index == kAuto ? L"Auto" : count, g_config);
    Log("Menu Multi Frame Generation=%ls", kValues[index]);
    // Now rather than at the next reload: menus read the value straight back.
    ApplyConfiguration(PrimaryHz(), "Menu");
}

// The game's On / Off spinner's index (0 = Off).
static int GameFrameGen() { return Read<int>(g_gameSpinner, g_selected); }
static int Shown() { return GameFrameGen() > 0 ? 1 + CurrentIndex() : 0; }
// Sets the game's spinner and runs the tab's handler for it, as a player's
// change would.
static void SetGameFrameGen(Obj tab, bool on) {
    int index = on ? 1 : 0;
    if (GameFrameGen() == index) return;
    Call(g_gameSpinner, L"SetSelectedIndexWithoutEnabledCheck", &index, sizeof(index));
    if (!Call(tab, L"BndEvt__ValueDLSSFrameGen_K2Node_ComponentBoundEvent_20_OnValueChanged__DelegateSignature", NULL, 0))
        Log("Menu: the tab's frame generation handler was not found");
}

// The game's Frame Generation row shows our spinner in its value slot.
static void Attach(Obj tab) {
    Obj frameGen = Field(tab, L"SettingDLSSFrameGen"), frameGenValue = Field(tab, L"ValueDLSSFrameGen");
    Obj content = frameGen ? Field(frameGen, L"SettingValue") : NULL;
    int gameSelected = frameGenValue ? Offset(frameGenValue, L"SelectedIndex") : -1;
    if (!content || gameSelected < 0) { Fail("Frame Generation row not found"); return; }
    Obj spinner = a.createWidget(tab, g_spinnerClass, MakeName(L"DlssMfgSm86Value", 1));
    int values = spinner ? Offset(spinner, L"Values") : -1, selected = spinner ? Offset(spinner, L"SelectedIndex") : -1;
    if (values < 0 || selected < 0) { Fail("setting widgets not created"); return; }
    for (const wchar_t* key : {L"WidgetStyle", L"Style", L"HoverEnabled", L"ToggleOnX", L"LoadingStyleSet", L"DefaultStyleSet", L"LanguageStyleSets"})
        Copy(spinner, frameGenValue, key);
    Obj array = Property(spinner, L"Values");
    a.arrayClear((char*)spinner + values, array);
    for (const wchar_t* label : kLabels) {
        Str text = {(wchar_t*)label, (int)wcslen(label) + 1, (int)wcslen(label) + 1};
        Text made;
        a.textFrom(&made, text);
        a.arrayAdd((char*)spinner + values, array, &made);
        a.textDestroy(&made);
    }
    g_tab = Track(tab); g_frameGen = frameGen; g_gameSpinner = frameGenValue; g_spinner = spinner; g_selected = selected;
    int index = Shown();
    Write<int>(spinner, selected, index);
    int defaults = Offset(spinner, L"DefaultValue");
    if (defaults >= 0) Write<int>(spinner, defaults, 1 + kAuto);
    if (!a.setContent(content, spinner)) { g_tab = {}; Fail("value slot"); return; }
    Call(spinner, L"SetSelectedIndexWithoutEnabledCheck", &index, sizeof(index));
    g_index = index;
    g_offSkipped = false;
    Log("Menu: DLSS Frame Gen extended to Off/2x/3x/4x/Auto, value=%ls", kLabels[index]);
}

// The game locks its Reflex row while Frame Generation is on. Then it stays
// editable with On / Enabled + Boost (Off is skipped: frame generation needs
// Reflex), and the shown choice reaches Streamline through the proxy's Reflex
// hook (the game itself asks for plain low latency with frame generation).
static void Reflex(Obj tab) {
    Obj frameGenValue = Field(tab, L"ValueDLSSFrameGen"), row = Field(tab, L"SettingReflex"), reflex = Field(tab, L"ValueReflex");
    int frameGenAt = frameGenValue ? Offset(frameGenValue, L"SelectedIndex") : -1;
    int selectedAt = reflex ? Offset(reflex, L"SelectedIndex") : -1, modesAt = Offset(tab, L"ReflexArray");
    if (!row || frameGenAt < 0 || selectedAt < 0 || modesAt < 0) return;
    struct Modes { unsigned char* data; int count, capacity; } modes = Read<Modes>(tab, modesAt); // EReflexMode per index
    int off = -1, on = -1;
    for (int i = 0; modes.data && i < modes.count && i < 8; ++i) {
        if (modes.data[i] == 0) off = i;
        if (modes.data[i] == 1) on = i;
    }
    int selected = Read<int>(reflex, selectedAt);
    if (Read<int>(frameGenValue, frameGenAt) > 0) {
        int can = Offset(row, L"CanChangeValue");
        if (can >= 0) Write<bool>(row, can, true);
        bool enabled = true;
        Call(row, L"SetIsEnabled", &enabled, sizeof(enabled));
        Call(reflex, L"SetIsEnabled", &enabled, sizeof(enabled));
        if (!g_offSkipped && off >= 0) { Call(reflex, L"AddIndexToSkip", &off, sizeof(off)); g_offSkipped = true; }
        if (selected == off && on >= 0) { Call(reflex, L"SetSelectedIndexWithoutEnabledCheck", &on, sizeof(on)); selected = on; }
    } else if (g_offSkipped) {
        int skipAt = Offset(reflex, L"IndexesToSkip");
        if (skipAt >= 0) Write<int>(reflex, skipAt + 8, 0); // the skipped list is ours alone
        g_offSkipped = false;
    }
    if (selected >= 0 && selected < modes.count) SetReflexBoost(modes.data[selected] == 2);
}

static void Tick() {
    if (g_failed) return;
    if (g_pending.object) {
        Tracked pending = g_pending; g_pending = {};
        if (Valid(pending) && !(g_tab.object && Valid(g_tab))) Attach(pending.object);
    }
    if (!g_tab.object) return;
    if (!Valid(g_tab)) { g_tab = {}; g_spinner = g_gameSpinner = g_frameGen = NULL; return; }
    int index = Read<int>(g_spinner, g_selected);
    if (index != g_index && index >= 0 && index <= 1 + kAuto) {
        // The player's choice: the count, then the game's On / Off.
        g_index = index;
        if (index) Apply(index - 1); else Log("Menu Frame generation=Off");
        SetGameFrameGen(g_tab.object, index > 0);
    } else {
        // The game's own changes (presets, defaults, availability) show here.
        int shown = Shown();
        if (shown != g_index) { g_index = shown; Call(g_spinner, L"SetSelectedIndexWithoutEnabledCheck", &shown, sizeof(shown)); }
    }
    static ULONGLONG nextReflex;
    ULONGLONG now = GetTickCount64();
    if (now >= nextReflex) { nextReflex = now + 200; Reflex(g_tab.object); }
}

static void (*o_draw)(Obj, Obj, Obj);
static void (*o_construct)(Obj);
static void H_draw(Obj viewport, Obj target, Obj canvas) { Tick(); o_draw(viewport, target, canvas); }
static void H_construct(Obj widget) {
    o_construct(widget);
    if (!g_failed && ClassOf(widget) == (g_tabClass ? g_tabClass :
        (g_tabClass = a.findObject(NULL, NULL, L"/Game/UI/SystemMenu/WBP_GraphicsSettings.WBP_GraphicsSettings_C", false)))) {
        if (!g_spinnerClass) g_spinnerClass = a.findObject(NULL, NULL, L"/Game/UI/SystemMenu/WBP_SettingSpinner.WBP_SettingSpinner_C", false);
        if (g_spinnerClass) g_pending = Track(widget);
        else Fail("setting templates not loaded");
    }
}

// Waits for the game's modules (loaded by its module manager after the proxy),
// then hooks widget construction and the viewport draw (the game thread).
static DWORD WINAPI Install(void*) {
    HMODULE core = NULL, objects = NULL, umg = NULL, engine = NULL;
    for (int i = 0; i < 12000 && !(core && objects && umg && engine); ++i, Sleep(10)) {
        core = GetModuleHandleW(L"Returnal-Core-Win64-Shipping.dll");
        objects = GetModuleHandleW(L"Returnal-CoreUObject-Win64-Shipping.dll");
        umg = GetModuleHandleW(L"Returnal-UMG-Win64-Shipping.dll");
        engine = GetModuleHandleW(L"Returnal-Engine-Win64-Shipping.dll");
    }
    if (!umg || !engine) { Log("Menu: game modules not loaded"); return 1; }
    bool ok = true;
#define MENU_LOAD(module, name, target) ok &= (*(void**)&target = (void*)GetProcAddress(module, name)) != NULL
    MENU_LOAD(core, "??0FName@@QEAA@PEB_WW4EFindName@@@Z", a.makeName);
    MENU_LOAD(objects, "?StaticFindObject@@YAPEAVUObject@@PEAVUClass@@PEAV1@PEB_W_N@Z", a.findObject);
    MENU_LOAD(objects, "?FindPropertyByName@UStruct@@QEBAPEAVFProperty@@VFName@@@Z", a.property);
    MENU_LOAD(objects, "?GetOffset_ForInternal@FProperty@@QEBAHXZ", a.offset);
    MENU_LOAD(objects, "?FindFunctionByName@UClass@@QEBAPEAVUFunction@@VFName@@W4Type@EIncludeSuperFlag@@@Z", a.function);
    MENU_LOAD(objects, "?ProcessEvent@UObject@@UEAAXPEAVUFunction@@PEAX@Z", a.event);
    MENU_LOAD(objects, "?CopyCompleteValue@FProperty@@QEBAXPEAXPEBX@Z", a.copyValue);
    MENU_LOAD(objects, "?GUObjectArray@@3VFUObjectArray@@A", a.objects);
    MENU_LOAD(umg, "?CreateWidgetInstance@UUserWidget@@SAPEAV1@AEAVUWidget@@V?$TSubclassOf@VUUserWidget@@@@VFName@@@Z", a.createWidget);
    MENU_LOAD(umg, "?GetParent@UWidget@@QEBAPEAVUPanelWidget@@XZ", a.parent);
    MENU_LOAD(umg, "?GetChildIndex@UPanelWidget@@QEBAHPEBVUWidget@@@Z", a.childIndex);
    MENU_LOAD(umg, "?GetChildrenCount@UPanelWidget@@QEBAHXZ", a.count);
    MENU_LOAD(umg, "?GetChildAt@UPanelWidget@@QEBAPEAVUWidget@@H@Z", a.child);
    MENU_LOAD(umg, "?RemoveChild@UPanelWidget@@QEAA_NPEAVUWidget@@@Z", a.removeChild);
    MENU_LOAD(umg, "?AddChild@UPanelWidget@@QEAAPEAVUPanelSlot@@PEAVUWidget@@@Z", a.addChild);
    MENU_LOAD(umg, "?SetContent@UContentWidget@@QEAAPEAVUPanelSlot@@PEAVUWidget@@@Z", a.setContent);
    MENU_LOAD(umg, "?GetVisibility@UWidget@@QEBA?AW4ESlateVisibility@@XZ", a.getVisibility);
    MENU_LOAD(umg, "?SetVisibility@UWidget@@UEAAXW4ESlateVisibility@@@Z", a.setVisibility);
    MENU_LOAD(core, "?FromString@FText@@SA?AV1@AEBVFString@@@Z", a.textFrom);
    MENU_LOAD(core, "??4FText@@QEAAAEAV0@AEBV0@@Z", a.textAssign);
    MENU_LOAD(core, "??1FText@@QEAA@XZ", a.textDestroy);
    MENU_LOAD(engine, "?GenericArray_Clear@UKismetArrayLibrary@@SAXPEAXPEBVFArrayProperty@@@Z", a.arrayClear);
    MENU_LOAD(engine, "?GenericArray_Add@UKismetArrayLibrary@@SAHPEAXPEBVFArrayProperty@@PEBX@Z", a.arrayAdd);
    void* draw = NULL; void* construct = NULL;
    MENU_LOAD(engine, "?Draw@UGameViewportClient@@UEAAXPEAVFViewport@@PEAVFCanvas@@@Z", draw);
    MENU_LOAD(umg, "?NativeConstruct@UUserWidget@@MEAAXXZ", construct);
#undef MENU_LOAD
    if (!ok) { Log("Menu: an engine export is missing; menu left as shipped"); return 2; }
    MH_STATUS status = MH_Initialize();
    if (status == MH_ERROR_ALREADY_INITIALIZED) status = MH_OK;
    if (status == MH_OK) status = MH_CreateHook(construct, (void*)H_construct, (void**)&o_construct);
    if (status == MH_OK) status = MH_CreateHook(draw, (void*)H_draw, (void**)&o_draw);
    if (status == MH_OK) status = MH_QueueEnableHook(construct);
    if (status == MH_OK) status = MH_QueueEnableHook(draw);
    if (status == MH_OK) status = MH_ApplyQueued();
    Log("Menu hooks %s", MH_StatusToString(status));
    return status == MH_OK ? 0 : 3;
}

static void Start() {
    if (!GetModuleHandleW(L"Returnal-Core-Win64-Shipping.dll")) return;
    if (!Setting(g_config, L"Menu", L"Enabled", 1, 0, 1)) return;
    HANDLE thread = CreateThread(NULL, 0, Install, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
}

} // namespace menu
