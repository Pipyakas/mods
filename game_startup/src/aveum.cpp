// Optional startup conveniences, only for Immortals of Aveum. Included by the
// standalone startup DLL after ue_reflection.cpp. Never changes network/account, save,
// game files or story movies. Shared [Startup] keys default to zero.
#include <winternl.h>

namespace startup_aveum {

static bool g_offline, g_skipIntro, g_skipWarnings;
static volatile LONG g_done;
static WCHAR g_movies[MAX_PATH];
static ue::Ref g_services, g_titleHandled, g_failureHandled;

static bool IsGame(const WCHAR* executable) {
    const WCHAR* name = wcsrchr(executable, L'\\');
    return !_wcsicmp(name ? name + 1 : executable, L"ImmortalsOfAveum-Win64-Shipping.exe");
}

// Match the exact three MoviePlayerSettings startup entries, in this game's
// Content/Movies directory. A movie with the same basename elsewhere is untouched.
static bool IsLogo(const WCHAR* path, const WCHAR* movies) {
    if (!path || !movies[0]) return false;
    if (!wcsncmp(path, L"\\??\\", 4) || !wcsncmp(path, L"\\\\?\\", 4)) path += 4;
    WCHAR full[MAX_PATH];
    DWORD length = GetFullPathNameW(path, MAX_PATH, full, NULL);
    if (!length || length >= MAX_PATH) return false;
    for (WCHAR* c = full; *c; ++c) if (*c == L'/') *c = L'\\';
    size_t prefix = wcslen(movies);
    if (_wcsnicmp(full, movies, prefix)) return false;
    const WCHAR* file = full + prefix;
    return !_wcsicmp(file, L"companylogo.mp4") || !_wcsicmp(file, L"publisherlogo.mp4")
        || !_wcsicmp(file, L"enginelogo.mp4");
}

typedef HANDLE (WINAPI* CreateFileFn)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef DWORD (WINAPI* AttributesFn)(LPCWSTR);
typedef BOOL (WINAPI* AttributesExFn)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
static CreateFileFn g_createFile;
static AttributesFn g_attributes;
static AttributesExFn g_attributesEx;
typedef NTSTATUS (NTAPI* NtCreateFileFn)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
    PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
static NtCreateFileFn g_ntCreateFile;

static bool ReadOnlyAccess(DWORD access) {
    return !(access & (GENERIC_WRITE | GENERIC_ALL | MAXIMUM_ALLOWED | DELETE | WRITE_DAC | WRITE_OWNER
        | FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES));
}

static bool SuppressLogo(const WCHAR* path) {
    if (!g_skipIntro || !IsLogo(path, g_movies)) return false;
    static volatile LONG reported;
    const WCHAR* file = wcsrchr(path, L'\\');
    const WCHAR* slash = wcsrchr(path, L'/');
    if (!file || (slash && slash > file)) file = slash;
    file = file ? file + 1 : path;
    LONG bit = !_wcsicmp(file, L"companylogo.mp4") ? 1 : !_wcsicmp(file, L"publisherlogo.mp4") ? 2 : 4;
    if (!(InterlockedOr(&reported, bit) & bit)) Log("Startup Aveum: skipped %ls", file);
    return true;
}

static HANDLE WINAPI H_createFile(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
    DWORD disposition, DWORD flags, HANDLE file) {
    if (disposition == OPEN_EXISTING && ReadOnlyAccess(access)
        && SuppressLogo(path)) {
        SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE;
    }
    return g_createFile(path, access, share, security, disposition, flags, file);
}
static DWORD WINAPI H_attributes(LPCWSTR path) {
    if (SuppressLogo(path)) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_FILE_ATTRIBUTES; }
    return g_attributes(path);
}
static BOOL WINAPI H_attributesEx(LPCWSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID info) {
    if (SuppressLogo(path)) { SetLastError(ERROR_FILE_NOT_FOUND); return FALSE; }
    return g_attributesEx(path, level, info);
}

// Media Foundation can open the startup MP4s without going through CreateFileW.
// Only absolute, read-only FILE_OPEN requests for those same three paths match.
static NTSTATUS NTAPI H_ntCreateFile(PHANDLE file, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
    PIO_STATUS_BLOCK result, PLARGE_INTEGER allocation, ULONG fileAttributes, ULONG share,
    ULONG disposition, ULONG options, PVOID ea, ULONG eaSize) {
    if (g_skipIntro && disposition == 1 && ReadOnlyAccess(access)
        && attributes && !attributes->RootDirectory && attributes->ObjectName) {
        UNICODE_STRING* name = attributes->ObjectName;
        if (name->Buffer && name->Length % sizeof(WCHAR) == 0 && name->Length < MAX_PATH * sizeof(WCHAR)) {
            WCHAR path[MAX_PATH];
            memcpy(path, name->Buffer, name->Length); path[name->Length / sizeof(WCHAR)] = 0;
            if (SuppressLogo(path)) {
                const NTSTATUS notFound = (NTSTATUS)0xC0000034L;
                if (result) { result->Status = notFound; result->Information = 0; }
                return notFound;
            }
        }
    }
    return g_ntCreateFile(file, access, attributes, result, allocation, fileAttributes, share,
        disposition, options, ea, eaSize);
}

static void Hook(HMODULE kernel, const char* name, void* detour, void** original) {
    void* target = (void*)GetProcAddress(kernel, name);
    MH_STATUS status = target ? MH_CreateHook(target, detour, original) : MH_ERROR_FUNCTION_NOT_FOUND;
    if (status == MH_OK) status = MH_EnableHook(target);
    Log("Startup Aveum: %s %s", name, MH_StatusToString(status));
}

