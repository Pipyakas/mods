// Native settings rows for ACE COMBAT 8 (UE5), included by dlss_mfg_sm86.cpp.
//
// AC8 has no frame generation or Reflex setting: it turns frame generation on
// in gameplay by itself and asks for Reflex Boost. Its option pages are built
// natively from option IDs: a page shows the IDs in its own lists that it
// knows, each titled by its LabelTextId and described by a row of
// DT_MenuOptionConfig (value texts and values), and the game stores and
// applies the values itself. The Graphics page also knows
// Graphics_GraphicsMode, the consoles' quality/frame rate mode, which does
// nothing on PC: it carries a "Frame Generation" row, Off / 2x / 3x / 4x /
// Auto. Its table row gets the new value texts, the page's class defaults the
// ID and title (again whenever a level reloads the class), and the game does
// the rest like for its own rows.
//
// The row shows, and the game saves, the raw value in its system save data
// (LiveSystemSaveGame.GraphicsData.GraphicsMode, a console enum of 0 and 1 that
// holds any value). The INI is the setting: its value is written there before
// the page is built, so the row shows it, and the value there once the page
// has closed (the player's choice, kept or put back by "Settings have been
// changed. Apply them?") is applied through [FrameGeneration] Enabled / Mode /
// GeneratedFrames. No other ID the page knows is free, so Reflex stays with the
// game (Boost) and [Reflex] Mode.

