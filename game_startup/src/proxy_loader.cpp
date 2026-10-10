#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>

static WCHAR g_mod[MAX_PATH];
static DWORD WINAPI LoadStartup(void*) {
    LoadLibraryExW(g_mod, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(instance);
    DWORD length = GetModuleFileNameW(instance, g_mod, MAX_PATH);
    if (!length || length >= MAX_PATH) return FALSE;
    WCHAR* slash = wcsrchr(g_mod, L'\\');
    if (!slash) return FALSE;
    slash[1] = 0; wcscat_s(g_mod, L"game_startup.dll");
    HANDLE thread = CreateThread(NULL, 0, LoadStartup, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    return TRUE;
}
