// [Debug] AgentControl=1 (AC8_BOOTSTRAP_AGENT_CONTROL=1): lets a tool drive the
// game while it runs in the background, for mod work. Included by
// ac8_offline.cpp; off unless asked for.
//
// The game is kept believing it is the foreground application: GetForegroundWindow
// answers with its window and losing activation is not passed on (otherwise it
// ignores input and keeps frame generation off). Input comes from a named pipe,
// \\.\pipe\ac8_agent, one command per line, each answered "ok" or "err <why>":
//   key <name> [ms]       press (and hold) a key: Up Down Left Right Enter
//                         Escape Space Tab Back Delete Home End PageUp PageDown
//                         F1-F12, A-Z, 0-9, or vk<hex>
//   keydown/keyup <name>
//   text <characters>
//   move <x> <y>          the game's cursor (client pixels); the real one is left alone
//   click <x> <y> [right] [ms]
//   wheel <steps>
//   pad <button> [ms]     a virtual XInput pad 0: A B X Y Up Down Left Right
//                         Start Back LB RB LS RS LT RT
//   focus on|off          the background trick on or off (on at start)
//   ping
// Real keyboard and mouse input stays with the foreground application. Uses
// user32 through GetProcAddress (the bootstrap maps this module with kernel32
// imports only).

namespace agent {

typedef HWND WINAPI ForegroundFn();
typedef BOOL WINAPI CursorFn(POINT*);
typedef BOOL WINAPI PostFn(HWND, UINT, WPARAM, LPARAM);
typedef UINT WINAPI MapKeyFn(UINT, UINT);
typedef BOOL WINAPI EnumFn(WNDENUMPROC, LPARAM);
typedef DWORD WINAPI ThreadProcessFn(HWND, DWORD*);
typedef BOOL WINAPI VisibleFn(HWND);
typedef int WINAPI ClassNameFn(HWND, LPWSTR, int);
typedef BOOL WINAPI ClientToScreenFn(HWND, POINT*);
typedef LONG_PTR WINAPI SetLongFn(HWND, int, LONG_PTR);
typedef LRESULT WINAPI CallProcFn(WNDPROC, HWND, UINT, WPARAM, LPARAM);

static PostFn* g_post;
static MapKeyFn* g_mapKey;
static ClientToScreenFn* g_clientToScreen;
static CallProcFn* g_callProc;
static ForegroundFn* g_foreground; // originals
static ForegroundFn* g_active;
static CursorFn* g_cursor;
static HWND g_window;
static volatile bool g_focus = true, g_cursorSet;
static volatile LONG g_cursorX, g_cursorY;
static WNDPROC g_windowProc;

static HWND WINAPI H_foreground() { return g_focus && g_window ? g_window : g_foreground(); }
static HWND WINAPI H_active() { return g_focus && g_window ? g_window : g_active(); }
static BOOL WINAPI H_cursor(POINT* point) {
    if (!g_focus || !g_cursorSet || !g_window || !point) return g_cursor(point);
    POINT p{g_cursorX, g_cursorY};
    g_clientToScreen(g_window, &p);
    *point = p;
    return TRUE;
}
// Deactivation is turned into activation while the trick is on.
static LRESULT CALLBACK H_windowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (g_focus) {
        if ((message == WM_ACTIVATEAPP || message == WM_NCACTIVATE) && !w) w = TRUE;
        else if (message == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE) w = WA_ACTIVE;
        else if (message == WM_KILLFOCUS) return 0;
    }
    return g_callProc(g_windowProc, window, message, w, l);
}

