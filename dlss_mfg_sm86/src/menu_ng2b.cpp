// Native settings row for NINJA GAIDEN 2 Black (UE5), included by
// dlss_mfg_sm86.cpp.
//
// Options > Screen Settings has a "Frame Generation" toggle and its own NVIDIA
// Reflex row (Off / On / Boost), which needs nothing. The toggle row becomes
// Off / 2x / 3x / 4x / Auto: every option row widget also carries the arrow
// part of the game's choice rows, so the row shows that part with our value.
// The game handles option input natively and flips a toggle on left/right and
// confirm alike, so while the row has the cursor that input is read here from
// the game's own input state (keyboard: arrows, A/D, C; gamepad: d-pad, left
// stick, bottom face button): left/right step
// the value, confirm switches Off <-> the last count, and the game's toggle is
// kept at On for any count (switched through the row's own click handler).
// Like the game's own options a change applies at once (toggle and INI).

namespace menu_ng2b {

static ue::Ref g_row;

// The game's toggle state and its click handler on the row.
static bool FrameGenOn(void* row) { return ue::Get<bool>(row, "is_toggle_on", false); }
static int g_index = -1, g_last = 3; // shown value; last count (for confirm)
static void Apply(int index) {
    if (index) { menu::Apply(index - 1); g_last = index; }
    else Log("Menu Frame generation=Off");
}

static bool ReadText(void* block, char* text, int capacity) {
    text[0] = 0;
    int at = block ? ue::OffsetOf(block, "Text") : -1;
    if (at < 0) return false;
    static void* library;
    if (!library) library = ue::FindDefault("KismetTextLibrary");
    ue::Call call(library, "Conv_TextToString");
    call.Arg("InText", ue::Read<ue::Text>(block, at)).Run();
    auto s = call.Result<ue::String>("ReturnValue");
    int k = 0;
    for (; s.data && k < s.count - 1 && k < capacity - 1; ++k) text[k] = (char)s.data[k];
    text[k] = 0;
    return k > 0;
}

// The Screen Settings row titled "Frame Generation".
static void* FindRow() {
    static void* type;
    if (!type) type = ue::FindClass("WBP_08_option_menu_S_C");
    for (int i = 0, n = type ? ue::Count() : 0; i < n; ++i) {
        void* row = ue::At(i, NULL);
        if (!row || ue::ClassOf(row) != type || ue::Template(row)) continue;
        char title[64];
        if (ReadText(ue::Get<void*>(row, "text_menu_name", NULL), title, sizeof(title)) && !strcmp(title, "Frame Generation")) return row;
    }
    return NULL;
}

static void SetVisible(void* widget, bool visible) {
    if (!widget) return;
    ue::Call get(widget, "GetVisibility");
    uint8_t wanted = visible ? 4 : 1; // SelfHitTestInvisible : Collapsed
    if (get.Run() && (get.Result<uint8_t>("ReturnValue") == 1) == visible) ue::Call(widget, "SetVisibility").Arg("InVisibility", wanted).Run();
}
static void Show(void* row, int index) {
    static const wchar_t* const labels[] = {L"Off", L"2x", L"3x", L"4x", L"Auto"};
    SetVisible(ue::Get<void*>(row, "type_toggle", NULL), false);
    SetVisible(ue::Get<void*>(row, "type_arrow_S", NULL), true);
    void* text = ue::Get<void*>(row, "text_arrow", NULL);
    char shown[32], wanted[32]; size_t n = 0;
    wcstombs_s(&n, wanted, labels[index], _TRUNCATE);
    if (text && (!ReadText(text, shown, sizeof(shown)) || strcmp(shown, wanted)))
        ue::Call(text, "SetText").Arg("InText", ue::MakeText(labels[index])).Run();
}

// The game's input state for a key (APlayerController::IsInputKeyDown).
static bool KeyDown(void* controller, const wchar_t* key) {
    static void* strings;
    if (!strings) strings = ue::FindDefault("KismetStringLibrary");
    struct Name { const wchar_t* key; uint64_t name; };
    static Name names[16];
    uint64_t name = 0;
    for (Name& n : names) {
        if (n.key == key) { name = n.name; break; }
        if (!n.key) {
            ue::Call convert(strings, "Conv_StringToName");
            convert.Arg("InString", ue::MakeString(key)).Run();
            n = Name{key, convert.Result<uint64_t>("ReturnValue")};
            name = n.name;
            break;
        }
    }
    struct { uint64_t name; void* details[2]; } fkey = {name, {}}; // FKey
    ue::Call down(controller, "IsInputKeyDown");
    down.Arg("Key", fkey).Run();
    return down.Result<bool>("ReturnValue");
}

// Presses seen since the last Step, sampled often (After) so taps count.
static volatile LONG g_pressed; // 1 left, 2 right, 4 confirm
static void Sample(void* world) {
    static void* statics;
    if (!statics) statics = ue::FindDefault("GameplayStatics");
    ue::Call get(statics, "GetPlayerController");
    get.Arg("WorldContextObject", world).Arg("PlayerIndex", 0).Run();
    void* controller = get.Result<void*>("ReturnValue");
    if (!controller) return;
    bool left = KeyDown(controller, L"Left") || KeyDown(controller, L"A") || KeyDown(controller, L"Gamepad_DPad_Left") || KeyDown(controller, L"Gamepad_LeftStick_Left");
    bool right = KeyDown(controller, L"Right") || KeyDown(controller, L"D") || KeyDown(controller, L"Gamepad_DPad_Right") || KeyDown(controller, L"Gamepad_LeftStick_Right");
    bool confirm = KeyDown(controller, L"C") || KeyDown(controller, L"Gamepad_FaceButton_Bottom");
    static bool wasLeft, wasRight, wasConfirm;
    LONG pressed = (left && !wasLeft ? 1 : 0) | (right && !wasRight ? 2 : 0) | (confirm && !wasConfirm ? 4 : 0);
    wasLeft = left; wasRight = right; wasConfirm = confirm;
    if (pressed) InterlockedOr(&g_pressed, pressed);
}
// -1 / +1 for a left/right press, 2 for confirm, else 0.
static int Step() {
    LONG pressed = InterlockedExchange(&g_pressed, 0);
    return pressed & 1 ? -1 : pressed & 2 ? 1 : pressed & 4 ? 2 : 0;
}
static void After(void*, void*, void*) {
    static ULONGLONG next;
    ULONGLONG now = GetTickCount64();
    if (now < next || !ue::Alive(g_row)) return;
    next = now + 15;
    Sample(g_row.object);
}
static bool GameInFront() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

static void Tick() {
    if (!ue::Alive(g_row)) {
        static ULONGLONG nextScan;
        ULONGLONG now = GetTickCount64();
        if (now < nextScan) return;
        nextScan = now + 500;
        void* row = FindRow();
        if (!row) return;
        g_row = ue::Track(row);
        g_index = -1;
        static bool logged;
        if (!logged) { logged = true; Log("Menu: Frame Generation extended to Off/2x/3x/4x/Auto"); }
    }
    void* row = g_row.object;
    ue::Call visible(row, "IsVisible");
    int step = Step(); // read every tick, so presses are new ones
    if (!visible.Run() || !visible.Result<bool>("ReturnValue")) { g_index = -1; return; }
    bool focused = ue::Get<bool>(row, "is_on", false) && GameInFront();
    // Without the cursor the game's toggle is the truth (e.g. "Default").
    if (g_index < 0 || !focused) g_index = FrameGenOn(row) ? 1 + menu::CurrentIndex() : 0;
    if (focused && step) {
        int index = step == 2 ? (g_index ? 0 : g_last) : g_index + step;
        if (index >= 0 && index <= 4 && index != g_index) { g_index = index; Apply(index); }
    }
    // The game flips its toggle on the same input: keep it at our value.
    if (focused && FrameGenOn(row) != (g_index > 0)) ue::Call(row, "set_clicked_toggle").Run();
    Show(row, g_index);
}

} // namespace menu_ng2b
