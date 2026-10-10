// Native settings row for Ghostrunner 2 (UE 4.27), included by dlss_mfg_sm86.cpp.
//
// Settings > Video is a C++ panel (PanelSettingsSubVideo) whose rows keep a
// State index, an options text list and explicit up/down neighbours. Below the
// game's "Frame generation" row this adds a combo row of the game's own class
// (the Reflex row's) with 2x / 3x / 4x / Auto, written to the INI like the
// Returnal row. The row is spliced into the scroll box, the panel's option and
// button lists, and the navigation chain. Any missing name leaves the menu as
// shipped. Ticked on the game thread by ue_host.cpp.

namespace menu_gr2 {

using ue::Call;
static ue::Ref g_panel, g_row;
static void* g_frameGen;
static int g_index = -1;
static ULONGLONG g_attachedAt;
static int g_pending = -1; // the row's shown, not yet applied choice
static void* g_reflex;
static bool g_frameGenOn;
static bool g_failed;

static void Fail(const char* reason) {
    if (!g_failed) Log("Menu disabled: %s", reason);
    g_failed = true;
}

// Inserts `item` after `after` in an object-pointer array property, growing it
// in an engine-allocated buffer when it is full.
static bool InsertAfter(void* owner, const char* property, void* after, void* item) {
    int at = ue::OffsetOf(owner, property);
    if (at < 0) return false;
    auto list = ue::Read<ue::Array<void*>>(owner, at);
    int position = -1;
    for (int i = 0; i < list.count; ++i) if (list.data[i] == after) position = i + 1;
    if (position < 0) return false;
    if (list.count >= list.capacity) {
        int bytes = 0;
        void** grown = (void**)ue::EngineBuffer((size_t)(list.count + 1) * sizeof(void*), &bytes);
        if (!grown) return false;
        memcpy(grown, list.data, list.count * sizeof(void*));
        list.data = grown; list.capacity = bytes / (int)sizeof(void*); // the old buffer is left to leak
    }
    memmove(list.data + position + 1, list.data + position, (list.count - position) * sizeof(void*));
    list.data[position] = item; list.count++;
    memcpy((char*)owner + at, &list, sizeof(list));
    return true;
}

static void Neighbours(void* button, void* up, void* down) {
    int at = ue::OffsetOf(button, "NavigationSettings");
    if (at < 0) return;
    void** current = (void**)((char*)button + at + 8); // ButtonNavigation: Button, then Up/Right/Down/Left
    Call(button, "SetNavigationTargets").Arg("Up", up ? up : current[0]).Arg("Right", current[1])
        .Arg("Down", down ? down : current[2]).Arg("Left", current[3]).Run();
}

static void SlotLayout(void* slot, void* source) {
    if (!slot || !source) return;
    struct Margin { float left, top, right, bottom; };
    int padding = ue::OffsetOf(source, "Padding"), horizontal = ue::OffsetOf(source, "HorizontalAlignment");
    int vertical = ue::OffsetOf(source, "VerticalAlignment");
    if (padding >= 0) Call(slot, "SetPadding").Arg("InPadding", ue::Read<Margin>(source, padding)).Run();
    if (horizontal >= 0) Call(slot, "SetHorizontalAlignment").Arg("InHorizontalAlignment", ue::Read<uint8_t>(source, horizontal)).Run();
    if (vertical >= 0) Call(slot, "SetVerticalAlignment").Arg("InVerticalAlignment", ue::Read<uint8_t>(source, vertical)).Run();
}

// Each multicast binding once: a delegate array holds (weak object, function
// name) pairs of 16 bytes.
static void Unique(void* owner, const char* property) {
    int at = owner ? ue::OffsetOf(owner, property) : -1;
    if (at < 0) return;
    auto list = ue::Read<ue::Array<char[16]>>(owner, at);
    int kept = 0;
    for (int i = 0; i < list.count; ++i) {
        bool seen = false;
        for (int k = 0; k < kept && !seen; ++k) seen = !memcmp(list.data[k], list.data[i], 16);
        if (!seen) memmove(list.data[kept++], list.data[i], 16);
    }
    memcpy((char*)owner + at + 8, &kept, sizeof(kept)); // Num
}

// SetActive(true) binds the combo row's click and selection handlers again
// without checking: twice bound, a mouse click opens the list and closes it at
// once.
static void Activate(void* option, bool active) {
    Call(option, "SetActive").Arg("bNewActive", active).Run();
    void* button = ue::Get<void*>(option, "BtnCombo", NULL), *combo = ue::Get<void*>(option, "CbOptions", NULL);
    for (const char* name : {"OnClicked", "OnPressed", "OnReleased", "OnHovered", "OnUnhovered"}) Unique(button, name);
    for (const char* name : {"OnSelectionChanged", "OnOpening"}) Unique(combo, name);
    void* base = ue::Get<void*>(option, "BtnBase", NULL);
    for (const char* name : {"OnClicked", "OnPressed", "OnReleased", "OnHovered", "OnUnhovered"}) Unique(base, name);
}

static void Attach(void* panel) {
    void* frameGen = ue::Get<void*>(panel, "OptDLSSGMode", NULL);
    void* reflex = ue::Get<void*>(panel, "OptReflex", NULL);
    void* scroll = ue::Get<void*>(panel, "ScrlBoxVideo", NULL);
    void* library = ue::FindDefault("WidgetBlueprintLibrary");
    if (!frameGen || !reflex || !scroll || !library) { Fail("video settings rows not found"); return; }
    Call create(library, "Create");
    create.Arg("WorldContextObject", panel).Arg("WidgetType", ue::ClassOf(reflex)).Arg("OwningPlayer", (void*)NULL).Run();
    void* row = create.Result<void*>("ReturnValue");
    if (!row) { Fail("row not created"); return; }
    Call(row, "InitPanel").Arg("InPanel", panel).Run();
    // Behaviour flags as on the game's Reflex row (same class).
    for (const char* flag : {"bCanLoop", "bCanFocus", "bCanChangeHor", "bCanChangeVer", "bHasDownArrow",
        "bSelectOnHover", "bSelectOnClick", "bDeselectOnUnhover"})
        ue::Set<bool>(row, flag, ue::Get<bool>(reflex, flag, false));
    ue::Set<float>(row, "MaxListHeight", ue::Get<float>(reflex, "MaxListHeight", 450.0f));
    ue::Set<float>(row, "InactiveAlpha", ue::Get<float>(reflex, "InactiveAlpha", 0.4f));
    for (const char* title : {"Title", "TitleXbox", "TitlePlaystation"}) ue::Set(row, title, ue::MakeText(L"Multi frame generation"));
    Call(row, "UpdateTitle").Arg("InText", ue::MakeText(L"Multi frame generation")).Run();
    // The designer sized each row's title per instance; take the Reflex row's.
    void* title = ue::Get<void*>(row, "TxtTitle", NULL), *model = ue::Get<void*>(reflex, "TxtTitle", NULL);
    int font = ue::OffsetOf(model, "Font");
    if (title && model && font >= 0) {
        Call setFont(title, "SetFont");
        int at = setFont.function ? ue::Offset(setFont.function, "InFontInfo") : -1;
        if (at >= 0 && at + 0x50 <= (int)sizeof(setFont.parameters)) {
            memcpy(setFont.parameters + at, (char*)model + font, 0x50); // FSlateFontInfo (UE 4.27), copied by the callee
            setFont.Run();
        }
    }
    int bytes = 0;
    auto texts = (ue::Text*)ue::EngineBuffer(4 * sizeof(ue::Text), &bytes);
    if (!texts) { Fail("option list not allocated"); return; }
    for (int i = 0; i < 4; ++i) texts[i] = ue::MakeText(i == menu::kAuto ? L"Auto" : i == 0 ? L"2x" : i == 1 ? L"3x" : L"4x");
    ue::Set(row, "OptionsList", ue::Array<ue::Text>{texts, 4, bytes / (int)sizeof(ue::Text)});
    ue::Set<int>(row, "StateMin", 0);
    ue::Set<int>(row, "StateMax", menu::kAuto);
    int index = menu::CurrentIndex();
    Call(row, "RefreshOptionsText").Run();
    // The panel restores every option's saved (temp) state when it opens.
    Call(row, "SetState").Arg("InState", index).Arg("AutoRefresh", true).Arg("SaveTempState", true).Run();

    // Visual position: re-append the rows after Frame generation behind ours.
    Call where(scroll, "GetChildIndex");
    where.Arg("Content", frameGen).Run();
    int at = where.Result<int>("ReturnValue");
    Call total(scroll, "GetChildrenCount");
    total.Run();
    int count = total.Result<int>("ReturnValue");
    void* after[64]; void* layout[64];
    int moved = 0;
    for (int i = at + 1; at >= 0 && i < count && moved < 64; ++i, ++moved) {
        Call child(scroll, "GetChildAt");
        child.Arg("Index", i).Run();
        after[moved] = child.Result<void*>("ReturnValue");
        layout[moved] = ue::Get<void*>(after[moved], "Slot", NULL);
    }
    if (at < 0 || at + 1 + moved != count) { Fail("video settings layout"); return; }
    for (int i = 0; i < moved; ++i) Call(scroll, "RemoveChild").Arg("Content", after[i]).Run();
    Call add(scroll, "AddChild");
    add.Arg("Content", row).Run();
    SlotLayout(add.Result<void*>("ReturnValue"), ue::Get<void*>(frameGen, "Slot", NULL));
    for (int i = 0; i < moved; ++i) {
        Call again(scroll, "AddChild");
        again.Arg("Content", after[i]).Run();
        SlotLayout(again.Result<void*>("ReturnValue"), layout[i]);
    }
    // The panel's own lists (focus and value handling) and the navigation chain.
    bool listed = InsertAfter(panel, "OptionList", frameGen, row) & InsertAfter(panel, "BtnList", frameGen, row);
    Neighbours(row, frameGen, reflex);
    Neighbours(frameGen, NULL, row);
    Neighbours(reflex, row, NULL);
    g_panel = ue::Track(panel); g_row = ue::Track(row); g_frameGen = frameGen; g_index = index; g_attachedAt = GetTickCount64();
    Log("Menu row attached below Frame generation (%d of %d), lists=%d, value=%ls", at + 1, count + 1, listed, menu::kValues[index]);
}

static void Tick() {
    if (g_failed) return;
    static ULONGLONG nextScan;
    if (!g_row.object) {
        ULONGLONG now = GetTickCount64();
        if (now < nextScan) return;
        nextScan = now + 2000;
        // Name lookups scan every object, so the class is found once; instances
        // are then matched by pointer.
        static void* type, *settingsType;
        if (!type) type = ue::FindClass("BP_PanelSettingsSubVideo_C");
        // The saved Reflex choice holds from the start, before Settings opens.
        if (!settingsType) settingsType = ue::FindClass("GhostrunnerGameUserSettings");
        if (void* settings = ue::FindInstance(settingsType))
            SetReflexBoost(ue::Get<int>(settings, "DLSSG", 0) != 0 && ue::Get<int>(settings, "Reflex", 1) == 2);
        void* panel = ue::FindInstance(type);
        if (panel && ue::Get<void*>(panel, "OptDLSSGMode", NULL)) Attach(panel);
        return;
    }
    if (!ue::Alive(g_panel) || !ue::Alive(g_row)) {
        Log("Menu row released (panel %d, row %d)", ue::Alive(g_panel), ue::Alive(g_row));
        g_panel = g_row = ue::Ref{}; g_frameGen = NULL;
        return;
    }
    void* row = g_row.object;
    // Shown, hidden, enabled and disabled with the game's Frame generation row,
    // and disabled while that row is Off: the count only matters with it on.
    Call visibility(g_frameGen, "GetVisibility");
    visibility.Run();
    uint8_t shown = visibility.Result<uint8_t>("ReturnValue");
    Call mine(row, "GetVisibility");
    mine.Run();
    if (mine.Result<uint8_t>("ReturnValue") != shown) Call(row, "SetVisibility").Arg("InVisibility", shown).Run();
    bool available = ue::Get<bool>(g_frameGen, "bIsActive", true) && ue::Get<int>(g_frameGen, "State", 1) != 0;
    if (ue::Get<bool>(row, "bIsActive", true) != available) Activate(row, available);
    // A shown value that differs from the applied one is a pending change: report
    // it like the game's options do, which enables "Apply settings".
    int shownIndex = ue::Get<int>(row, "State", -1), appliedIndex = ue::Get<int>(row, "TmpState", -1);
    if (shownIndex != appliedIndex && shownIndex >= 0 && shownIndex <= menu::kAuto && shownIndex != g_pending) {
        g_pending = shownIndex;
        Call(g_panel.object, "AnyOptionChange").Run();
    }
    // Frame generation needs Reflex, so the game locks its Reflex row at
    // "Enabled" while it is on (and shows Enabled even when Enabled + Boost is
    // saved), and ignores the row when settings are applied. Keep the choice
    // between the two: the row stays enabled, Disabled is out of its range (and
    // hidden in its list, see After), a choice committed by "Apply settings" is
    // saved here, and otherwise the row shows the saved mode.
    void* reflex = ue::Get<void*>(g_panel.object, "OptReflex", NULL);
    void* settings = ue::Get<void*>(g_panel.object, "UserSettingsGR", NULL);
    static int reflexPending = -1;
    g_reflex = reflex;
    g_frameGenOn = ue::Get<int>(g_frameGen, "State", 0) != 0;
    if (reflex) {
        int minimum = g_frameGenOn ? 1 : 0;
        if (ue::Get<int>(reflex, "StateMin", 0) != minimum) ue::Set<int>(reflex, "StateMin", minimum);
        if (g_frameGenOn && settings) {
            if (!ue::Get<bool>(reflex, "bIsActive", true)) Activate(reflex, true);
            int saved = ue::Get<int>(settings, "Reflex", 1);
            saved = saved < 1 ? 1 : saved > 2 ? 2 : saved;
            int state = ue::Get<int>(reflex, "State", saved), applied = ue::Get<int>(reflex, "TmpState", saved);
            if (state != applied) reflexPending = state;
            else if (state == reflexPending && state != saved && state >= 1 && state <= 2) {
                reflexPending = -1;
                ue::Set<int>(settings, "Reflex", state);
                Call(settings, "SaveSettings").Run();
                Log("Menu Reflex=%s", state == 2 ? "Enabled + Boost" : "Enabled");
            } else if (state != saved) {
                reflexPending = -1;
                Call(reflex, "SetState").Arg("InState", saved).Arg("AutoRefresh", true).Arg("SaveTempState", true).Run();
            } else reflexPending = -1;
        }
    }
    // The game itself sends Streamline plain low latency while frame generation
    // is on; the saved Boost choice is applied by the proxy.
    SetReflexBoost(settings && ue::Get<int>(settings, "DLSSG", 0) != 0 && ue::Get<int>(settings, "Reflex", 1) == 2);
    // "Apply settings" commits options: TmpState takes the pending State. Only a
    // commit of the pending choice counts; the game also rewrites its options'
    // applied values on its own (when Settings opens, and right after the panel
    // is built), which is put back.
    if (appliedIndex == g_index) {
        if (shownIndex == appliedIndex) g_pending = -1; // Back reverted the choice
        return;
    }
    if (appliedIndex == g_pending) {
        g_index = appliedIndex;
        g_pending = -1;
        menu::Apply(appliedIndex);
    } else {
        g_pending = -1;
        Call(row, "SetState").Arg("InState", g_index).Arg("AutoRefresh", true).Arg("SaveTempState", true).Run();
    }
}

// While frame generation is on, the Reflex list leaves out Disabled: its combo
// builds one widget per entry, and the first entry's is collapsed.
static void After(void* object, void* function, void* parameters) {
    if (!g_frameGenOn || !g_reflex || object != g_reflex || !ue::Is(function, "On_CbOptions_GenerateWidget")) return;
    int item = ue::Offset(function, "Item"), result = ue::Offset(function, "ReturnValue");
    void* combo = ue::Get<void*>(g_reflex, "CbOptions", NULL);
    if (item < 0 || result < 0 || !combo) return;
    auto shown = ue::Read<ue::String>(parameters, item);
    Call first(combo, "GetOptionAtIndex");
    first.Arg("Index", 0).Run();
    auto disabled = first.Result<ue::String>("ReturnValue");
    void* widget = ue::Read<void*>(parameters, result);
    if (widget && shown.data && disabled.data && !wcscmp(shown.data, disabled.data))
        Call(widget, "SetVisibility").Arg("InVisibility", (uint8_t)1).Run(); // Collapsed
}

} // namespace menu_gr2