// ---- virtual pad ---------------------------------------------------------
struct PadState { DWORD packet; WORD buttons; BYTE leftTrigger, rightTrigger; SHORT lx, ly, rx, ry; };
typedef DWORD WINAPI PadFn(DWORD, PadState*);
static PadFn* g_padOriginal[3];
static volatile LONG g_padButtons, g_padTriggers, g_padPacket;
template <int K> static DWORD WINAPI H_pad(DWORD index, PadState* state) {
    DWORD result = g_padOriginal[K](index, state);
    if (index != 0 || !state) return result;
    LONG buttons = g_padButtons, triggers = g_padTriggers;
    if (!buttons && !triggers && result == ERROR_SUCCESS) return result;
    if (result != ERROR_SUCCESS) { memset(state, 0, sizeof(*state)); result = ERROR_SUCCESS; }
    state->buttons |= (WORD)buttons;
    if (triggers & 1) state->leftTrigger = 255;
    if (triggers & 2) state->rightTrigger = 255;
    state->packet += g_padPacket;
    return result;
}
static bool HookPad() {
    static bool hooked;
    if (hooked) return true;
    static const wchar_t* const modules[3] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
    static PadFn* const detours[3] = {H_pad<0>, H_pad<1>, H_pad<2>};
    for (int k = 0; k < 3; ++k) {
        HMODULE module = GetModuleHandleW(modules[k]);
        void* target = module ? (void*)GetProcAddress(module, "XInputGetState") : NULL;
        if (target && MH_CreateHook(target, (void*)detours[k], (void**)&g_padOriginal[k]) == MH_OK && MH_EnableHook(target) == MH_OK) hooked = true;
    }
    return hooked;
}

// ---- commands ---------------------------------------------------------------
static const struct { const char* name; WORD vk; } kKeys[] = {
    {"Up", VK_UP}, {"Down", VK_DOWN}, {"Left", VK_LEFT}, {"Right", VK_RIGHT}, {"Enter", VK_RETURN},
    {"Escape", VK_ESCAPE}, {"Space", VK_SPACE}, {"Tab", VK_TAB}, {"Back", VK_BACK}, {"Delete", VK_DELETE},
    {"Home", VK_HOME}, {"End", VK_END}, {"PageUp", VK_PRIOR}, {"PageDown", VK_NEXT},
    {"Shift", VK_SHIFT}, {"Ctrl", VK_CONTROL}, {"Alt", VK_MENU},
};
static WORD KeyCode(const char* name) {
    for (auto& k : kKeys) if (!_stricmp(name, k.name)) return k.vk;
    if ((name[0] == 'F' || name[0] == 'f') && name[1] >= '1' && name[1] <= '9') {
        int n = atoi(name + 1);
        if (n >= 1 && n <= 12) return (WORD)(VK_F1 + n - 1);
    }
    if (!_strnicmp(name, "vk", 2) && name[2]) return (WORD)strtoul(name + 2, NULL, 16);
    if (name[0] && !name[1]) {
        char c = name[0];
        if (c >= 'a' && c <= 'z') c -= 32;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (WORD)c;
    }
    return 0;
}
static const struct { const char* name; WORD bit; int trigger; } kPad[] = {
    {"Up", 0x0001}, {"Down", 0x0002}, {"Left", 0x0004}, {"Right", 0x0008}, {"Start", 0x0010}, {"Back", 0x0020},
    {"LS", 0x0040}, {"RS", 0x0080}, {"LB", 0x0100}, {"RB", 0x0200}, {"A", 0x1000}, {"B", 0x2000},
    {"X", 0x4000}, {"Y", 0x8000}, {"LT", 0, 1}, {"RT", 0, 2},
};

static void Key(WORD vk, bool down) {
    UINT scan = g_mapKey(vk, 0 /* MAPVK_VK_TO_VSC */);
    bool extended = vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_DELETE
        || vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT;
    LPARAM l = 1 | ((LPARAM)scan << 16) | (extended ? (1 << 24) : 0) | (down ? 0 : (3u << 30));
    g_post(g_window, down ? WM_KEYDOWN : WM_KEYUP, vk, l);
}
static void Mouse(UINT message, WPARAM buttons) {
    g_post(g_window, message, buttons, MAKELPARAM(g_cursorX, g_cursorY));
}
static void MoveTo(int x, int y) {
    g_cursorX = x; g_cursorY = y; g_cursorSet = true;
    Mouse(WM_MOUSEMOVE, 0);
}

