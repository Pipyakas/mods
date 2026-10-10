// Minimal Unreal Engine reflection for monolithic shipping executables (UE 4.25+
// and UE5 layouts), included by dlss_mfg_sm86.cpp. These games export nothing,
// so three roots are located in memory and verified against each other:
//   GUObjectArray  - in a writable section: a chunked object array whose first
//                    entries report their own index;
//   the name table - in a writable section: a block pointer whose first entry is
//                    the name "None" (FName entries: 2-byte header, then text);
//   ProcessEvent   - a UObject virtual that tests both FUNC_Native (0x400) and
//                    FUNC_HasDefaults (0x400000) of the function it calls.
// Everything else (classes, properties, functions, calls) is found by name
// through the engine's own reflection data. Nothing is patched here.

namespace ue {

// UObject, UStruct and UFunction offsets shared by UE 4.25-4.27 and UE5.
enum : size_t {
    kObjectFlags = 0x08, kObjectIndex = 0x0C, kObjectClass = 0x10, kObjectName = 0x18, kObjectOuter = 0x20,
    kFieldNext = 0x28, kStructSuper = 0x40, kStructChildren = 0x48, kStructChildProperties = 0x50,
    kFunctionFlags = 0xB0, kFunctionParmsSize = 0xB6,
};
// FField/FProperty layout: UE 4.25 - 5.x keep a 16-byte owner (next, name and
// offset at 0x20/0x28/0x4C); later UE5 versions pack it into 8 bytes
// (0x18/0x20/0x44). Calibrated in Init against the engine's Vector struct.
static size_t kPropertyNext = 0x20, kPropertyName = 0x28, kPropertyOffset = 0x4C;
enum : unsigned { kClassDefaultObject = 0x10, kArchetypeObject = 0x20 };

typedef void ProcessEventFn(void* object, void* function, void* parameters);
static char* g_objects;      // FUObjectArray
static char** g_nameBlocks;  // FNameEntryAllocator::Blocks
static int g_processEventIndex = -1;
static ProcessEventFn* g_processEvent;

template <class T> static T Read(const void* at, size_t offset) { T value; memcpy(&value, (const char*)at + offset, sizeof(T)); return value; }

static bool Section(HMODULE module, const char* wanted, DWORD mustHave, char** start, size_t* size, int skip) {
    auto dos = (IMAGE_DOS_HEADER*)module;
    auto nt = (IMAGE_NT_HEADERS*)((char*)module + dos->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (wanted && strncmp((const char*)section->Name, wanted, 8)) continue;
        if ((section->Characteristics & mustHave) != mustHave) continue;
        if (skip-- > 0) continue;
        *start = (char*)module + section->VirtualAddress;
        *size = section->Misc.VirtualSize;
        return true;
    }
    return false;
}

// Cheap test before dereferencing a candidate pointer: most section data is not
// a pointer, and a fault per candidate would make the scan take minutes.
static bool Readable(const void* pointer) {
    static MEMORY_BASIC_INFORMATION region;
    uintptr_t at = (uintptr_t)pointer, base = (uintptr_t)region.BaseAddress;
    if (at < 0x10000 || at > 0x7FFFFFFFFFFF || (at & 7)) return false;
    if (at - base >= region.RegionSize && !VirtualQuery(pointer, &region, sizeof(region))) return false;
    return region.State == MEM_COMMIT && !(region.Protect & (PAGE_GUARD | PAGE_NOACCESS))
        && (region.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE));
}
// Object array layout: Objects (chunk table) at 0x10, MaxElements 0x20,
// NumElements 0x24, MaxChunks 0x28, NumChunks 0x2C; items are 24 bytes.
static bool LooksLikeObjectArray(const char* at) {
    __try {
        int maxElements = Read<int>(at, 0x20), count = Read<int>(at, 0x24);
        int maxChunks = Read<int>(at, 0x28), chunks = Read<int>(at, 0x2C);
        if (count < 4096 || count > 8 * 1024 * 1024 || maxElements < count) return false;
        if (chunks != (count + 65535) / 65536 || maxChunks < chunks || maxChunks > 1024) return false;
        char** table = Read<char**>(at, 0x10);
        if (!Readable(table) || !Readable(table[0])) return false;
        for (int i = 0; i < 3; ++i) {
            char* object = Read<char*>(table[0] + i * 24, 0);
            if (!object || Read<int>(object, kObjectIndex) != i) return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool LooksLikeNameBlocks(const char* at) {
    __try {
        const char* block = Read<const char*>(at, 0);
        if (!Readable(block)) return false;
        uint16_t header = Read<uint16_t>(block, 0);
        return (header & 1) == 0 && (header >> 6) == 4 && !memcmp(block + 2, "None", 4)
            && (Read<uint16_t>(block, 6) >> 6) == 12 && !memcmp(block + 8, "ByteProperty", 12);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Roots already known for a game build (image offsets), used only when they
// pass the same checks as the search, which then has nothing to do. The object
// array passes once the engine has started; call again until it does.
static void Prime(size_t objects, size_t names) {
    if (g_objects || g_nameBlocks || !objects || !names) return;
    __try {
        char* image = (char*)GetModuleHandleW(NULL);
        auto nt = (IMAGE_NT_HEADERS*)(image + ((IMAGE_DOS_HEADER*)image)->e_lfanew);
        if (nt->OptionalHeader.SizeOfImage < objects + 0x30 || nt->OptionalHeader.SizeOfImage < names + 8) return;
        if (!LooksLikeObjectArray(image + objects) || !LooksLikeNameBlocks(image + names)) return;
        g_objects = image + objects;
        g_nameBlocks = (char**)(image + names);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Name entries are 2-byte aligned within 64 KiB-stride blocks.
static bool NameEquals(uint32_t index, const char* text) {
    __try {
        const char* block = g_nameBlocks[index >> 16];
        if (!block) return false;
        const char* entry = block + (size_t)(index & 0xFFFF) * 2;
        uint16_t header = Read<uint16_t>(entry, 0);
        size_t length = header >> 6;
        if (strlen(text) != length) return false;
        if (header & 1) {
            for (size_t i = 0; i < length; ++i)
                if (Read<wchar_t>(entry + 2, i * 2) != (wchar_t)(unsigned char)text[i]) return false;
            return true;
        }
        return !memcmp(entry + 2, text, length);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static int NameText(uint32_t index, char* out, int capacity) {
    __try {
        const char* entry = g_nameBlocks[index >> 16] + (size_t)(index & 0xFFFF) * 2;
        uint16_t header = Read<uint16_t>(entry, 0);
        int length = header >> 6, i = 0;
        for (; i < length && i < capacity - 1; ++i)
            out[i] = (header & 1) ? (char)Read<wchar_t>(entry + 2, i * 2) : entry[2 + i];
        out[i] = 0;
        return i;
    } __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; return 0; }
}

static bool Is(const void* object, const char* name) { return object && NameEquals(Read<uint32_t>(object, kObjectName), name); }
static void* ClassOf(const void* object) { return Read<void*>(object, kObjectClass); }
static void* Outer(const void* object) { return Read<void*>(object, kObjectOuter); }

static int Count() { return Read<int>(g_objects, 0x24); }
// The object in slot `index` if it is alive, with its serial number.
static void* At(int index, int* serial) {
    char** table = Read<char**>(g_objects, 0x10);
    char* chunk = table[index / 65536];
    if (!chunk) return NULL;
    char* item = chunk + (size_t)(index % 65536) * 24;
    if (Read<unsigned>(item, 8) & 0x30000000) return NULL; // unreachable / pending kill
    if (serial) *serial = Read<int>(item, 16);
    return Read<void*>(item, 0);
}

// A pointer that survives across frames: valid while its slot still holds it.
struct Ref { void* object; int index, serial; };
static Ref Track(void* object) {
    if (!object) return Ref{};
    int index = Read<int>(object, kObjectIndex), serial = 0;
    return At(index, &serial) == object ? Ref{object, index, serial} : Ref{};
}
// Serial numbers are assigned lazily (on the first weak reference), so a zero
// serial is adopted once one appears.
static bool Alive(Ref& ref) {
    int serial = -1;
    if (!ref.object || ref.index >= Count() || At(ref.index, &serial) != ref.object) return false;
    if (serial == ref.serial) return true;
    if (ref.serial) return false;
    ref.serial = serial;
    return true;
}

// Live instance or class lookup by short name. Templates (class defaults and
// widget-tree archetypes, which sit inside a class) are skipped for instances.
static bool Template(const void* object) {
    if (Read<unsigned>(object, kObjectFlags) & (kClassDefaultObject | kArchetypeObject)) return true;
    for (const void* outer = Outer(object); outer; outer = Outer(outer)) {
        const void* type = ClassOf(outer);
        if (Is(type, "Class") || Is(type, "WidgetBlueprintGeneratedClass") || Is(type, "BlueprintGeneratedClass")) return true;
    }
    return false;
}
static void* FindInstance(const void* type) {
    for (int i = 0, n = type ? Count() : 0; i < n; ++i) {
        void* object = At(i, NULL);
        if (object && ClassOf(object) == type && !Template(object)) return object;
    }
    return NULL;
}
static void* FindClass(const char* name) {
    for (int i = 0, n = Count(); i < n; ++i) {
        void* object = At(i, NULL);
        if (!object || !Is(object, name)) continue;
        void* type = ClassOf(object);
        if (Is(type, "Class") || Is(type, "WidgetBlueprintGeneratedClass") || Is(type, "BlueprintGeneratedClass")) return object;
    }
    return NULL;
}
// A class default object ("Default__<Class>"), used to call static library functions.
static void* FindDefault(const char* className) {
    for (int i = 0, n = Count(); i < n; ++i) {
        void* object = At(i, NULL);
        if (object && (Read<unsigned>(object, kObjectFlags) & kClassDefaultObject) && Is(ClassOf(object), className))
            return object;
    }
    return NULL;
}

// Byte offset of a property in a class or struct (or a function's parameter
// block), searching base classes too; -1 when absent.
static int Offset(const void* type, const char* name) {
    for (; type; type = Read<void*>(type, kStructSuper))
        for (char* field = Read<char*>(type, kStructChildProperties); field; field = Read<char*>(field, kPropertyNext))
            if (NameEquals(Read<uint32_t>(field, kPropertyName), name)) return Read<int>(field, kPropertyOffset);
    return -1;
}
static int OffsetOf(const void* object, const char* name) { return object ? Offset(ClassOf(object), name) : -1; }
template <class T> static T Get(const void* object, const char* name, T fallback) {
    int at = OffsetOf(object, name);
    return at < 0 ? fallback : Read<T>(object, at);
}
template <class T> static bool Set(void* object, const char* name, T value) {
    int at = OffsetOf(object, name);
    if (at < 0) return false;
    memcpy((char*)object + at, &value, sizeof(T));
    return true;
}
static void* Function(const void* object, const char* name) {
    for (const void* type = object ? ClassOf(object) : NULL; type; type = Read<void*>(type, kStructSuper))
        for (char* field = Read<char*>(type, kStructChildren); field; field = Read<char*>(field, kFieldNext))
            if (Is(field, name) && Is(ClassOf(field), "Function")) return field;
    return NULL;
}

// One call through ProcessEvent with named parameters.
struct Call {
    void* object; void* function;
    alignas(16) unsigned char parameters[512];
    Call(void* target, const char* name) : object(target), function(Function(target, name)) { memset(parameters, 0, sizeof(parameters)); }
    bool Ok() const { return function && Read<uint16_t>(function, kFunctionParmsSize) <= sizeof(parameters); }
    template <class T> Call& Arg(const char* name, T value) {
        int at = function ? Offset(function, name) : -1;
        if (at >= 0 && at + sizeof(T) <= sizeof(parameters)) memcpy(parameters + at, &value, sizeof(T));
        return *this;
    }
    template <class T> T Result(const char* name) const {
        int at = function ? Offset(function, name) : -1;
        T value{};
        if (at >= 0) memcpy(&value, parameters + at, sizeof(T));
        return value;
    }
    bool Run() {
        if (!Ok()) return false;
        g_processEvent(object, function, parameters);
        return true;
    }
};

struct String { const wchar_t* data; int count, capacity; };
static String MakeString(const wchar_t* text) { int n = (int)wcslen(text) + 1; return String{text, n, n}; }
struct Text { unsigned char bytes[24]; };
template <class T> struct Array { T* data; int count, capacity; };

// Engine-owned text (the caller takes over the engine's reference).
static Text MakeText(const wchar_t* text) {
    static void* library;
    if (!library) library = FindDefault("KismetTextLibrary");
    Call call(library, "Conv_StringToText");
    // The parameter is "inString" in UE4 builds, "InString" in UE5 ones.
    call.Arg(call.function && Offset(call.function, "InString") >= 0 ? "InString" : "inString", MakeString(text)).Run();
    return call.Result<Text>("ReturnValue");
}
// An engine-allocated buffer of at least `bytes`, from an array of strings the
// engine builds (UKismetStringLibrary::ParseIntoArray). Freed by the engine when
// the array it is moved into is destroyed; the strings in it are dropped.
static void* EngineBuffer(size_t bytes, int* capacityBytes) {
    static void* library;
    if (!library) library = FindDefault("KismetStringLibrary");
    wchar_t source[512]; int n = 0;
    size_t strings = (bytes + 15) / 16;
    if (!library || strings == 0 || strings > 200) return NULL;
    for (size_t i = 0; i < strings; ++i) { source[n++] = L'x'; source[n++] = L','; }
    source[n] = 0;
    Call call(library, "ParseIntoArray");
    call.Arg("SourceString", MakeString(source)).Arg("Delimiter", MakeString(L",")).Arg("CullEmptyStrings", true).Run();
    auto result = call.Result<Array<String>>("ReturnValue");
    if (!result.data || (size_t)result.capacity * 16 < bytes) return NULL;
    *capacityBytes = result.capacity * 16;
    return result.data;
}

// Virtual-table slot called by a native UFunction's exec thunk (UFunction::Func):
// the first indirect call or jump through a register-held table, `call/jmp
// [reg+disp]`. -1 if none is found.
enum : size_t { kFunctionNative = 0xD8 };
static int VirtualSlot(const void* function) {
    __try {
        const unsigned char* code = Read<const unsigned char*>(function, kFunctionNative);
        if (!code) return -1;
        for (int i = 0; i < 0x80; ++i) {
            int at = i;
            if ((code[at] & 0xF0) == 0x40) at++; // REX
            if (code[at] != 0xFF) continue;
            unsigned char modrm = code[at + 1];
            int mod = modrm >> 6, reg = (modrm >> 3) & 7, rm = modrm & 7;
            if ((reg != 2 && reg != 4) || rm == 4 || rm == 5 || mod == 0 || mod == 3) continue;
            int disp = mod == 1 ? (signed char)code[at + 2] : Read<int>(code, at + 2);
            if (disp >= 0 && disp % 8 == 0 && disp < 0x2000) return disp / 8;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return -1;
}

static bool HasPattern(const unsigned char* code, size_t length, const unsigned char* immediate) {
    __try {
        for (size_t i = 0; i + 10 <= length; ++i)
            if (code[i] == 0xF7 && Read<uint32_t>(code, i + 2) == kFunctionFlags && !memcmp(code + i + 6, immediate, 4))
                return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

// Locates and cross-checks the three roots; false (and nothing used) on any doubt.
static bool Init() {
    if (g_processEvent) return true;
    HMODULE module = GetModuleHandleW(NULL);
    char* data; size_t size;
    for (int skip = 0; !g_objects || !g_nameBlocks; ++skip) {
        if (!Section(module, NULL, IMAGE_SCN_MEM_WRITE, &data, &size, skip)) break;
        for (size_t at = 0; at + 0x30 <= size; at += 8) {
            if (!g_objects && LooksLikeObjectArray(data + at)) g_objects = data + at;
            if (!g_nameBlocks && LooksLikeNameBlocks(data + at)) g_nameBlocks = (char**)(data + at);
        }
    }
    if (!g_objects || !g_nameBlocks) { Log("UE reflection: object array %p, names %p", g_objects, g_nameBlocks); return false; }
    void* first = At(0, NULL);
    char name[64];
    NameText(Read<uint32_t>(first, kObjectName), name, sizeof(name));
    if (strcmp(name, "/Script/CoreUObject")) { Log("UE reflection: object 0 is '%s'", name); return false; }
    // Property layout: the Vector struct's first fields are X, then Y.
    void* vector = NULL;
    for (int i = 0, n = Count(); i < n && !vector; ++i) {
        void* object = At(i, NULL);
        if (object && Is(object, "Vector") && Is(ClassOf(object), "ScriptStruct")) vector = object;
    }
    static const size_t layouts[2][3] = {{0x20, 0x28, 0x4C}, {0x18, 0x20, 0x44}};
    int layout = -1;
    for (int k = 0; k < 2 && vector && layout < 0; ++k) {
        __try {
            char* x = Read<char*>(vector, kStructChildProperties);
            char* y = x ? Read<char*>(x, layouts[k][0]) : NULL;
            if (x && Readable(y) && NameEquals(Read<uint32_t>(x, layouts[k][1]), "X") && NameEquals(Read<uint32_t>(y, layouts[k][1]), "Y")
                && Read<int>(x, layouts[k][2]) == 0 && Read<int>(y, layouts[k][2]) > 0) layout = k;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (layout < 0) { Log("UE reflection: property layout not recognised"); return false; }
    kPropertyNext = layouts[layout][0]; kPropertyName = layouts[layout][1]; kPropertyOffset = layouts[layout][2];
    // Protected executables rename their sections; any executable one will do.
    static const unsigned char native[4] = {0x00, 0x04, 0x00, 0x00}, defaults[4] = {0x00, 0x00, 0x40, 0x00};
    void** table = Read<void**>(first, 0);
    for (int i = 0; i < 128 && g_processEventIndex < 0; ++i) {
        unsigned char* code = (unsigned char*)table[i];
        bool executable = false;
        char* start; size_t length;
        for (int s = 0; !executable && Section(module, NULL, IMAGE_SCN_MEM_EXECUTE, &start, &length, s); ++s)
            executable = code >= (unsigned char*)start && code < (unsigned char*)start + length;
        if (executable && HasPattern(code, 0x400, native) && HasPattern(code, 0x400, defaults)) g_processEventIndex = i;
    }
    if (g_processEventIndex < 0) { Log("UE reflection: ProcessEvent not found"); return false; }
    g_processEvent = (ProcessEventFn*)table[g_processEventIndex];
    Log("UE reflection: objects=%d at +0x%llx, names at +0x%llx, ProcessEvent vtable[%d] +0x%llx, property layout %d",
        Count(), (unsigned long long)(g_objects - (char*)module), (unsigned long long)((char*)g_nameBlocks - (char*)module),
        g_processEventIndex, (unsigned long long)((char*)g_processEvent - (char*)module), layout);
    return true;
}

} // namespace ue
