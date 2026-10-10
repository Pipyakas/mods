// Streamline 1.x bridge, included by dlss_mfg_sm86.cpp.
//
// Games built on Streamline 1.x (Returnal: 1.3.3) can't take the 2.14.1 runtime:
// their engine plugin calls the 1.x API, which 2.x removed, and it checks NVIDIA's
// signature on sl.interposer.dll, so the file can't be replaced either. When a
// 2.14.1 runtime sits in an "sl2" folder beside the game's 1.x interposer, the
// bridge loads it and turns the 1.x interposer's exports into calls to it:
// DLSS-G mode, Reflex options and markers, resource tags, camera constants, and
// the D3D12 device / DXGI factory that carry Streamline's present hooks. The 1.x
// runtime itself is never initialized. The DLSS-G hooks then see a 2.x game.
// Without the sl2 folder nothing is hooked and the game runs as shipped.
//
// The 1.x layouts below were checked against Returnal's live calls.

#include <unknwn.h>
#include "sl_pcl.h"

namespace sl1 {

enum : uint32_t { kFeatureReflex = 3, kFeatureDLSS_G = 1000, kMarkerSleep = 0x1000 };

struct Resource {
    uint32_t type; void* native; void* memory; void* view; uint32_t state; void* ext;
};
struct Extent { uint32_t top, left, width, height; };
struct Constants {
    float matrices[5][16];
    float jitter[2], mvecScale[2], pinhole[2], pos[3], up[3], right[3], fwd[3];
    float cameraNear, cameraFar, cameraFOV, cameraAspectRatio, motionVectorsInvalidValue;
    char depthInverted, cameraMotionIncluded, motionVectors3D, reset, notRenderingGameFrames,
        orthographicProjection, motionVectorsDilated, motionVectorsJittered;
    void* ext;
};
struct DLSSGConstants { uint32_t mode, numFramesToGenerate; };
// Only the fields the bridge writes; the rest of the caller's structure is kept.
// frameTimeMs is the displayed frame time (Returnal's overlay shows its FPS).
struct DLSSGSettings { uint64_t estimatedVRAMUsageInBytes; float frameTimeMs; uint32_t status; };
struct ReflexConstants { uint32_t mode, frameLimitUs; bool useMarkersToOptimize; uint16_t virtualKey; void* ext; };
struct ReflexSettings { bool lowLatencyAvailable, latencyReportAvailable; uint32_t statsWindowMessage; };

static_assert(offsetof(Resource, native) == 8 && offsetof(Resource, state) == 32, "1.x resource");
static_assert(offsetof(Constants, jitter) == 320 && offsetof(Constants, cameraNear) == 392, "1.x constants");
static_assert(offsetof(Constants, depthInverted) == 412 && sizeof(Constants) == 432, "1.x constants");
static_assert(offsetof(ReflexConstants, virtualKey) == 10, "1.x reflex constants");
static_assert(offsetof(sl::Constants, motionVectorsInvalidValue) - offsetof(sl::Constants, cameraViewToClip)
    == offsetof(Constants, motionVectorsInvalidValue), "1.x and 2.x camera blocks differ");

typedef bool Init(const void* preferences, int applicationId);
typedef bool Shutdown();
typedef bool SetEnabled(uint32_t feature, bool enabled);
typedef bool IsEnabled(uint32_t feature);
typedef bool IsSupported(uint32_t feature, uint32_t* adapterMask);
typedef bool SetTag(const Resource* resource, uint32_t tag, uint32_t id, const Extent* extent);
typedef bool SetConstants(const Constants& values, uint32_t frameIndex, uint32_t id);
typedef bool SetFeatureConstants(uint32_t feature, const void* consts, uint32_t frameIndex, uint32_t id);
typedef bool GetFeatureSettings(uint32_t feature, const void* consts, void* settings);
typedef bool Evaluate(void* commandList, uint32_t feature, uint32_t frameIndex, uint32_t id);
typedef bool Allocate(void* commandList, uint32_t feature, uint32_t id);
typedef bool Free(uint32_t feature, uint32_t id);
typedef bool Upgrade(void** object);
typedef HRESULT WINAPI CreateDevice(void* adapter, int level, REFIID riid, void** device);
typedef HRESULT WINAPI CreateFactory(REFIID riid, void** factory);
typedef HRESULT WINAPI CreateFactory2(UINT flags, REFIID riid, void** factory);

} // namespace sl1

