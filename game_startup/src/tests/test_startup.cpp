#define GAME_STARTUP_TEST
#include <initializer_list>
#include "../game_startup.cpp"

static int TestStartup() {
    using namespace startup_aveum;
    if (!ReadOnlyAccess(GENERIC_READ) || !ReadOnlyAccess(FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE)
        || ReadOnlyAccess(GENERIC_ALL) || ReadOnlyAccess(GENERIC_WRITE) || ReadOnlyAccess(MAXIMUM_ALLOWED)
        || ReadOnlyAccess(FILE_WRITE_DATA) || ReadOnlyAccess(FILE_APPEND_DATA) || ReadOnlyAccess(FILE_WRITE_ATTRIBUTES)
        || ReadOnlyAccess(DELETE) || ReadOnlyAccess(WRITE_DAC) || ReadOnlyAccess(WRITE_OWNER)) return 1;
    if (!IsGame(L"D:\\game\\ImmortalsOfAveum-Win64-Shipping.exe")
        || !IsGame(L"IMMORTALSOFAVEUM-WIN64-SHIPPING.EXE")
        || IsGame(L"D:\\game\\NotImmortalsOfAveum-Win64-Shipping.exe")
        || IsGame(L"D:\\ImmortalsOfAveum-Win64-Shipping.exe\\other.exe")) return 2;
    const WCHAR* movies = L"D:\\test\\P3\\Content\\Movies\\";
    if (!IsLogo(L"D:\\test\\P3\\Content\\Movies\\companylogo.mp4", movies)
        || !IsLogo(L"D:/test/P3/Content/Movies/PUBLISHERLOGO.MP4", movies)
        || !IsLogo(L"D:\\test\\P3\\Content\\Movies\\enginelogo.mp4", movies)
        || !IsLogo(L"\\??\\D:\\test\\P3\\Content\\Movies\\companylogo.mp4", movies)
        || !IsLogo(L"\\\\?\\D:\\test\\P3\\Content\\Movies\\enginelogo.mp4", movies)
        || !IsLogo(L"D:\\test\\P3\\Content\\Movies\\child\\..\\enginelogo.mp4", movies)) return 3;
    if (IsLogo(NULL, movies) || IsLogo(L"D:\\test\\P3\\Content\\Movies\\companylogo.mp4", L"")
        || IsLogo(L"D:\\other\\P3\\Content\\Movies\\companylogo.mp4", movies)
        || IsLogo(L"D:\\test\\P3\\Content\\MoviesElsewhere\\companylogo.mp4", movies)
        || IsLogo(L"D:\\test\\P3\\Content\\Movies\\..\\companylogo.mp4", movies)
        || IsLogo(L"D:\\test\\P3\\Content\\Movies\\companylogo.mp4.bak", movies)
        || IsLogo(L"D:\\test\\P3\\Content\\Movies\\IGC\\companylogo.mp4", movies)
        || IsLogo(L"D:\\test\\P3\\Content\\Movies\\blackscreen.mp4", movies)
        || IsLogo(L"D:\\test\\P3\\Content\\Movies\\PalathonFalls.mp4", movies)) return 4;
    WCHAR temp[MAX_PATH], path[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temp) || !GetTempFileNameW(temp, L"gst", 0, path)) return 5;
    int result = 0;
    for (const WCHAR* key : {L"AutoOffline", L"SkipIntro", L"SkipWarnings"}) {
        if (Setting(path, L"Startup", key, 0, 0, 1) != 0) result = 6;
        WritePrivateProfileStringW(L"Startup", key, L"1", path);
        if (Setting(path, L"Startup", key, 0, 0, 1) != 1) result = 7;
        WritePrivateProfileStringW(L"Startup", key, L"garbage", path);
        if (Setting(path, L"Startup", key, 0, 0, 1) != 0) result = 8;
    }
    DeleteFileW(path);
    return result;
}

int main() {
    int result = TestStartup();
    if (result) { printf("FAIL: startup test %d\n", result); return result; }
    puts("PASS: opt-in defaults, invalid config, game/NT/logo path allowlist, story/write exclusions");
    return 0;
}
