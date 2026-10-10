// ac8_offline: ACE COMBAT 8's offline-mode conveniences. Built as a DLL and
// embedded in start_protected_game.exe, which maps it into the game's memory
// itself (no file is written or loaded) and starts it with ac8_start, an APC on
// the game's main thread before the game's own code runs. The bootstrap's
// [Launcher] keys reach it as environment variables:
//   AC8_BOOTSTRAP_SKIP_INTRO=1        SkipIntro
//   AC8_BOOTSTRAP_HIDE_ONLINE_MENU=1  HideOnlineMenu
//   AC8_BOOTSTRAP_AGENT_CONTROL=1     [Debug] AgentControl (agent_control.cpp)
// No frame generation, graphics or network code. Unreal reflection is shared
// with dlss_mfg_sm86 (source only); both coexist in the game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include "MinHook.h"

static WCHAR g_directory[MAX_PATH], g_log[MAX_PATH];
static SRWLOCK g_logLock = SRWLOCK_INIT;
static bool g_quiet; // reflection retries while the engine starts

static void Log(const char* format, ...) {
    if (g_quiet || !g_log[0]) return;
    AcquireSRWLockExclusive(&g_logLock);
    FILE* file = NULL;
    if (!_wfopen_s(&file, g_log, L"a") && file) {
        fprintf(file, "pid=%lu tick=%llu ", GetCurrentProcessId(), GetTickCount64());
        va_list args; va_start(args, format); vfprintf(file, format, args); va_end(args);
        fputc('\n', file); fclose(file);
    }
    ReleaseSRWLockExclusive(&g_logLock);
}
static bool Enabled(const WCHAR* variable) {
    WCHAR value[4] = {};
    return GetEnvironmentVariableW(variable, value, 4) == 1 && value[0] == L'1';
}

#include "../../../../src/ue_reflection.cpp"
#include "agent_control.cpp"