namespace bridge {

static WCHAR g_folder[MAX_PATH];      // the 2.14.1 runtime, "<1.x interposer folder>\sl2\"
static volatile LONG g_armed = 0;     // the 1.x exports are hooked
static volatile LONG g_decided = 0;   // the 1.x interposer was seen (armed or not)
static volatile float g_frameMs = 0;  // rendered frame interval, smoothed
static HMODULE g_modern;              // set once the 2.14.1 interposer is loaded
static SRWLOCK g_lock = SRWLOCK_INIT;
static BOOL g_failed = FALSE;

// 1.x originals (MinHook trampolines), used only if the 2.x runtime fails to load.
static sl1::Init* o_init; static sl1::Shutdown* o_shutdown; static sl1::SetEnabled* o_setEnabled;
static sl1::IsEnabled* o_isEnabled; static sl1::IsSupported* o_isSupported; static sl1::SetTag* o_setTag;
static sl1::SetConstants* o_setConstants; static sl1::SetFeatureConstants* o_setFeatureConstants;
static sl1::GetFeatureSettings* o_getSettings; static sl1::Evaluate* o_evaluate;
static sl1::Allocate* o_allocate; static sl1::Free* o_free; static sl1::Upgrade* o_upgrade;
static sl1::CreateDevice* o_device; static sl1::CreateFactory* o_factory; static sl1::CreateFactory* o_factory1;
static sl1::CreateFactory2* o_factory2;

// 2.14.1 entry points.
static PFun_slInit* m_init; static PFun_slShutdown* m_shutdown; static PFun_slSetFeatureLoaded* m_setLoaded;
static PFun_slIsFeatureLoaded* m_isLoaded; static PFun_slIsFeatureSupported* m_isSupported;
#pragma warning(suppress: 4996) // the frame-less slSetTag matches 1.x tagging
static PFun_slSetTag* m_setTag; static PFun_slSetConstants* m_setConstants;
static PFun_slGetNewFrameToken* m_token; static PFun_slAllocateResources* m_allocate;
static PFun_slFreeResources* m_free; static PFun_slUpgradeInterface* m_upgrade;
static PFun_slGetFeatureFunction* m_feature; static PFun_slSetD3DDevice* m_setDevice;
static HRESULT (WINAPI* s_createDevice)(void*, int, REFIID, void**); // System32 d3d12
static sl1::CreateFactory* m_factory; static sl1::CreateFactory* m_factory1;
static sl1::CreateFactory2* m_factory2;
static PFun_slDLSSGGetState* m_dlssgState;
static PFun_slReflexSetOptions* m_reflexOptions; static PFun_slReflexSleep* m_reflexSleep;
static PFun_slReflexGetState* m_reflexState; static PFun_slPCLSetMarker* m_marker;

static void ModernLog(sl::LogType type, const char* message) {
    if (type != sl::LogType::eInfo) Log("SL2 %s", message);
}

// Loads and initializes the 2.14.1 runtime on the game's first call. FALSE (and
// every later call falls back to the 1.x runtime) if any step fails.
static BOOL Modern(int applicationId) {
    AcquireSRWLockExclusive(&g_lock);
    if (!g_modern && !g_failed) {
        WCHAR path[MAX_PATH];
        wcscpy_s(path, g_folder); wcscat_s(path, L"sl.interposer.dll");
        HMODULE module = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
#define BRIDGE_LOAD(name, target) target = module ? (decltype(target))GetProcAddress(module, name) : NULL
        BRIDGE_LOAD("slInit", m_init); BRIDGE_LOAD("slShutdown", m_shutdown);
        BRIDGE_LOAD("slSetFeatureLoaded", m_setLoaded); BRIDGE_LOAD("slIsFeatureLoaded", m_isLoaded);
        BRIDGE_LOAD("slIsFeatureSupported", m_isSupported); BRIDGE_LOAD("slSetTag", m_setTag);
        BRIDGE_LOAD("slSetConstants", m_setConstants); BRIDGE_LOAD("slGetNewFrameToken", m_token);
        BRIDGE_LOAD("slAllocateResources", m_allocate); BRIDGE_LOAD("slFreeResources", m_free);
        BRIDGE_LOAD("slUpgradeInterface", m_upgrade); BRIDGE_LOAD("slGetFeatureFunction", m_feature);
        BRIDGE_LOAD("slSetD3DDevice", m_setDevice);
        BRIDGE_LOAD("CreateDXGIFactory", m_factory);
        BRIDGE_LOAD("CreateDXGIFactory1", m_factory1); BRIDGE_LOAD("CreateDXGIFactory2", m_factory2);
#undef BRIDGE_LOAD
        HMODULE d3d12 = LoadLibraryExW(L"d3d12.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        *(void**)&s_createDevice = d3d12 ? (void*)GetProcAddress(d3d12, "D3D12CreateDevice") : NULL;
        sl::Result result = sl::Result::eErrorMissingOrInvalidAPI;
        if (s_createDevice && m_init && m_shutdown && m_setLoaded && m_isLoaded && m_isSupported && m_setTag && m_setConstants
            && m_token && m_allocate && m_free && m_upgrade && m_feature && m_setDevice && m_factory
            && m_factory1 && m_factory2) {
            const sl::Feature features[] = {sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL};
            const wchar_t* plugins[] = {g_folder};
            sl::Preferences options{};
            options.logLevel = sl::LogLevel::eDefault;
            options.logMessageCallback = ModernLog;
            options.pathsToPlugins = plugins; options.numPathsToPlugins = 1;
            options.flags = sl::PreferenceFlags::eDisableCLStateTracking;
            options.featuresToLoad = features; options.numFeaturesToLoad = 3;
            options.applicationId = (uint32_t)applicationId;
            options.engine = sl::EngineType::eUnreal;
            options.renderAPI = sl::RenderAPI::eD3D12;
            result = m_init(options, sl::kSDKVersion);
        }
        Log("Streamline1 bridge: 2.14.1 runtime %s, slInit result=%d", module ? "loaded" : "missing", (int)result);
        if (result == sl::Result::eOk) g_modern = module;
        else g_failed = TRUE;
    }
    BOOL ready = g_modern != NULL;
    ReleaseSRWLockExclusive(&g_lock);
    return ready;
}

static BOOL Ready() { return g_modern != NULL; }
static void EnsureDevice();

template <typename T> static T* Feature(T*& cached, sl::Feature feature, const char* name) {
    if (!cached) {
        void* function = NULL;
        if (m_feature(feature, name, function) == sl::Result::eOk) cached = (T*)function;
        // Reflex options pass the proxy's hook like a 2.x game's ([Reflex]
        // Boost / Mode, menu choices); calls through `cached` reach it.
        if (cached && !strcmp(name, "slReflexSetOptions")) ::Install(name, function);
    }
    return cached;
}

// The engine numbers 1.x frames itself; 2.x wants a token per frame. Game and
// render threads work on different frames at once, so keep a few.
static const sl::FrameToken* Token(uint32_t frameIndex) {
    static struct { uint32_t index; sl::FrameToken* token; } ring[8] = {};
    static SRWLOCK lock = SRWLOCK_INIT;
    auto& slot = ring[frameIndex % 8];
    AcquireSRWLockExclusive(&lock);
    if (!slot.token || slot.index != frameIndex) {
        sl::FrameToken* token = NULL;
        if (m_token(token, &frameIndex) == sl::Result::eOk) { slot.token = token; slot.index = frameIndex; }
    }
    sl::FrameToken* token = slot.index == frameIndex ? slot.token : NULL;
    ReleaseSRWLockExclusive(&lock);
    return token;
}

static bool H_init(const void* preferences, int applicationId) {
    if (Modern(applicationId)) return true;
    return o_init(preferences, applicationId);
}
static bool H_shutdown() { return Ready() ? m_shutdown() == sl::Result::eOk : o_shutdown(); }
static bool H_setEnabled(uint32_t feature, bool enabled) {
    return Ready() ? m_setLoaded(feature, enabled) == sl::Result::eOk : o_setEnabled(feature, enabled);
}
static bool H_isEnabled(uint32_t feature) {
    if (!Ready()) return o_isEnabled(feature);
    bool loaded = false;
    return m_isLoaded(feature, loaded) == sl::Result::eOk && loaded;
}
static bool H_isSupported(uint32_t feature, uint32_t* adapterMask) {
    if (!Ready()) return o_isSupported(feature, adapterMask);
    sl::AdapterInfo adapter{};
    sl::Result result = m_isSupported(feature, adapter);
    static volatile LONG logged[2];
    if (!InterlockedExchange(&logged[feature == sl1::kFeatureDLSS_G], 1))
        Log("Streamline1 bridge: feature %u supported result=%d", feature, (int)result);
    return result == sl::Result::eOk;
}
static bool H_setTag(const sl1::Resource* resource, uint32_t tag, uint32_t id, const sl1::Extent* extent) {
    if (!Ready()) return o_setTag(resource, tag, id, extent);
    EnsureDevice();
    sl::Resource modern(resource && resource->type ? sl::ResourceType::eBuffer : sl::ResourceType::eTex2d,
        resource ? resource->native : NULL, resource ? resource->memory : NULL,
        resource ? resource->view : NULL, resource ? resource->state : 0);
    sl::Extent area{};
    if (extent) area = {extent->top, extent->left, extent->width, extent->height};
    sl::ResourceTag tagged(resource && resource->native ? &modern : NULL, tag,
        sl::ResourceLifecycle::eValidUntilPresent, extent ? &area : NULL);
    return m_setTag(sl::ViewportHandle(id), &tagged, 1, NULL) == sl::Result::eOk;
}
static bool H_setConstants(const sl1::Constants& values, uint32_t frameIndex, uint32_t id) {
    if (!Ready()) return o_setConstants(values, frameIndex, id);
    EnsureDevice();
    const sl::FrameToken* token = Token(frameIndex);
    if (!token) return false;
    sl::Constants modern{};
    memcpy(&modern.cameraViewToClip, &values, offsetof(sl1::Constants, depthInverted));
    modern.depthInverted = (sl::Boolean)values.depthInverted;
    modern.cameraMotionIncluded = (sl::Boolean)values.cameraMotionIncluded;
    modern.motionVectors3D = (sl::Boolean)values.motionVectors3D;
    modern.reset = (sl::Boolean)values.reset;
    modern.orthographicProjection = (sl::Boolean)values.orthographicProjection;
    modern.motionVectorsDilated = (sl::Boolean)values.motionVectorsDilated;
    modern.motionVectorsJittered = (sl::Boolean)values.motionVectorsJittered;
    return m_setConstants(modern, *token, sl::ViewportHandle(id)) == sl::Result::eOk;
}
static bool H_setFeatureConstants(uint32_t feature, const void* consts, uint32_t frameIndex, uint32_t id) {
    if (!Ready()) return o_setFeatureConstants(feature, consts, frameIndex, id);
    EnsureDevice();
    if (!consts) return false;
    if (feature == sl1::kFeatureDLSS_G) {
        static PFun_slDLSSGSetOptions* setOptions;
        if (!Feature(setOptions, sl::kFeatureDLSS_G, "slDLSSGSetOptions")) return false;
        const auto& legacy = *(const sl1::DLSSGConstants*)consts;
        sl::DLSSGOptions options{};
        options.mode = legacy.mode == 1 ? sl::DLSSGMode::eOn : legacy.mode == 2 ? sl::DLSSGMode::eAuto : sl::DLSSGMode::eOff;
        options.numFramesToGenerate = 1; // the 1.x plugin's only count; the DLSS-G hooks raise it
        // 1.x resends its constants every frame; 2.x wants options only when they
        // change. A configuration reload (g_epoch) resends them for the new mode.
        static volatile LONG lastMode = -1, lastEpoch = -1;
        LONG mode = (LONG)options.mode, epoch = g_epoch;
        if (InterlockedExchange(&lastMode, mode) == mode && InterlockedExchange(&lastEpoch, epoch) == epoch) return true;
        InterlockedExchange(&lastEpoch, epoch);
        return setOptions(sl::ViewportHandle(id), options) == sl::Result::eOk;
    }
    if (feature == sl1::kFeatureReflex) {
        if (!Feature(m_reflexOptions, sl::kFeatureReflex, "slReflexSetOptions")) return false;
        const auto& legacy = *(const sl1::ReflexConstants*)consts;
        sl::ReflexOptions options{};
        options.mode = (sl::ReflexMode)legacy.mode;
        options.frameLimitUs = legacy.frameLimitUs;
        options.useMarkersToOptimize = legacy.useMarkersToOptimize;
        options.virtualKey = legacy.virtualKey;
        return m_reflexOptions(options) == sl::Result::eOk;
    }
    return false;
}
static bool H_getSettings(uint32_t feature, const void* consts, void* settings) {
    if (!Ready()) return o_getSettings(feature, consts, settings);
    if (!settings) return false;
    if (feature == sl1::kFeatureDLSS_G) {
        if (!Feature(m_dlssgState, sl::kFeatureDLSS_G, "slDLSSGGetState")) return false;
        sl::DLSSGState state{};
        if (m_dlssgState(sl::ViewportHandle(0), state, NULL) != sl::Result::eOk) return false;
        auto& legacy = *(sl1::DLSSGSettings*)settings;
        legacy.estimatedVRAMUsageInBytes = state.estimatedVRAMUsageInBytes;
        legacy.status = (uint32_t)state.status;
        // 2.x reports no frame time: the rendered frame interval (between the
        // game's present markers) over the frames presented for each one.
        uint32_t presented = state.numFramesActuallyPresented ? state.numFramesActuallyPresented : 1;
        legacy.frameTimeMs = g_frameMs / presented;
        return true;
    }
    if (feature == sl1::kFeatureReflex) {
        if (!Feature(m_reflexState, sl::kFeatureReflex, "slReflexGetState")) return false;
        sl::ReflexState state{};
        if (m_reflexState(state) != sl::Result::eOk) return false;
        auto& legacy = *(sl1::ReflexSettings*)settings;
        legacy.lowLatencyAvailable = state.lowLatencyAvailable;
        legacy.latencyReportAvailable = state.latencyReportAvailable;
        legacy.statsWindowMessage = state.statsWindowMessage;
        return true;
    }
    return false;
}
// 1.x carries Reflex markers (and the sleep) as evaluations of the Reflex feature.
static bool H_evaluate(void* commandList, uint32_t feature, uint32_t frameIndex, uint32_t id) {
    if (!Ready()) return o_evaluate(commandList, feature, frameIndex, id);
    if (feature != sl1::kFeatureReflex) return true;
    const sl::FrameToken* token = Token(frameIndex);
    if (!token) return false;
    if (id == sl1::kMarkerSleep) {
        if (!Feature(m_reflexSleep, sl::kFeatureReflex, "slReflexSleep")) return false;
        return m_reflexSleep(*token) == sl::Result::eOk;
    }
    if (id == (uint32_t)sl::PCLMarker::ePresentStart) {
        static LARGE_INTEGER frequency, last;
        LARGE_INTEGER now;
        if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&now);
        float ms = last.QuadPart ? (float)((now.QuadPart - last.QuadPart) * 1000.0 / frequency.QuadPart) : 0.0f;
        last = now;
        if (ms > 0.0f && ms < 1000.0f) g_frameMs = g_frameMs ? g_frameMs * 0.9f + ms * 0.1f : ms;
    }
    if (!Feature(m_marker, sl::kFeaturePCL, "slPCLSetMarker")) return false;
    return m_marker((sl::PCLMarker)id, *token) == sl::Result::eOk;
}
static bool H_allocate(void* commandList, uint32_t feature, uint32_t id) {
    if (!Ready()) return o_allocate(commandList, feature, id);
    return m_allocate((sl::CommandBuffer*)commandList, feature, sl::ViewportHandle(id)) == sl::Result::eOk;
}
static bool H_free(uint32_t feature, uint32_t id) {
    return Ready() ? m_free(feature, sl::ViewportHandle(id)) == sl::Result::eOk : o_free(feature, id);
}
static bool H_upgrade(void** object) {
    return Ready() ? m_upgrade(object) == sl::Result::eOk : o_upgrade(object);
}
// 1.x picked up the device it created; 2.x must be told which one (slSetD3DDevice).
// Devices are created natively, as 2.x engine plugins do: a Streamline proxy
// device reaching the game's own NGX calls makes DLSS Super Resolution vanish.
// Unreal first creates and drops a device per adapter at the minimum level
// (11_0), then the real one at its highest level; that one is handed over.
static void* g_lastDevice;           // fallback if no device above 11_0 appears
static volatile LONG g_deviceSet = 0;

static void SetDevice(void* device, const char* reason) {
    if (InterlockedExchange(&g_deviceSet, 1)) return;
    sl::Result result = m_setDevice(device);
    Log("Streamline1 bridge: D3D12 device set (%s) result=%d", reason, (int)result);
}
// Called before per-frame work: hands over the last device if none qualified.
static void EnsureDevice() {
    if (g_deviceSet) return;
    AcquireSRWLockExclusive(&g_lock);
    void* device = g_lastDevice; g_lastDevice = NULL;
    ReleaseSRWLockExclusive(&g_lock);
    if (device) { SetDevice(device, "last created"); ((IUnknown*)device)->Release(); }
}
static HRESULT WINAPI H_device(void* adapter, int level, REFIID riid, void** device) {
    if (!Ready() || !s_createDevice) return o_device(adapter, level, riid, device);
    HRESULT result = s_createDevice(adapter, level, riid, device);
    if (SUCCEEDED(result) && device && *device && !g_deviceSet) {
        if (level > 0xb000) { SetDevice(*device, "highest level"); return result; }
        ((IUnknown*)*device)->AddRef();
        AcquireSRWLockExclusive(&g_lock);
        void* previous = g_lastDevice; g_lastDevice = *device;
        ReleaseSRWLockExclusive(&g_lock);
        if (previous) ((IUnknown*)previous)->Release();
    }
    return result;
}
static HRESULT WINAPI H_factory(REFIID riid, void** factory) {
    return Ready() ? m_factory(riid, factory) : o_factory(riid, factory);
}
static HRESULT WINAPI H_factory1(REFIID riid, void** factory) {
    return Ready() ? m_factory1(riid, factory) : o_factory1(riid, factory);
}
static HRESULT WINAPI H_factory2(UINT flags, REFIID riid, void** factory) {
    return Ready() ? m_factory2(flags, riid, factory) : o_factory2(flags, riid, factory);
}

static BOOL Hook(HMODULE module, const char* name, void* detour, void** original) {
    void* target = (void*)GetProcAddress(module, name);
    MH_STATUS status = target ? MH_CreateHook(target, detour, original) : MH_ERROR_FUNCTION_NOT_FOUND;
    if (status == MH_OK) status = MH_QueueEnableHook(target);
    if (status != MH_OK) Log("Streamline1 bridge: hook %s %s", name, MH_StatusToString(status));
    return status == MH_OK;
}

// Runs inside the loader's notification for the 1.x interposer, before the game
// can call it; only hooks are installed here (no loading under the loader lock).
static void Arm(HMODULE legacy) {
    if (GetProcAddress(legacy, "slGetFeatureFunction") || !GetProcAddress(legacy, "slSetFeatureConstants")) return;
    struct Decided { ~Decided() { InterlockedExchange(&g_decided, 1); } } decided;
    DWORD length = GetModuleFileNameW(legacy, g_folder, MAX_PATH);
    WCHAR* slash = length && length < MAX_PATH - 24 ? wcsrchr(g_folder, L'\\') : NULL;
    if (!slash) return;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - g_folder), L"sl2\\");
    WCHAR runtime[MAX_PATH];
    wcscpy_s(runtime, g_folder); wcscat_s(runtime, L"sl.interposer.dll");
    if (GetFileAttributesW(runtime) == INVALID_FILE_ATTRIBUTES) {
        Log("Streamline1 game; no %ls, running as shipped", runtime);
        return;
    }
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return;
    BOOL ok = TRUE;