namespace menu_ac8 {

static const wchar_t* const kLabels = L"Off,2x,3x,4x,Auto";
// The values stored for them, by menu index (Off, then 1 + menu::CurrentIndex):
// the game's default, 0, is Auto (the INI's default too), and its other stock
// value, 1, is 4x.
static const wchar_t* const kValues = L"9,2,3,1,0";
static const uint8_t kStored[] = {9, 2, 3, 1, 0};
static const char* const kFrameGenId = "Graphics_GraphicsMode";
static bool g_patched, g_failed;
static ue::Ref g_save;   // LiveSystemSaveGame
static int g_modeAt = -1; // GraphicsData.GraphicsMode in it

static void Fail(const char* reason) {
    if (!g_failed) Log("Menu disabled: %s", reason);
    g_failed = true;
}

// Engine-allocated strings (UKismetStringLibrary::ParseIntoArray).
static ue::Array<ue::String> Strings(const wchar_t* list) {
    static void* library;
    if (!library) library = ue::FindDefault("KismetStringLibrary");
    ue::Call call(library, "ParseIntoArray");
    call.Arg("SourceString", ue::MakeString(list)).Arg("Delimiter", ue::MakeString(L",")).Arg("CullEmptyStrings", true).Run();
    return call.Result<ue::Array<ue::String>>("ReturnValue");
}

static void* FindStruct(const char* name) {
    for (int i = 0, n = ue::Count(); i < n; ++i) {
        void* object = ue::At(i, NULL);
        if (object && ue::Is(object, name) && ue::Is(ue::ClassOf(object), "ScriptStruct")) return object;
    }
    return NULL;
}
static int MemberOffset(const void* type, const char* name) {
    for (char* f = ue::Read<char*>(type, ue::kStructChildProperties); f; f = ue::Read<char*>(f, ue::kPropertyNext))
        if (ue::NameEquals(ue::Read<uint32_t>(f, ue::kPropertyName), name)) return ue::Read<int>(f, ue::kPropertyOffset);
    return -1;
}

// A DT_MenuOptionConfig row's value list becomes `labels` with `values`.
static bool Relabel(char* row, int valuesAt, void* valueType, const wchar_t* labels, const wchar_t* values) {
    int stride = ue::Read<int>(valueType, 0x58);
    int textAt = MemberOffset(valueType, "TextID"), valueAt = MemberOffset(valueType, "Value");
    if (stride < 32 || stride > 512 || textAt < 0 || valueAt < 0) return false;
    auto texts = Strings(labels), valueStrings = Strings(values);
    int bytes = 0;
    char* list = (char*)ue::EngineBuffer((size_t)texts.count * stride, &bytes);
    if (!list || valueStrings.count != texts.count) return false;
    memset(list, 0, (size_t)texts.count * stride);
    // The strings are moved out of the parsed arrays, which are left to leak.
    for (int i = 0; i < texts.count; ++i) {
        memcpy(list + (size_t)i * stride + textAt, &texts.data[i], sizeof(ue::String));
        memcpy(list + (size_t)i * stride + valueAt, &valueStrings.data[i], sizeof(ue::String));
    }
    ue::Array<char> replaced{list, texts.count, bytes / stride};
    memcpy(row + valuesAt, &replaced, sizeof(replaced)); // the old list is left to leak
    return true;
}

static void SetStrings(void* object, const char* property, const wchar_t* list) {
    int at = ue::OffsetOf(object, property);
    if (at < 0) return;
    auto strings = Strings(list);
    memcpy((char*)object + at, &strings, sizeof(strings)); // the old array is left to leak
}
static int Narrow(const ue::String& s, char* text, int capacity) {
    int k = 0;
    for (; s.data && k < s.count - 1 && k < capacity - 1; ++k) text[k] = (char)s.data[k];
    text[k] = 0;
    return k;
}

// The option table (loaded with the game, well before the menus).
static void Patch() {
    g_patched = true;
    void* table = NULL;
    for (int i = 0, n = ue::Count(); i < n && !table; ++i) {
        void* object = ue::At(i, NULL);
        if (object && ue::Is(object, "DT_MenuOptionConfig") && !ue::Template(object)) table = object;
    }
    void* rowType = table ? ue::Get<void*>(table, "RowStruct", NULL) : NULL;
    void* valueType = FindStruct("LiveMenuOptionValueConfig");
    int valuesAt = rowType ? MemberOffset(rowType, "ValueConfigs") : -1;
    if (!table || !valueType || valuesAt < 0) { Fail("option table not found"); return; }
    // UDataTable::RowMap follows RowStruct: set elements of (row name, data).
    auto rows = ue::Read<ue::Array<char[24]>>(table, ue::OffsetOf(table, "RowStruct") + 8);
    char* frameGen = NULL;
    for (int r = 0; r < rows.count && !frameGen; ++r)
        if (ue::NameEquals(ue::Read<uint32_t>(rows.data[r], 0), kFrameGenId)) frameGen = ue::Read<char*>(rows.data[r], 8);
    if (!frameGen) { Fail("option row not found"); return; }
    if (!Relabel(frameGen, valuesAt, valueType, kLabels, kValues)) { Fail("option values not replaced"); return; }
    // The description shown for the row (an unknown text ID is shown as is).
    int helpAt = MemberOffset(rowType, "DefaultHelpTextID");
    if (helpAt >= 0) {
        auto help = Strings(L"Frames shown per rendered frame. Auto lets NVIDIA Dynamic Multi Frame Generation choose 2x to 4x for the display refresh rate.");
        if (help.count == 1) memcpy(frameGen + helpAt, &help.data[0], sizeof(ue::String));
    }
}

// The Graphics page's class defaults (pages built later copy them): the ID is
// shown after Motion Blur, allowed on PC, its values used as given, and titled
// "Frame Generation" (UsedCommandInfoList holds each ID's LabelTextId; an
// unknown text ID is shown as is).
static bool PatchPage(void* page) {
    int listAt = page ? ue::OffsetOf(page, "SteamOptionIdList") : -1;
    if (listAt < 0) return false;
    auto ids = ue::Read<ue::Array<ue::String>>(page, listAt);
    wchar_t list[1024] = {}; int used = 0;
    for (int i = 0; i < ids.count; ++i) {
        char id[96]; Narrow(ids.data[i], id, sizeof(id));
        if (!strcmp(id, kFrameGenId)) return true; // done before
        used += swprintf_s(list + used, 1024 - used, L"%s,", ids.data[i].data);
        if (!strcmp(id, "Graphics_MotionBlurOn")) used += swprintf_s(list + used, 1024 - used, L"%hs,", kFrameGenId);
    }
    SetStrings(page, "SteamOptionIdList", list);
    SetStrings(page, "NotLocalizedParamOptionIdArray", L"Graphics_GraphicsMode");
    static void* infoType;
    if (!infoType) infoType = FindStruct("LiveNUISystemCommonCommandCreateInfo");
    int infosAt = ue::OffsetOf(page, "UsedCommandInfoList");
    int stride = infoType ? ue::Read<int>(infoType, 0x58) : 0;
    int idAt = infoType ? MemberOffset(infoType, "OptionID") : -1, labelAt = infoType ? MemberOffset(infoType, "LabelTextId") : -1;
    if (infosAt >= 0 && stride >= 32 && idAt >= 0 && labelAt >= 0) {
        auto infos = ue::Read<ue::Array<char>>(page, infosAt);
        for (int i = 0; i < infos.count; ++i) {
            char* info = infos.data + (size_t)i * stride;
            char id[96]; Narrow(ue::Read<ue::String>(info, idAt), id, sizeof(id));
            if (strcmp(id, kFrameGenId)) continue;
            auto title = Strings(L"Frame Generation");
            if (title.count == 1) memcpy(info + labelAt, &title.data[0], sizeof(ue::String)); // the old string is left to leak
        }
    }
    return true;
}

// The INI's choice as a menu index (0 Off, then 2x, 3x, 4x, Auto).
static int Applied() { return g_frameGenEnabled ? 1 + menu::CurrentIndex() : 0; }
static void ApplyFrameGen(int index) {
    WritePrivateProfileStringW(L"FrameGeneration", L"Enabled", index ? L"1" : L"0", g_config);
    if (index) menu::Apply(index - 1); // writes the count and applies the INI
    else { Log("Menu Frame generation=Off"); ApplyConfiguration(PrimaryHz(), "Menu"); }
}

// The save data's stored value (NULL until the save data exists).
static uint8_t* Stored() {
    if (!ue::Alive(g_save)) {
        static ULONGLONG nextFind;
        ULONGLONG now = GetTickCount64();
        if (now < nextFind) return NULL;
        nextFind = now + 1000;
        static void* type;
        if (!type) type = ue::FindClass("LiveSystemSaveGame");
        void* save = ue::FindInstance(type);
        if (!save) return NULL;
        int dataAt = ue::OffsetOf(save, "GraphicsData");
        void* dataType = FindStruct("LiveGraphicsSaveData");
        int modeAt = dataType ? MemberOffset(dataType, "GraphicsMode") : -1;
        if (dataAt < 0 || modeAt < 0) { Fail("graphics save data not found"); return NULL; }
        g_save = ue::Track(save);
        g_modeAt = dataAt + modeAt;
    }
    return (uint8_t*)g_save.object + g_modeAt;
}
// What is shown of the settings menus (a page shown is in the viewport and
// visible; System's frame, which holds the lists, is only visible). Their
// classes are reloaded with each level, so they are found again when gone.
enum Kind { kGraphics = 1, kAdvanced = 2, kDialog = 4, kSystem = 8 };
static const char* const kKindClasses[] = {
    "WBP_Menu_SystemGraphics_000_C", "WBP_Menu_SystemGraphicsAdvancedSetting_000_C",
    "WBP_Menu_Common_Dialog_000_C", "WBP_Menu_SystemBoot_000_C"};
static ue::Ref g_classes[4];
static bool Visible(void* widget, bool inViewport) {
    ue::Call visible(widget, "IsVisible");
    if (!visible.Run() || !visible.Result<bool>("ReturnValue")) return false;
    if (!inViewport) return true;
    ue::Call call(widget, "IsInViewport");
    return call.Run() && call.Result<bool>("ReturnValue");
}
static int Shown() {
    void* types[4];
    for (int k = 0; k < 4; ++k) {
        if (!ue::Alive(g_classes[k])) g_classes[k] = ue::Track(ue::FindClass(kKindClasses[k]));
        types[k] = g_classes[k].object;
    }
    int shown = 0;
    for (int i = 0, n = ue::Count(); i < n; ++i) {
        void* object = ue::At(i, NULL);
        void* type = object ? ue::ClassOf(object) : NULL;
        if (!type) continue;
        for (int k = 0; k < 4; ++k)
            if (type == types[k] && !(shown & (1 << k)) && !ue::Template(object) && Visible(object, k < 3)) shown |= 1 << k;
    }
    return shown;
}

// Keeps the stored value and the INI in step. From the moment a Graphics page
// is shown the game owns the value: the row writes it as it changes, and the
// "Settings have been changed. Apply them?" shown when the Settings list is
// left keeps it (Yes) or puts the previous one back (No). Once that has been
// answered (or System has been left) a value that differs is the player's
// choice, applied. Otherwise (the save loaded from disk, the INI edited) the
// INI's value is written for the row to show.
static int g_opened = -1;  // the value when Graphics was shown, during a session
static bool g_asked;       // the dialog has been shown in it
static void Sync(ULONGLONG now) {
    uint8_t* stored = Stored();
    if (!stored) return;
    static ULONGLONG nextLook;
    if (now < nextLook) return;
    nextLook = now + 250;
    int shown = Shown();
    if (g_opened < 0) {
        if (shown & (kGraphics | kAdvanced)) {
            g_opened = *stored;
            g_asked = false;
            Log("Menu: Graphics shown (stored %d)", g_opened);
            return;
        }
    } else {
        if (shown & kDialog) g_asked = true;
        if ((shown & (kGraphics | kAdvanced | kDialog)) || (!g_asked && (shown & kSystem))) return;
    }
    int value = *stored, index = -1;
    for (int i = 0; i < 5; ++i) if (kStored[i] == value) index = i;
    if (g_opened >= 0) {
        Log("Menu: settings closed (stored %d)", value);
        if (value != g_opened && index >= 0 && index != Applied()) ApplyFrameGen(index);
        g_opened = -1;
    }
    uint8_t wanted = kStored[Applied()];
    if (value != wanted) *stored = wanted;
}

static void Tick() {
    if (g_failed) return;
    if (!g_patched) { Patch(); return; }
    // The page's class defaults, again whenever its class is reloaded (pages
    // built later copy them).
    static ue::Ref defaults;
    static ULONGLONG nextScan;
    ULONGLONG now = GetTickCount64();
    if (!ue::Alive(defaults) && now >= nextScan) {
        nextScan = now + 1000;
        void* page = ue::FindDefault("WBP_Menu_SystemGraphics_000_C");
        if (page && PatchPage(page)) {
            defaults = ue::Track(page);
            Log("Menu: Frame generation row added to Settings > Graphics");
        }
    }
    Sync(now);
}

} // namespace menu_ac8