namespace ac8 {

// Verified roots for this AC8 build, used only when they validate; other builds
// fall back to the scanner, which takes ~40 s (after the notices).
static void PrimeRoots() { ue::Prime(0xdc60cd0, 0xe37d990); }

static void* FindNamed(const char* className, const char* name) {
    for (int i = 0, n = ue::Count(); i < n; ++i) {
        void* object = ue::At(i, NULL);
        if (object && ue::Is(object, name) && ue::Is(ue::ClassOf(object), className)) return object;
    }
    return NULL;
}

// ---- SkipIntro: data edits made before the game uses the data -------------
// Menus are opened by ID from DT_LiveMenuMainClassTable; the startup notices
// (logos, legal screens) are the Sequences of DA_LiveMenuNoticeScreenSetting.

// The notice screen ends at once.
static bool SkipNotices() {
    void* setting = FindNamed("LiveMenuNoticeScreenSetting", "DA_LiveMenuNoticeScreenSetting");
    if (!setting) return false;
    int at = ue::OffsetOf(setting, "Sequences");
    if (at < 0) { Log("Intro: notice sequences not found"); return true; }
    auto sequences = ue::Read<ue::Array<char>>(setting, at);
    if (sequences.count < 0 || sequences.count > 64 || sequences.capacity < sequences.count) { Log("Intro: notice sequences not recognised"); return true; }
    int none = 0;
    memcpy((char*)setting + at + 8, &none, sizeof(none)); // the entries stay allocated
    Log("Intro: skipped %d startup notice screens", sequences.count);
    return true;
}

// The table's rows (UDataTable::RowMap after RowStruct: set elements of row
// name and row data).
static char* Row(void* table, const char* id) {
    int structAt = ue::OffsetOf(table, "RowStruct");
    if (structAt < 0) return NULL;
    auto rows = ue::Read<ue::Array<char[24]>>(table, structAt + 8);
    if (rows.count <= 0 || rows.count > 4096 || !ue::Readable(rows.data)) return NULL;
    for (int r = 0; r < rows.count; ++r)
        if (ue::NameEquals(ue::Read<uint32_t>(rows.data[r], 0), id)) return ue::Read<char*>(rows.data[r], 8);
    return NULL;
}
// The sign-in step (Authenticate) opens where the title screen ("PRESS THE
// ENTER KEY") would: the game signs in and loads the save as after that key.
static bool SkipTitle() {
    void* table = FindNamed("DataTable", "DT_LiveMenuMainClassTable");
    if (!table) return false;
    char* title = Row(table, "GameTitle"), *authenticate = Row(table, "Authenticate");
    if (!title || !authenticate || !ue::Readable(title) || !ue::Readable(authenticate)) { Log("Intro: menu rows not found"); return true; }
    // FSoftObjectPtr: weak pointer (8), then package and asset names (16); the
    // sub-path string after them is empty for classes and left alone.
    const int reference = ue::Offset(ue::Get<void*>(table, "RowStruct", NULL), "MenuWidgetReference");
    if (reference < 0) { Log("Intro: menu reference not found"); return true; }
    memcpy(title + reference, authenticate + reference, 24);
    Log("Intro: title screen replaced by the sign-in step");
    return true;
}

// The main menu's entrance: the background's boot sequence and the menu's and
// header's fade-ins are reduced to their last frame (MovieScene.PlaybackRange, a
// TRange<FFrameNumber>: lower bound type/value at +0/+4, upper at +8/+12).
struct Entrance { const char* owner, *animation; bool done; };
static Entrance g_entrance[] = {
    {"WBP_Menu_BG_000_C", "In_INST", false},
    {"WBP_Menu_MainTop_000_C", "In_INST", false},
    {"WBP_Menu_Common_Header_000_C", "Anim_In_INST", false},
};
static bool SkipEntrance() {
    bool all = true;
    for (Entrance& e : g_entrance) {
        if (e.done) continue;
        all = false;
        for (int i = 0, n = ue::Count(); i < n && !e.done; ++i) {
            void* animation = ue::At(i, NULL);
            if (!animation || !ue::Is(animation, e.animation) || !ue::Is(ue::ClassOf(animation), "WidgetAnimation")
                || !ue::Is(ue::Outer(animation), e.owner)) continue;
            void* scene = ue::Get<void*>(animation, "MovieScene", NULL);
            int at = scene ? ue::OffsetOf(scene, "PlaybackRange") : -1;
            if (at < 0) continue;
            char* range = (char*)scene + at;
            int lower = ue::Read<int>(range, 4), upper = ue::Read<int>(range, 12), last = upper - 1;
            if (last > lower) memcpy(range + 4, &last, sizeof(last));
            e.done = true;
            Log("Intro: %s %s shortened (%d frames)", e.owner, e.animation, upper - lower);
        }
    }
    return all;
}

static void SkipIntro() {
    bool notices = false, title = false, entrance = false;
    for (int i = 0; i < 9000 && !(notices && title && entrance); ++i) { // 3 minutes
        __try {
            if (!notices) notices = SkipNotices();
            if (!title) title = SkipTitle();
            if (!entrance) entrance = SkipEntrance();
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        Sleep(20);
    }
    if (!notices || !title || !entrance) Log("Intro: unfinished (notices %d, title %d, menu entrance %d)", notices, title, entrance);
}

// ---- HideOnlineMenu: before the main menu is built -------------------------
// Without Easy Anti-Cheat the main menu's ACE COMBAT ONLINE and ACE COMBAT
// STORE are greyed out for good. The menu is built from DT_LiveMenuMainTopCommands
// when its list (WBP_MenuCommon_MainList_000) is created, and the game already
// hides a row whose feature is locked and whose InvalidAppearance is Invisible
// (CAMPAIGN TRAINING until it is unlocked). Both rows are given that state just
// before the list is created: a feature value the game never unlocks (20, unused
// between DataViewer and Special1) and Invisible. Their StringArgs ("Online",
// "Store") are emptied, as the game greys out a row that names an online
// service whatever else it says. The game builds a four-row menu with its own
// navigation, mouse and descriptions.

static DWORD g_gameThread;
static ue::Ref g_patched; // the table already edited

// A data table by name, loaded from `path` when it is not (menus load theirs
// as they are built, and drop them when closed).
static void* LoadTable(const char* name, const wchar_t* path) {
    void* table = FindNamed("DataTable", name);
    if (table) return table;
    void* system = ue::FindDefault("KismetSystemLibrary");
    ue::Call soft_path(system, "MakeSoftObjectPath");
    soft_path.Arg("PathString", ue::MakeString(path)).Run();
    struct SoftPath { unsigned char bytes[24]; };   // FSoftObjectPath (UE5: package, asset, sub-path)
    struct SoftObject { unsigned char bytes[40]; }; // TSoftObjectPtr: weak pointer, then the path
    ue::Call soft(system, "Conv_SoftObjPathToSoftObjRef");
    if (!soft_path.Ok() || !soft.Ok()) return NULL;
    soft.Arg("SoftObjectPath", soft_path.Result<SoftPath>("ReturnValue")).Run();
    ue::Call load(system, "LoadAsset_Blocking");
    if (!load.Ok()) return NULL;
    load.Arg("Asset", soft.Result<SoftObject>("ReturnValue")).Run();
    table = load.Result<void*>("ReturnValue");
    return table && ue::Is(table, name) ? table : NULL;
}
static void HideRows() {
    if (ue::Alive(g_patched)) return;
    void* table = LoadTable("DT_LiveMenuMainTopCommands", L"/Game/Datatables/UI/Menu/MainMenu/Top/DT_LiveMenuMainTopCommands.DT_LiveMenuMainTopCommands");
    if (!table) { Log("Menu: command table not found; main menu left as shipped"); return; }
    void* row = ue::Get<void*>(table, "RowStruct", NULL);
    const int feature = ue::Offset(row, "RefFeature"), appearance = ue::Offset(row, "InvalidAppearance"), args = ue::Offset(row, "StringArgs");
    if (feature < 0 || appearance < 0 || args < 0) { Log("Menu: command rows not recognised; main menu left as shipped"); return; }
    int hidden = 0;
    for (const char* id : {"Online", "Shop"}) {
        char* data = Row(table, id);
        if (!data || !ue::Readable(data)) continue;
        data[feature] = 20;
        data[appearance] = 2;
        memset(data + args + 8, 0, sizeof(int)); // empty string, buffer kept
        hidden++;
    }
    g_patched = ue::Track(table);
    Log("Menu: %d online entries hidden from the main menu", hidden);
}

// UObject::ProcessEvent is hooked in place (not in the class tables, which
// dlss_mfg_sm86 routes): every caller, that one too, passes through here. The
// list's OnInitialized event comes before it is filled.
static ue::ProcessEventFn* g_original;
static void H_processEvent(void* object, void* function, void* parameters) {
    static uint32_t initialized; // the name OnInitialized
    if (GetCurrentThreadId() == g_gameThread) __try {
        uint32_t name = ue::Read<uint32_t>(function, ue::kObjectName);
        if (!initialized && ue::NameEquals(name, "OnInitialized")) initialized = name;
        if (initialized && name == initialized && ue::Is(ue::ClassOf(object), "WBP_MenuCommon_MainList_000_C")) HideRows();
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_original(object, function, parameters);
}
static void HideOnlineMenu() {
    MH_STATUS status = MH_Initialize();
    if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED)
        status = MH_CreateHook((void*)ue::g_processEvent, (void*)H_processEvent, (void**)&g_original);
    if (status == MH_OK) {
        ue::g_processEvent = g_original; // reflected calls made here skip the hook
        status = MH_EnableHook(MH_ALL_HOOKS);
    }
    Log("Menu: ProcessEvent hook %s", MH_StatusToString(status));
}

static DWORD WINAPI Worker(void*) {
    bool intro = Enabled(L"AC8_BOOTSTRAP_SKIP_INTRO"), hide = Enabled(L"AC8_BOOTSTRAP_HIDE_ONLINE_MENU");
    bool control = Enabled(L"AC8_BOOTSTRAP_AGENT_CONTROL");
    Log("Offline mode: SkipIntro=%d HideOnlineMenu=%d AgentControl=%d", intro, hide, control);
    if (control) agent::Start();
    if (!intro && !hide) return 0;
    ULONGLONG start = GetTickCount64();
    bool ready = false;
    for (int i = 0; i < 600 && !ready; ++i) { // the scanner is the fallback (slow)
        PrimeRoots();
        g_quiet = i > 0;
        if (ue::g_objects || i >= 100) ready = ue::Init();
        if (!ready) Sleep(100);
    }
    g_quiet = false;
    if (!ready) { Log("Offline mode: engine reflection not found; game left as shipped"); return 0; }
    Log("Offline mode: reflection ready after %llu ms", GetTickCount64() - start);
    if (hide) HideOnlineMenu();
    if (intro) SkipIntro();
    return 0;
}

} // namespace ac8

#ifndef AC8_OFFLINE_TEST
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    // Started on the game's main thread, which becomes its game thread.
    ac8::g_gameThread = GetCurrentThreadId();
    DWORD length = GetModuleFileNameW(NULL, g_directory, MAX_PATH); // AceCombat8.exe
    if (!length || length >= MAX_PATH) return TRUE;
    WCHAR* slash = wcsrchr(g_directory, L'\\');
    if (!slash) return TRUE;
    slash[1] = 0;
    wcscpy_s(g_log, g_directory); wcscat_s(g_log, L"ac8_offline.log");
    HANDLE thread = CreateThread(NULL, 0, ac8::Worker, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    return TRUE;
}

// Entry point for the bootstrap's mapping (an APC routine; `base` is where the
// image was mapped). Nothing has been initialised yet: the image's unwind data is
// registered first (structured exceptions are used throughout), then the C
// runtime is started, which calls DllMain.
extern "C" BOOL WINAPI _DllMainCRTStartup(HINSTANCE, DWORD, LPVOID);
extern "C" __declspec(dllexport) void WINAPI ac8_start(ULONG_PTR base) {
    auto image = (char*)base;
    auto nt = (IMAGE_NT_HEADERS*)(image + ((IMAGE_DOS_HEADER*)image)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& unwind = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (unwind.Size && !RtlAddFunctionTable((RUNTIME_FUNCTION*)(image + unwind.VirtualAddress), unwind.Size / sizeof(RUNTIME_FUNCTION), base)) return;
    _DllMainCRTStartup((HINSTANCE)base, DLL_PROCESS_ATTACH, NULL);
}
#endif