#define BRIDGE_HOOK(name, detour, original) ok &= Hook(legacy, name, (void*)detour, (void**)&original)
    BRIDGE_HOOK("slInit", H_init, o_init); BRIDGE_HOOK("slShutdown", H_shutdown, o_shutdown);
    BRIDGE_HOOK("slSetFeatureEnabled", H_setEnabled, o_setEnabled);
    BRIDGE_HOOK("slIsFeatureEnabled", H_isEnabled, o_isEnabled);
    BRIDGE_HOOK("slIsFeatureSupported", H_isSupported, o_isSupported);
    BRIDGE_HOOK("slSetTag", H_setTag, o_setTag); BRIDGE_HOOK("slSetConstants", H_setConstants, o_setConstants);
    BRIDGE_HOOK("slSetFeatureConstants", H_setFeatureConstants, o_setFeatureConstants);
    BRIDGE_HOOK("slGetFeatureSettings", H_getSettings, o_getSettings);
    BRIDGE_HOOK("slEvaluateFeature", H_evaluate, o_evaluate);
    BRIDGE_HOOK("slAllocateResources", H_allocate, o_allocate); BRIDGE_HOOK("slFreeResources", H_free, o_free);
    BRIDGE_HOOK("slUpgradeInterface", H_upgrade, o_upgrade);
    BRIDGE_HOOK("D3D12CreateDevice", H_device, o_device); BRIDGE_HOOK("CreateDXGIFactory", H_factory, o_factory);
    BRIDGE_HOOK("CreateDXGIFactory1", H_factory1, o_factory1);
    BRIDGE_HOOK("CreateDXGIFactory2", H_factory2, o_factory2);