// One command line (in place); the reply in `reply`.
static void Run(char* line, char* reply, size_t size) {
    char* words[8] = {}; int count = 0;
    for (char* next = NULL, *word = strtok_s(line, " \t\r", &next); word && count < 8; word = strtok_s(NULL, " \t\r", &next)) words[count++] = word;
    strcpy_s(reply, size, "ok");
    if (!count) return;
    const char* verb = words[0];
    if (!_stricmp(verb, "ping")) return;
    if (!_stricmp(verb, "focus") && count > 1) { g_focus = !_stricmp(words[1], "on"); if (g_focus) g_post(g_window, WM_ACTIVATEAPP, TRUE, 0); return; }
    if (!g_window) { strcpy_s(reply, size, "err no game window"); return; }
    if ((!_stricmp(verb, "key") || !_stricmp(verb, "keydown") || !_stricmp(verb, "keyup")) && count > 1) {
        WORD vk = KeyCode(words[1]);
        if (!vk) { sprintf_s(reply, size, "err unknown key %s", words[1]); return; }
        if (_stricmp(verb, "keyup")) Key(vk, true);
        if (!_stricmp(verb, "key")) { Sleep(count > 2 ? atoi(words[2]) : 60); Key(vk, false); }
        if (!_stricmp(verb, "keyup")) Key(vk, false);
        return;
    }
    if (!_stricmp(verb, "text") && count > 1) {
        for (int w = 1; w < count; ++w) {
            for (const char* c = words[w]; *c; ++c) g_post(g_window, WM_CHAR, (WPARAM)(unsigned char)*c, 1);
            if (w + 1 < count) g_post(g_window, WM_CHAR, ' ', 1);
        }
        return;
    }
    if (!_stricmp(verb, "move") && count > 2) { MoveTo(atoi(words[1]), atoi(words[2])); return; }
    if (!_stricmp(verb, "click") && count > 2) {
        bool right = count > 3 && !_stricmp(words[3], "right");
        int hold = count > 3 + right ? atoi(words[3 + right]) : 60;
        MoveTo(atoi(words[1]), atoi(words[2]));
        Sleep(30);
        Mouse(right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN, right ? MK_RBUTTON : MK_LBUTTON);
        Sleep(hold);
        Mouse(right ? WM_RBUTTONUP : WM_LBUTTONUP, 0);
        return;
    }
    if (!_stricmp(verb, "wheel") && count > 1) {
        POINT p{g_cursorX, g_cursorY};
        g_clientToScreen(g_window, &p);
        g_post(g_window, WM_MOUSEWHEEL, MAKEWPARAM(0, (short)(atoi(words[1]) * WHEEL_DELTA)), MAKELPARAM(p.x, p.y));
        return;
    }
    if (!_stricmp(verb, "pad") && count > 1) {
        if (!HookPad()) { strcpy_s(reply, size, "err XInput not loaded"); return; }
        for (auto& b : kPad) {
            if (_stricmp(words[1], b.name)) continue;
            if (b.trigger) InterlockedOr(&g_padTriggers, b.trigger); else InterlockedOr(&g_padButtons, b.bit);
            InterlockedIncrement(&g_padPacket);
            Sleep(count > 2 ? atoi(words[2]) : 120);
            if (b.trigger) InterlockedAnd(&g_padTriggers, ~b.trigger); else InterlockedAnd(&g_padButtons, ~(LONG)b.bit);
            InterlockedIncrement(&g_padPacket);
            return;
        }
        sprintf_s(reply, size, "err unknown pad button %s", words[1]);
        return;
    }
    sprintf_s(reply, size, "err unknown command %s", verb);
}

