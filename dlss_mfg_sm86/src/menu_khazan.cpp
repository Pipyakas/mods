// Native settings options for The First Berserker: Khazan (UE 4.27), included
// by dlss_mfg_sm86.cpp.
//
// Khazan's settings are Lyra-style GameSetting objects. Its "NVIDIA DLSS Frame
// Generation" setting (DevName DLSSFrameGeneration, a discrete Off/On value)
// becomes Off / 2x / 3x / 4x / Auto: that one object gets its own copy of its
// class's virtual table with the four discrete-value functions replaced.
// Off and the four counts still drive the game's own On/Off; the count is a
// pending change until "Apply Changes", then goes to the INI like the other
// games' rows. Slots come from the engine's exec thunks
// of GetDiscreteOptionIndex / GetDiscreteOptionDefaultIndex / GetDiscreteOptions
// (declared in that order, right after SetDiscreteOptionByIndex).
//
// Its "NVIDIA Reflex" setting (UseReflex: Off / On / Boost) is locked by an
// edit condition while frame generation is on. Its conditions are dropped and,
// the same way, it then offers On / Boost; the game applies Boost itself.

namespace menu_khazan {

typedef void SetFn(void* self, int index);
typedef int IndexFn(void* self);
typedef ue::Array<ue::Text>* OptionsFn(void* self, ue::Array<ue::Text>* result);

// One setting object's original discrete-value functions.
struct Discrete { SetFn* set; IndexFn* index; IndexFn* defaults; OptionsFn* options; ue::Ref object; };
static Discrete g_frameGen, g_reflex;
static wchar_t g_off[64] = L"Off";
static bool g_failed, g_reflexFailed;

static bool FrameGenOn() {
    return g_frameGen.index && ue::Alive(g_frameGen.object) && g_frameGen.index(g_frameGen.object.object) > 0;
}
// The Reflex row reads its options and value again when the setting reports a
// change: setting the current value does that.
static void RefreshReflex() {
    if (g_reflex.set && ue::Alive(g_reflex.object)) g_reflex.set(g_reflex.object.object, g_reflex.index(g_reflex.object.object));
}

// The count chosen in the menu but not applied yet (-1: none). Like the game's
// own values it is committed by "Apply Changes" and dropped by cancelling.
static int g_pending = -1;
typedef bool ChangedFn(void* self);
typedef void ActionFn(void* self);
static ChangedFn* o_changed;
static ActionFn* o_store, *o_reset, *o_restore;

static int H_index(void* self) {
    int index = g_frameGen.index(self);
    return index <= 0 ? index : 1 + (g_pending >= 0 ? g_pending : menu::CurrentIndex());
}
static int H_default(void* self) {
    int index = g_frameGen.defaults(self);
    return index <= 0 ? index : 1 + menu::kAuto;
}
// Switching frame generation on (even when it already is) makes the game set
// Reflex to On; a Boost choice is kept.
static void H_set(void* self, int index) {
    bool wasOn = g_frameGen.index(self) > 0;
    void* reflex = g_reflex.set && ue::Alive(g_reflex.object) ? g_reflex.object.object : NULL;
    int reflexBefore = reflex ? g_reflex.index(reflex) : -1;
    g_pending = index > 0 && index <= 1 + menu::kAuto && index - 1 != menu::CurrentIndex() ? index - 1 : -1;
    g_frameGen.set(self, index > 0 ? 1 : 0); // the screen asks H_changed whether this is a change
    if (reflex && reflexBefore == 2 && g_reflex.index(reflex) != 2) g_reflex.set(reflex, 2);
    else if (wasOn != (index > 0)) RefreshReflex();
}
// "Changed since applied": a pending count is a change ("Apply Changes").
static bool H_changed(void* self) { return o_changed(self) || g_pending >= 0; }
// Applying stores the current values as the applied ones: the count too.
static void H_store(void* self) {
    if (g_pending >= 0) { int index = g_pending; g_pending = -1; menu::Apply(index); }
    o_store(self);
}
static void H_reset(void* self) { g_pending = -1; o_reset(self); }
static void H_restore(void* self) { g_pending = -1; o_restore(self); }
// A fresh engine-owned array each time: the caller destroys it.
static ue::Array<ue::Text>* H_options(void* self, ue::Array<ue::Text>* result) {
    int bytes = 0;
    auto texts = (ue::Text*)ue::EngineBuffer(5 * sizeof(ue::Text), &bytes);
    if (!texts) return g_frameGen.options(self, result);
    texts[0] = ue::MakeText(g_off);
    for (int i = 0; i <= menu::kAuto; ++i) texts[i + 1] = ue::MakeText(menu::kValues[i]);
    *result = ue::Array<ue::Text>{texts, 5, bytes / (int)sizeof(ue::Text)};
    return result;
}

// Reflex with frame generation on: index 0 / 1 are On / Boost (the game's 1 / 2).
static int R_index(void* self) {
    int index = g_reflex.index(self);
    return FrameGenOn() ? (index > 1 ? index - 1 : 0) : index;
}
static int R_default(void* self) {
    int index = g_reflex.defaults(self);
    return FrameGenOn() ? (index > 1 ? index - 1 : 0) : index;
}
static void R_set(void* self, int index) {
    g_reflex.set(self, FrameGenOn() ? index + 1 : index);
    SetReflexBoost(g_reflex.index(self) == 2);
}
// Off is moved out of the list (its text keeps one engine reference).
static ue::Array<ue::Text>* R_options(void* self, ue::Array<ue::Text>* result) {
    g_reflex.options(self, result);
    if (FrameGenOn() && result->count == 3) {
        memmove(result->data, result->data + 1, 2 * sizeof(ue::Text));
        result->count = 2;
    }
    return result;
}

static bool Executable(void* code) {
    char* start; size_t length;
    for (int s = 0; ue::Section(GetModuleHandleW(NULL), NULL, IMAGE_SCN_MEM_EXECUTE, &start, &length, s); ++s)
        if ((char*)code >= start && (char*)code < start + length) return true;
    return false;
}

// Gives `setting` its own virtual table with the four functions replaced.
static bool Install(void* setting, Discrete& original, SetFn* set, IndexFn* index, IndexFn* defaults, OptionsFn* options) {
    int at = ue::VirtualSlot(ue::Function(setting, "GetDiscreteOptionIndex"));
    int defaultsAt = ue::VirtualSlot(ue::Function(setting, "GetDiscreteOptionDefaultIndex"));
    int optionsAt = ue::VirtualSlot(ue::Function(setting, "GetDiscreteOptions"));
    if (at < 1 || defaultsAt != at + 1 || optionsAt != at + 2) { Log("Menu: discrete setting slots %d/%d/%d", at, defaultsAt, optionsAt); return false; }
    void** table = ue::Read<void**>(setting, 0);
    int size = optionsAt + 1;
    while (size < 1024 && Executable(table[size])) size++;
    for (int slot = at - 6; slot <= optionsAt; ++slot) // the value functions before Set too
        if (!Executable(table[slot])) { Log("Menu: slot %d is not code", slot); return false; }
    void** copy = (void**)VirtualAlloc(NULL, (size + 1) * sizeof(void*), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!copy) return false;
    memcpy(copy, table - 1, (size + 1) * sizeof(void*)); // RTTI locator, then the slots
    void** mine = copy + 1;
    original.set = (SetFn*)table[at - 1]; original.index = (IndexFn*)table[at];
    original.defaults = (IndexFn*)table[defaultsAt]; original.options = (OptionsFn*)table[optionsAt];
    mine[at - 1] = (void*)set; mine[at] = (void*)index;
    mine[defaultsAt] = (void*)defaults; mine[optionsAt] = (void*)options;
    *(void***)setting = mine;
    original.object = ue::Track(setting);
    return true;
}

static void InstallFrameGen(void* setting) {
    // The "Off" label in the game's language, from its own option list.
    void** table = ue::Read<void**>(setting, 0);
    int optionsAt = ue::VirtualSlot(ue::Function(setting, "GetDiscreteOptions"));
    if (optionsAt > 0) {
        ue::Array<ue::Text> original{};
        ((OptionsFn*)table[optionsAt])(setting, &original);
        if (original.count > 0) {
            static void* library;
            if (!library) library = ue::FindDefault("KismetTextLibrary");
            ue::Call convert(library, "Conv_TextToString");
            convert.Arg("InText", original.data[0]).Run();
            auto off = convert.Result<ue::String>("ReturnValue");
            if (off.data && off.count > 1 && off.count < 64) wcscpy_s(g_off, off.data);
        }
    }
    if (!Install(setting, g_frameGen, H_set, H_index, H_default, H_options)) { g_failed = true; return; }
    // GameSettingValue's StoreInitial / ResetToDefault / RestoreToInitial, then
    // this build's "changed since applied" and "changed from default", sit
    // right before SetDiscreteOptionByIndex.
    int set = ue::VirtualSlot(ue::Function(setting, "GetDiscreteOptionIndex")) - 1;
    void** mine = ue::Read<void**>(setting, 0);
    o_store = (ActionFn*)mine[set - 5]; o_reset = (ActionFn*)mine[set - 4];
    o_restore = (ActionFn*)mine[set - 3]; o_changed = (ChangedFn*)mine[set - 2];
    mine[set - 5] = (void*)H_store; mine[set - 4] = (void*)H_reset;
    mine[set - 3] = (void*)H_restore; mine[set - 2] = (void*)H_changed;
    Log("Menu: DLSSFrameGeneration extended to Off/2x/3x/4x/Auto, applied with Apply Changes");
}

// The setting's edit conditions (GameSetting::EditConditions, shared
// references to polymorphic objects) at a fixed offset in this build; checked
// before use. Reflex's lock it to On while frame generation is on.
static const int kEditConditions = 0x110;
static bool EditConditions(void* setting, ue::Array<char[16]>* list) {
    *list = ue::Read<ue::Array<char[16]>>(setting, kEditConditions);
    if (list->count < 1 || list->count > 4 || list->capacity < list->count || !ue::Readable(list->data)) return false;
    char* image = (char*)GetModuleHandleW(NULL);
    auto nt = (IMAGE_NT_HEADERS*)(image + ((IMAGE_DOS_HEADER*)image)->e_lfanew);
    for (int i = 0; i < list->count; ++i) {
        void* condition = ue::Read<void*>(list->data[i], 0);
        char* table = ue::Readable(condition) ? ue::Read<char*>(condition, 0) : NULL;
        if (table < image || table >= image + nt->OptionalHeader.SizeOfImage) return false;
    }
    return true;
}

static void InstallReflex(void* setting) {
    ue::Array<char[16]> conditions;
    if (!EditConditions(setting, &conditions)) { g_reflexFailed = true; Log("Menu: UseReflex edit conditions not found"); return; }
    if (!Install(setting, g_reflex, R_set, R_index, R_default, R_options)) { g_reflexFailed = true; return; } // frame generation still works
    // Without its conditions the setting stays editable (the references are
    // left to the registry, which keeps the objects).
    int none = 0;
    memcpy((char*)setting + kEditConditions + 8, &none, sizeof(none));
    SetReflexBoost(g_reflex.index(setting) == 2);
    Log("Menu: UseReflex unlocked (%d edit conditions); On/Boost while frame generation is on", conditions.count);
}

static void Tick() {
    if (g_failed) return;
    ULONGLONG now = GetTickCount64();
    static ULONGLONG nextScan;
    if (ue::Alive(g_frameGen.object) && (g_reflexFailed || ue::Alive(g_reflex.object))) return;
    if (now < nextScan) return;
    nextScan = now + 1000;
    // The registry builds its settings when Settings first opens.
    static void* boolType, *numberType;
    if (!boolType) boolType = ue::FindClass("GameSettingValueDiscreteDynamic_Bool");
    if (!numberType) numberType = ue::FindClass("GameSettingValueDiscreteDynamic_Number");
    for (int i = 0, n = boolType ? ue::Count() : 0; i < n; ++i) {
        void* object = ue::At(i, NULL);
        if (!object || ue::Template(object)) continue;
        void* type = ue::ClassOf(object);
        bool frameGen = type == boolType && !ue::Alive(g_frameGen.object), reflex = type == numberType && !g_reflexFailed && !ue::Alive(g_reflex.object);
        if (!frameGen && !reflex) continue;
        ue::Call dev(object, "GetDevName");
        if (!dev.Run()) continue;
        uint32_t name = dev.Result<uint32_t>("ReturnValue");
        if (frameGen && ue::NameEquals(name, "DLSSFrameGeneration")) InstallFrameGen(object);
        else if (reflex && ue::NameEquals(name, "UseReflex")) InstallReflex(object);
    }
    // Rows built before the unlock show the old lock until the settings that
    // depend on frame generation are re-evaluated: switching it and back does.
    static ue::Ref unlocked;
    if (ue::Alive(g_reflex.object) && ue::Alive(g_frameGen.object) && unlocked.object != g_reflex.object.object) {
        unlocked = g_reflex.object;
        void* frameGen = g_frameGen.object.object;
        int current = g_frameGen.index(frameGen) > 0 ? 1 : 0, reflex = g_reflex.index(g_reflex.object.object);
        g_frameGen.set(frameGen, 1 - current);
        g_frameGen.set(frameGen, current);
        if (g_reflex.index(g_reflex.object.object) != reflex) g_reflex.set(g_reflex.object.object, reflex);
    }
}

} // namespace menu_khazan