#undef BRIDGE_HOOK
    // All or nothing: a partly bridged 1.x runtime would mix two Streamlines.
    status = ok ? MH_ApplyQueued() : MH_ERROR_NOT_CREATED;
    if (status == MH_OK) InterlockedExchange(&g_armed, 1);
    Log("Streamline1 bridge %s (%ls)", status == MH_OK ? "armed" : "failed", g_folder);
}

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } Name;
typedef struct { ULONG Flags; const Name* FullDllName; const Name* BaseDllName; PVOID DllBase; ULONG SizeOfImage; } Loaded;
typedef VOID CALLBACK Notify(ULONG reason, const Loaded* data, PVOID context);
typedef LONG NTAPI Register(ULONG flags, Notify* callback, PVOID context, PVOID* cookie);

static VOID CALLBACK OnLoad(ULONG reason, const Loaded* data, PVOID) {
    static const WCHAR kName[] = L"sl.interposer.dll";
    if (reason != 1 || g_decided || g_folder[0] || !data || !data->BaseDllName) return;
    const Name& name = *data->BaseDllName;
    if (name.Length != sizeof(kName) - sizeof(WCHAR) || _wcsnicmp(name.Buffer, kName, name.Length / 2)) return;
    Arm((HMODULE)data->DllBase);
}

// Called from DllMain: Streamline loads long after the proxy, but the bridge must
// be in place before the game's first 1.x call.
static void Watch() {
    static PVOID cookie;
    auto reg = (Register*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification");
    if (reg) reg(0, OnLoad, NULL, &cookie);
}

// The 2.x interposer the DLSS-G hooks should use: for a 1.x game, the bridged
// runtime once the game has initialized it; otherwise the one the game loaded.
static HMODULE Interposer(HMODULE loaded) {
    if (GetProcAddress(loaded, "slGetFeatureFunction")) return loaded;
    ULONGLONG deadline = GetTickCount64() + 60000;
    while (!g_decided && GetTickCount64() < deadline) Sleep(10);
    while (g_armed && !g_modern && !g_failed && GetTickCount64() < deadline) Sleep(10);
    return g_modern ? g_modern : loaded;
}

} // namespace bridge
