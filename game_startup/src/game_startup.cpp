// Standalone, GPU-independent startup mod. No DLSS/Streamline/graphics hooks.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <cstring>
#include <cwchar>
#include "MinHook.h"

static WCHAR g_directory[MAX_PATH], g_config[MAX_PATH], g_log[MAX_PATH];
static bool g_logging;
static SRWLOCK g_logLock = SRWLOCK_INIT;

static int Setting(const WCHAR* path, const WCHAR* section, const WCHAR* key, int fallback, int low, int high) {
    WCHAR value[64]; GetPrivateProfileStringW(section, key, L"", value, 64, path);
    WCHAR* end = NULL;
    if (!value[0]) return fallback;
    long parsed = wcstol(value, &end, 10);
    if (end == value || *end || parsed < low || parsed > high) return fallback;
    return (int)parsed;
}
static void Log(const char* format, ...) {
    if (!g_logging) return;
    AcquireSRWLockExclusive(&g_logLock);
    FILE* file = NULL;
    if (!_wfopen_s(&file, g_log, L"a") && file) {
        fprintf(file, "pid=%lu tick=%llu ", GetCurrentProcessId(), GetTickCount64());
        va_list args; va_start(args, format); vfprintf(file, format, args); va_end(args);
        fputc('\n', file); fclose(file);
    }
    ReleaseSRWLockExclusive(&g_logLock);
}

#include "../../dlss_mfg_sm86/src/ue_reflection.cpp"
#include "aveum.cpp"

namespace startup_host {
static DWORD g_gameThread;
static thread_local bool g_inside;

// Verified roots for this Aveum build, used only when they validate against the
// same engine metadata as the generic scanner. Different builds fall back to
// discovery. This avoids scanning the protected image for 20+ seconds while
// the first warning is already being shown. Addresses are RVAs, not patches.
static void PrimeAveumRoots() {
    if (ue::g_objects || ue::g_nameBlocks) return;
    __try {
        char* image = (char*)GetModuleHandleW(NULL);
        auto dos = (IMAGE_DOS_HEADER*)image;
        auto nt = (IMAGE_NT_HEADERS*)(image + dos->e_lfanew);
        const size_t objects = 0x8d27e60, names = 0x8c88410;
        if (nt->OptionalHeader.SizeOfImage < objects + 0x30 || nt->OptionalHeader.SizeOfImage < names + 8) return;
        if (!ue::LooksLikeObjectArray(image + objects) || !ue::LooksLikeNameBlocks(image + names)) return;
        ue::g_objects = image + objects;
        ue::g_nameBlocks = (char**)(image + names);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void ProcessEvent(void* object, void* function, void* parameters) {
    // Restrict reflected startup work to known game-thread widget events. The
    // class tables can also be invoked by async loading/animation threads.
    if (!g_gameThread && object && !ue::Template(object)
        && (ue::Is(function, "Construct") || ue::Is(function, "Tick") || ue::Is(function, "OnInitialized"))
        && (startup_aveum::Warning(object) || ue::Is(ue::ClassOf(object), "wbp_StartGame_C")))
        InterlockedCompareExchange((volatile LONG*)&g_gameThread, (LONG)GetCurrentThreadId(), 0);
    bool gameThread = g_gameThread == GetCurrentThreadId();
    if (gameThread && !g_inside) {
        g_inside = true;
        startup_aveum::Before(object, function);
        ue::g_processEvent(object, function, parameters);
        startup_aveum::Tick();
        g_inside = false;
    } else {
        if (gameThread) startup_aveum::Before(object, function);
        ue::g_processEvent(object, function, parameters);
    }
}

static void PatchTables() {
    for (int i = 0, n = ue::Count(); i < n; ++i) {
        void* object = ue::At(i, NULL);
        void** table = object ? ue::Read<void**>(object, 0) : NULL;
        if (!table || table[ue::g_processEventIndex] != (void*)ue::g_processEvent) continue;
        void** slot = table + ue::g_processEventIndex;
        DWORD old;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) continue;
        InterlockedExchangePointer(slot, (void*)ProcessEvent);
        VirtualProtect(slot, sizeof(void*), old, &old);
    }
}
static void Run() {
    ULONGLONG start = GetTickCount64();
    bool ready = false;
    for (int i = 0; i < 120 && !ready; ++i) {
        PrimeAveumRoots();
        if (!ue::g_objects && i < 10) { Sleep(500); continue; }
        ready = ue::Init();
        if (!ready) Sleep(1000);
    }
    if (!ready) { Log("Startup reflection unavailable; UI left as shipped"); return; }
    Log("Startup reflection ready after %llu ms", GetTickCount64() - start);
    PatchTables();
    // Newly loaded widget classes have different virtual tables. This worker
    // only installs routing; it never executes reflected functions itself.
    while (!InterlockedCompareExchange(&startup_aveum::g_done, 0, 0)) { Sleep(1000); PatchTables(); }
}
} // namespace startup_host

static DWORD WINAPI Worker(void*) {
    HMODULE pinned;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)Worker, &pinned);
    wcscpy_s(g_config, g_directory); wcscat_s(g_config, L"game_startup.ini");
    wcscpy_s(g_log, g_directory); wcscat_s(g_log, L"game_startup.log");
    g_logging = Setting(g_config, L"Logging", L"Enabled", 1, 0, 1) != 0;
    WCHAR executable[MAX_PATH];
    if (!GetModuleFileNameW(NULL, executable, MAX_PATH)) return 0;
    if (!startup_aveum::IsGame(executable)) return 0;
    startup_aveum::Start(executable);
    if (startup_aveum::g_offline || startup_aveum::g_skipWarnings) startup_host::Run();
    return 0;
}

#ifndef GAME_STARTUP_TEST
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(instance);
    DWORD length = GetModuleFileNameW(instance, g_directory, MAX_PATH);
    if (!length || length >= MAX_PATH) return FALSE;
    WCHAR* slash = wcsrchr(g_directory, L'\\');
    if (!slash) return FALSE;
    slash[1] = 0;
    HANDLE thread = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    return TRUE;
}
#endif