// The game's main window: a visible UnrealWindow of this process.
static BOOL CALLBACK FindWindowOf(HWND window, LPARAM found) {
    static ThreadProcessFn* threadProcess;
    static VisibleFn* visible;
    static ClassNameFn* className;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!threadProcess) {
        threadProcess = (ThreadProcessFn*)GetProcAddress(user32, "GetWindowThreadProcessId");
        visible = (VisibleFn*)GetProcAddress(user32, "IsWindowVisible");
        className = (ClassNameFn*)GetProcAddress(user32, "GetClassNameW");
    }
    DWORD process = 0;
    threadProcess(window, &process);
    wchar_t name[64] = {};
    if (process == GetCurrentProcessId() && visible(window) && className(window, name, 64) && !wcscmp(name, L"UnrealWindow")) {
        *(HWND*)found = window;
        return FALSE;
    }
    return TRUE;
}

static DWORD WINAPI Server(void*) {
    HMODULE user32 = NULL;
    for (int i = 0; i < 600 && !(user32 = GetModuleHandleW(L"user32.dll")); ++i) Sleep(100);
    if (!user32) { Log("Agent: user32 not loaded"); return 1; }
    g_post = (PostFn*)GetProcAddress(user32, "PostMessageW");
    g_mapKey = (MapKeyFn*)GetProcAddress(user32, "MapVirtualKeyW");
    g_clientToScreen = (ClientToScreenFn*)GetProcAddress(user32, "ClientToScreen");
    g_callProc = (CallProcFn*)GetProcAddress(user32, "CallWindowProcW");
    auto enumWindows = (EnumFn*)GetProcAddress(user32, "EnumWindows");
    auto setLong = (SetLongFn*)GetProcAddress(user32, "SetWindowLongPtrW");
    MH_STATUS status = MH_Initialize();
    if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED) {
        MH_CreateHook((void*)GetProcAddress(user32, "GetForegroundWindow"), (void*)H_foreground, (void**)&g_foreground);
        MH_CreateHook((void*)GetProcAddress(user32, "GetActiveWindow"), (void*)H_active, (void**)&g_active);
        MH_CreateHook((void*)GetProcAddress(user32, "GetCursorPos"), (void*)H_cursor, (void**)&g_cursor);
        status = MH_EnableHook(MH_ALL_HOOKS);
    }
    Log("Agent: focus hooks %s", MH_StatusToString(status));
    // The window appears once the engine has started.
    for (int i = 0; i < 1200 && !g_window; ++i) { enumWindows(FindWindowOf, (LPARAM)&g_window); if (!g_window) Sleep(100); }
    if (g_window) {
        g_windowProc = (WNDPROC)setLong(g_window, GWLP_WNDPROC, (LONG_PTR)H_windowProc);
        g_post(g_window, WM_ACTIVATEAPP, TRUE, 0);
    }
    Log("Agent: game window %p; commands on \\\\.\\pipe\\ac8_agent", g_window);
    for (;;) {
        HANDLE pipe = CreateNamedPipeW(L"\\\\.\\pipe\\ac8_agent", PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, NULL);
        if (pipe == INVALID_HANDLE_VALUE) { Log("Agent: pipe not created (%lu)", GetLastError()); return 1; }
        if (ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            char buffer[1024]; int used = 0; DWORD got;
            while (ReadFile(pipe, buffer + used, (DWORD)(sizeof(buffer) - 1 - used), &got, NULL) && got) {
                used += got;
                buffer[used] = 0;
                char* start = buffer;
                for (char* end; (end = strchr(start, '\n')); start = end + 1) {
                    *end = 0;
                    char reply[160];
                    Run(start, reply, sizeof(reply));
                    strcat_s(reply, "\n");
                    DWORD wrote;
                    WriteFile(pipe, reply, (DWORD)strlen(reply), &wrote, NULL);
                }
                used = (int)(buffer + used - start);
                memmove(buffer, start, used);
                if (used >= (int)sizeof(buffer) - 1) used = 0; // an over-long line is dropped
            }
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

static void Start() {
    HANDLE thread = CreateThread(NULL, 0, Server, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
}

} // namespace agent