// Runs before waiting for Streamline. UE's native MoviePlayer starts before
// game-thread reflection is available, so suppress its file lookups early.
static void Start(const WCHAR* executable) {
    if (!IsGame(executable)) return;
    g_offline = Setting(g_config, L"Startup", L"AutoOffline", 0, 0, 1) != 0;
    g_skipIntro = Setting(g_config, L"Startup", L"SkipIntro", 0, 0, 1) != 0;
    g_skipWarnings = Setting(g_config, L"Startup", L"SkipWarnings", 0, 0, 1) != 0;
    if (!g_offline && !g_skipIntro && !g_skipWarnings) return;
    Log("Startup Aveum: AutoOffline=%d SkipIntro=%d SkipWarnings=%d (restart to change)", g_offline, g_skipIntro, g_skipWarnings);
    if (!g_skipIntro) return;
    WCHAR path[MAX_PATH]; wcscpy_s(path, executable);
    WCHAR* name = wcsrchr(path, L'\\');
    if (!name) return;
    name[1] = 0;
    wcscat_s(path, L"..\\..\\Content\\Movies\\");
    DWORD length = GetFullPathNameW(path, MAX_PATH, g_movies, NULL);
    if (!length || length >= MAX_PATH) { g_movies[0] = 0; return; }
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        Log("Startup Aveum: hook initialization %s", MH_StatusToString(status)); return;
    }
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    Hook(kernel, "GetFileAttributesW", (void*)H_attributes, (void**)&g_attributes);
    Hook(kernel, "GetFileAttributesExW", (void*)H_attributesEx, (void**)&g_attributesEx);
    Hook(kernel, "CreateFileW", (void*)H_createFile, (void**)&g_createFile);
    Hook(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile", (void*)H_ntCreateFile, (void**)&g_ntCreateFile);
}

static bool Visible(void* widget) {
    ue::Call call(widget, "IsVisible");
    return call.Run() && call.Result<bool>("ReturnValue");
}

static bool Warning(void* object) {
    return object && (ue::Is(ue::ClassOf(object), "WBP_SeizureWarning_C")
        || ue::Is(ue::ClassOf(object), "WBP_Startup_SaveWarning_C"));
}

// Keep the game's completion delegates/timers, but never paint the warning.
// A tiny positive duration is intentional: Unreal disables timers at zero.
static void Before(void* object, void* function) {
    if (!g_skipWarnings || g_done || !Warning(object) || !ue::Is(function, "Construct")) return;
    if (ue::Is(ue::ClassOf(object), "WBP_SeizureWarning_C")) ue::Set(object, "Duration", 0.001f);
    else ue::Set(object, "Duration", 0.001);
    ue::Call(object, "SetVisibility").Arg("InVisibility", (uint8_t)1).Run();
    Log("Startup Aveum: skipped %s", ue::Is(ue::ClassOf(object), "WBP_SeizureWarning_C") ? "seizure warning" : "save warning");
}

static bool EnsureOffline() {
    static void* type;
    if (!type) type = ue::FindClass("GameServicesSubsystem");
    if (!ue::Alive(g_services)) g_services = ue::Track(ue::FindInstance(type));
    if (!ue::Alive(g_services)) return false;
    ue::Call state(g_services.object, "IsOfflineModeEnabled");
    if (!state.Run()) return false;
    if (state.Result<bool>("ReturnValue")) return true;
    ue::Call enable(g_services.object, "SetOfflineModeEnabled");
    if (!enable.Ok() || ue::Offset(enable.function, "bEnabled") < 0) return false;
    if (!enable.Arg("bEnabled", true).Run()) return false;
    Log("Startup Aveum: selected the game's offline mode");
    return true;
}

// Runs on the game thread. Let the game's own start/login flow advance after
// license initialization; never bypass licenses/EULA or start a saved game.
static void Tick() {
    if (g_done) return;
    static ULONGLONG next;
    ULONGLONG now = GetTickCount64();
    if (now < next) return;
    next = now + 500;
    static void* failedType, *titleType, *menuType;
    if (!failedType) failedType = ue::FindClass("WBP_Login_Failed_C");
    if (!titleType) titleType = ue::FindClass("wbp_StartGame_C");
    if (!menuType) menuType = ue::FindClass("WBP_MainMenu_C");
    void* mainMenu = ue::FindInstance(menuType);
    if (mainMenu && Visible(mainMenu)) {
        InterlockedExchange(&g_done, 1); // Later explicit online choices are not overridden.
        Log("Startup Aveum: main menu reached; startup automation finished");
        return;
    }
    if (!g_offline) return;
    if (!EnsureOffline()) return;
    void* failed = ue::FindInstance(failedType);
    if (failed && Visible(failed) && (!ue::Alive(g_failureHandled) || g_failureHandled.object != failed)) {
        ue::Call click(failed, "BndEvt__WBP_Login_Failed_OfflineButton_K2Node_ComponentBoundEvent_3_OnClicked__DelegateSignature");
        if (click.Ok()) {
            g_failureHandled = ue::Track(failed); // before calling: the handler may re-enter
            click.Run();
            Log("Startup Aveum: advanced past Play Offline dialog");
        }
    }
    void* title = ue::FindInstance(titleType);
    if (title && Visible(title) && ue::Get<bool>(title, "bInitializedGameLicense", false)
        && (!ue::Alive(g_titleHandled) || g_titleHandled.object != title)) {
        ue::Call start(title, "TryLoginProcess");
        if (start.Ok()) {
            g_titleHandled = ue::Track(title);
            start.Run();
            Log("Startup Aveum: advanced title through the offline startup flow");
        }
    }
}

} // namespace startup_aveum
