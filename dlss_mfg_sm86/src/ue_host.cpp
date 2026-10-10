// Game-thread host for Unreal settings-menu rows, included by dlss_mfg_sm86.cpp.
//
// In a monolithic Unreal game (detected by the core) it locates the engine's
// reflection (ue_reflection.cpp), hooks ProcessEvent and, every 100 ms on the
// game thread, ticks the menu module for this game. [Menu] Probe=1 also polls
// dlss_mfg_sm86_probe.txt beside the INI for research commands; their output
// goes to dlss_mfg_sm86_probe_out.txt:
//   classes <text>   classes and structs whose name contains <text>
//   class <name>     properties (offsets, types) and functions (parameters)
//   tree <class>     widget tree of the first live instance, with texts
//   find <text>      live widgets whose text contains <text>, with their owners

namespace host {

// trace <names> <seconds>: log ProcessEvent calls on those objects.
static char g_traceName[128];
static ULONGLONG g_traceUntil;

static FILE* g_out;
static void Out(const char* format, ...) {
    if (!g_out) return;
    va_list args; va_start(args, format);
    vfprintf(g_out, format, args);
    va_end(args);
    fputc('\n', g_out);
}

static void Name(const void* object, char* text, int capacity) {
    ue::NameText(ue::Read<uint32_t>(object, ue::kObjectName), text, capacity);
}
static void FieldName(const void* field, char* text, int capacity) {
    ue::NameText(ue::Read<uint32_t>(field, ue::kPropertyName), text, capacity);
}
static void FieldType(const void* field, char* text, int capacity) {
    const void* type = ue::Read<void*>(field, 8); // FFieldClass, its FName first
    ue::NameText(type ? ue::Read<uint32_t>(type, 0) : 0, text, capacity);
}

// Text of an FText property (by default "Text", as on TextBlock-like widgets).
static bool WidgetText(void* widget, char* text, int capacity, const char* property = "Text") {
    text[0] = 0;
    int at = ue::OffsetOf(widget, property);
    if (at < 0) return false;
    static void* library;
    if (!library) library = ue::FindDefault("KismetTextLibrary");
    ue::Call call(library, "Conv_TextToString");
    call.Arg("InText", ue::Read<ue::Text>(widget, at)).Run(); // read only: no reference taken
    auto string = call.Result<ue::String>("ReturnValue");
    int n = 0;
    for (; string.data && n < string.count - 1 && n < capacity - 1; ++n) text[n] = (char)string.data[n];
    text[n] = 0;
    return n > 0;
}

static void Describe(const void* type) {
    char name[128], field[128], kind[64];
    for (; type; type = ue::Read<void*>(type, ue::kStructSuper)) {
        Name(type, name, sizeof(name));
        Out("CLASS %s", name);
        for (char* p = ue::Read<char*>(type, ue::kStructChildProperties); p; p = ue::Read<char*>(p, ue::kPropertyNext)) {
            FieldName(p, field, sizeof(field)); FieldType(p, kind, sizeof(kind));
            // Inner types (FArrayProperty::Inner, FStructProperty::Struct,
            // FMapProperty Key/Value): right after FProperty (0x78).
            char inner[160] = "";
            auto describe = [&](char* q, char* out, int capacity) {
                char k2[64]; FieldType(q, k2, sizeof(k2));
                void* s = ue::Read<void*>(q, 0x78);
                char n2[96] = "";
                if (!strcmp(k2, "StructProperty") && ue::Readable(s)) Name(s, n2, sizeof(n2));
                sprintf_s(out, capacity, "%s%s%s", k2, n2[0] ? ":" : "", n2);
            };
            if (!strcmp(kind, "StructProperty")) { void* s = ue::Read<void*>(p, 0x78); if (ue::Readable(s)) { strcpy_s(inner, " <"); Name(s, inner + 2, 150); strcat_s(inner, ">"); } }
            else if (!strcmp(kind, "ArrayProperty") || !strcmp(kind, "SetProperty")) {
                char* q = ue::Read<char*>(p, 0x78);
                if (ue::Readable(q)) { char d[128]; describe(q, d, sizeof(d)); sprintf_s(inner, " <%s>", d); }
            } else if (!strcmp(kind, "MapProperty")) {
                char* k = ue::Read<char*>(p, 0x78), *v = ue::Read<char*>(p, 0x80);
                if (ue::Readable(k) && ue::Readable(v)) { char d1[96], d2[96]; describe(k, d1, sizeof(d1)); describe(v, d2, sizeof(d2)); sprintf_s(inner, " <%s, %s>", d1, d2); }
            }
            Out("  P %s %s%s @%d", field, kind, inner, ue::Read<int>(p, ue::kPropertyOffset));
        }
        for (char* f = ue::Read<char*>(type, ue::kStructChildren); f; f = ue::Read<char*>(f, ue::kFieldNext)) {
            Name(f, field, sizeof(field));
            char line[1024]; int used = sprintf_s(line, "  F %s(", field);
            for (char* p = ue::Read<char*>(f, ue::kStructChildProperties); p && used < 900; p = ue::Read<char*>(p, ue::kPropertyNext)) {
                FieldName(p, name, sizeof(name)); FieldType(p, kind, sizeof(kind));
                used += sprintf_s(line + used, sizeof(line) - used, "%s:%s@%d ", name, kind, ue::Read<int>(p, ue::kPropertyOffset));
            }
            Out("%s)", line);
        }
        if (!strcmp(name, "UserWidget") || !strcmp(name, "Object")) break;
    }
}

static void Tree(void* widget, int depth) {
    if (!widget || depth > 24) return;
    char cls[128], name[128], text[256];
    Name(ue::ClassOf(widget), cls, sizeof(cls)); Name(widget, name, sizeof(name));
    WidgetText(widget, text, sizeof(text));
    ue::Call count(widget, "GetChildrenCount");
    int children = count.Run() ? count.Result<int>("ReturnValue") : -1;
    Out("%*s%s %s%s%s%s children=%d", depth * 2, "", cls, name, text[0] ? " '" : "", text, text[0] ? "'" : "", children);
    for (int i = 0; i < children; ++i) {
        ue::Call child(widget, "GetChildAt");
        child.Arg("Index", i).Run();
        Tree(child.Result<void*>("ReturnValue"), depth + 1);
    }
    ue::Call content(widget, "GetContent");
    if (content.Run()) Tree(content.Result<void*>("ReturnValue"), depth + 1);
    void* tree = ue::Get<void*>(widget, "WidgetTree", NULL);
    void* root = tree ? ue::Get<void*>(tree, "RootWidget", NULL) : NULL;
    if (root && root != widget) Tree(root, depth + 1);
}

static void Command(char* line) {
    char* argument = strchr(line, ' ');
    if (argument) *argument++ = 0; else argument = (char*)"";
    char name[256];
    if (!strcmp(line, "classes")) {
        for (int i = 0, n = ue::Count(); i < n; ++i) {
            void* object = ue::At(i, NULL);
            if (!object) continue;
            void* type = ue::ClassOf(object);
            if (!ue::Is(type, "Class") && !ue::Is(type, "WidgetBlueprintGeneratedClass") && !ue::Is(type, "BlueprintGeneratedClass")
                && !ue::Is(type, "ScriptStruct")) continue;
            Name(object, name, sizeof(name));
            if (strstr(name, argument)) Out("%s", name);
        }
    } else if (!strcmp(line, "class")) {
        void* type = ue::FindClass(argument);
        for (int i = 0, n = type ? 0 : ue::Count(); i < n && !type; ++i) { // or a struct
            void* object = ue::At(i, NULL);
            if (object && ue::Is(object, argument) && ue::Is(ue::ClassOf(object), "ScriptStruct")) type = object;
        }
        if (type) Describe(type); else Out("no class %s", argument);
    } else if (!strcmp(line, "tree")) {
        void* object = ue::FindInstance(ue::FindClass(argument));
        if (object) Tree(object, 0); else Out("no live %s", argument);
    } else if (!strcmp(line, "find")) {
        char text[256];
        for (int i = 0, n = ue::Count(); i < n; ++i) {
            void* object = ue::At(i, NULL);
            if (!object || ue::Template(object) || ue::OffsetOf(object, "Text") < 0) continue;
            if (!WidgetText(object, text, sizeof(text)) || !strstr(text, argument)) continue;
            char path[512]; int used = 0;
            for (const void* o = object; o && used < 400; o = ue::Outer(o)) {
                Name(o, name, sizeof(name));
                used += sprintf_s(path + used, sizeof(path) - used, "%s<", name);
            }
            Name(ue::ClassOf(object), name, sizeof(name));
            Out("%s '%s' %s", name, text, path);
        }
    } else if (!strcmp(line, "objs")) {
        // Live objects whose class name contains the text, with their names and
        // the usual setting identifiers (DevName, DisplayName).
        char text[256], cls[128], dev[128];
        for (int i = 0, n = ue::Count(); i < n; ++i) {
            void* object = ue::At(i, NULL);
            if (!object || ue::Template(object)) continue;
            Name(ue::ClassOf(object), cls, sizeof(cls));
            if (!strstr(cls, argument)) continue;
            Name(object, name, sizeof(name));
            int devAt = ue::OffsetOf(object, "DevName");
            dev[0] = 0;
            if (devAt >= 0) ue::NameText(ue::Read<uint32_t>(object, devAt), dev, sizeof(dev));
            WidgetText(object, text, sizeof(text), "DisplayName");
            // Lyra-style settings expose them only through getters.
            ue::Call getDev(object, "GetDevName");
            if (getDev.Run()) ue::NameText(getDev.Result<uint32_t>("ReturnValue"), dev, sizeof(dev));
            ue::Call getName(object, "GetDisplayName");
            if (getName.Run()) {
                ue::Text shown = getName.Result<ue::Text>("ReturnValue");
                static void* library;
                if (!library) library = ue::FindDefault("KismetTextLibrary");
                ue::Call convert(library, "Conv_TextToString");
                convert.Arg("InText", shown).Run();
                auto s = convert.Result<ue::String>("ReturnValue");
                int k = 0;
                for (; s.data && k < s.count - 1 && k < 255; ++k) text[k] = (char)s.data[k];
                text[k] = 0;
            }
            // Setting list entries: their setting and whether input is on.
            void* setting = ue::Get<void*>(object, "Setting", NULL);
            if (setting) {
                ue::Call settingDev(setting, "GetDevName");
                if (settingDev.Run()) ue::NameText(settingDev.Result<uint32_t>("ReturnValue"), dev, sizeof(dev));
                void* up = ue::Get<void*>(object, "Button_Increase", NULL), *value = ue::Get<void*>(object, "Panel_Value", NULL);
                ue::Call enabled(object, "GetIsEnabled");
                enabled.Run();
                Out("%s %s setting=%s enabled=%d increase=%u panel=%u", cls, name, dev, enabled.Result<bool>("ReturnValue"),
                    up ? ue::Get<uint8_t>(up, "bIsEnabled", 9) : 9, value ? ue::Get<uint8_t>(value, "bIsEnabled", 9) : 9);
                continue;
            }
            Out("%s %s dev=%s display='%s'", cls, name, dev, text);
        }
    } else if (!strcmp(line, "get") || !strcmp(line, "call")) {
        // get <class> <a.b.c>             typed value of a property path from the
        //                                 first live instance (objects followed)
        // call <class> [<a.b>:]<fn> [int] a function there, as UI input would
        char type[128] = {}, path[256] = {}; int value = 0;
        int fields = sscanf_s(argument, "%127s %255s %d", type, (unsigned)sizeof(type), path, (unsigned)sizeof(path), &value);
        // <class>@<name> picks the live instance with that name.
        char* named = strchr(type, '@');
        if (named) *named++ = 0;
        void* object = NULL;
        if (fields >= 2 && named) {
            void* cls = ue::FindClass(type);
            for (int i = 0, n = cls ? ue::Count() : 0; i < n && !object; ++i) {
                void* o = ue::At(i, NULL);
                if (!o || ue::ClassOf(o) != cls || (ue::Template(o) && strncmp(named, "Default__", 9))) continue;
                if (!strncmp(named, "txt:", 4)) { // txt:<text block property>=<its text>
                    char key[96] = {}; const char* equals = strchr(named + 4, '=');
                    if (!equals) continue;
                    memcpy(key, named + 4, std::min<size_t>(equals - named - 4, 95));
                    void* block = ue::Get<void*>(o, key, NULL);
                    char text[128];
                    char wanted[128]; strcpy_s(wanted, equals + 1);
                    for (char* c = wanted; *c; ++c) if (*c == '_') *c = ' '; // "_" for spaces
                    if (block && WidgetText(block, text, sizeof(text)) && !_stricmp(text, wanted)) object = o;
                    continue;
                }
                if (!strncmp(named, "dev:", 4) || !strncmp(named, "for:", 4)) {
                    // Lyra-style settings by DevName; "for:" their list entries
                    void* setting = named[0] == 'f' ? ue::Get<void*>(o, "Setting", NULL) : o;
                    if (!setting) continue;
                    ue::Call dev(setting, "GetDevName");
                    if (dev.Run() && ue::NameEquals(dev.Result<uint32_t>("ReturnValue"), named + 4)) object = o;
                } else if (ue::NameEquals(ue::Read<uint32_t>(o, ue::kObjectName), named)) object = o;
            }
        } else if (fields >= 2) {
            object = ue::FindInstance(ue::FindClass(type));
            if (!object) object = ue::FindDefault(type); // static function libraries
        }
        if (!object) { Out("no live %s", type); return; }
        char* function = NULL;
        if (!strcmp(line, "call")) {
            function = strchr(path, ':');
            if (function) *function++ = 0;
            else { static char only[256]; strcpy_s(only, path); function = only; path[0] = 0; }
        }
        char* context = NULL;
        char* part = path[0] ? strtok_s(path, ".", &context) : NULL;
        while (part) {
            char* nextPart = strtok_s(NULL, ".", &context);
            char* field = NULL;
            for (const void* c = ue::ClassOf(object); c && !field; c = ue::Read<void*>(c, ue::kStructSuper))
                for (char* f = ue::Read<char*>(c, ue::kStructChildProperties); f && !field; f = ue::Read<char*>(f, ue::kPropertyNext))
                    if (ue::NameEquals(ue::Read<uint32_t>(f, ue::kPropertyName), part)) field = f;
            if (!field) { Out("no property %s", part); return; }
            char kind[64]; FieldType(field, kind, sizeof(kind));
            char* at = (char*)object + ue::Read<int>(field, ue::kPropertyOffset);
            if (nextPart || function) {
                if (strcmp(kind, "ObjectProperty")) { Out("%s is %s, not an object", part, kind); return; }
                object = ue::Read<void*>(at, 0);
                if (!object) { Out("%s is null", part); return; }
                part = nextPart;
                continue;
            }
            if (!strcmp(kind, "TextProperty")) { char text[512]; WidgetText(object, text, sizeof(text), part); Out("%s = '%s'", part, text); }
            else if (!strcmp(kind, "BoolProperty") || !strcmp(kind, "ByteProperty") || !strcmp(kind, "EnumProperty")) Out("%s = %u", part, ue::Read<uint8_t>(at, 0));
            else if (!strcmp(kind, "FloatProperty")) Out("%s = %f", part, ue::Read<float>(at, 0));
            else if (!strcmp(kind, "ObjectProperty")) {
                void* pointer = ue::Read<void*>(at, 0); char pointed[128] = "null";
                if (pointer) Name(pointer, pointed, sizeof(pointed));
                Out("%s = %s", part, pointed);
            } else if (!strcmp(kind, "ArrayProperty") && ue::Readable(ue::Read<void*>(field, 0x78))) {
                char innerKind[64]; FieldType(ue::Read<char*>(field, 0x78), innerKind, sizeof(innerKind));
                auto list = ue::Read<ue::Array<ue::String>>(at, 0);
                Out("%s: %d %s", part, list.count, innerKind);
                for (int i = 0; !strcmp(innerKind, "StrProperty") && i < list.count && i < 64; ++i) {
                    char text[128] = {}; int k = 0;
                    for (; list.data[i].data && k < list.data[i].count - 1 && k < 127; ++k) text[k] = (char)list.data[i].data[k];
                    Out("  [%d] %s", i, text);
                }
            } else if (!strcmp(kind, "MapProperty")) {
                // Elements of 32 bytes: a small key padded to 8, a string, hash links.
                auto list = ue::Read<ue::Array<char[32]>>(at, 0);
                Out("%s: %d entries", part, list.count);
                for (int i = 0; i < list.count && i < 64; ++i) {
                    auto s = ue::Read<ue::String>(list.data[i], 8);
                    char text[128] = {}; int k = 0;
                    for (; ue::Readable((void*)s.data) && k < s.count - 1 && k < 127; ++k) text[k] = (char)s.data[k];
                    Out("  %d -> %s", ue::Read<int>(list.data[i], 0), text);
                }
            } else if (strstr(kind, "MulticastInlineDelegate")) {
                // Bindings: weak object (index, serial) and function name.
                auto list = ue::Read<ue::Array<char[16]>>(at, 0);
                Out("%s: %d bindings", part, list.count);
                for (int i = 0; i < list.count && i < 16; ++i) {
                    void* bound = ue::At(ue::Read<int>(list.data[i], 0), NULL);
                    char who[128] = "?", what[128];
                    if (bound) Name(bound, who, sizeof(who));
                    ue::NameText(ue::Read<uint32_t>(list.data[i], 8), what, sizeof(what));
                    Out("  %s.%s", who, what);
                }
            } else Out("%s (%s) = %d", part, kind, ue::Read<int>(at, 0));
            return;
        }
        if (!function) return;
        // call <class> texts: text arrays held anywhere in the object's first
        // 1 KB (unreflected members such as a setting's edit state).
        if (!strcmp(function, "texts")) {
            static void* library;
            if (!library) library = ue::FindDefault("KismetTextLibrary");
            for (int offset = 0x28; offset < 0x400; offset += 8) {
                auto list = ue::Read<ue::Array<ue::Text>>(object, offset);
                if (list.count < 1 || list.count > 8 || list.capacity < list.count || !ue::Readable(list.data)) continue;
                void* data = ue::Read<void*>(list.data, 0);
                if (!ue::Readable(data)) continue;
                __try {
                    ue::Call convert(library, "Conv_TextToString");
                    convert.Arg("InText", list.data[0]).Run();
                    auto s = convert.Result<ue::String>("ReturnValue");
                    char text[96] = {}; int k = 0;
                    for (; s.data && k < s.count - 1 && k < 95; ++k) text[k] = (char)s.data[k];
                    if (k) Out("+0x%X count=%d byte-8=0x%02X '%s'", offset, list.count, ue::Read<uint8_t>(object, offset - 8), text);
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            return;
        }
        // call <class> conds: arrays of polymorphic shared objects in the first
        // 1 KB (a Lyra setting's edit conditions), with their tables' RVAs.
        if (!strcmp(function, "conds")) {
            char* image = (char*)GetModuleHandleW(NULL);
            auto nt = (IMAGE_NT_HEADERS*)(image + ((IMAGE_DOS_HEADER*)image)->e_lfanew);
            char* end = image + nt->OptionalHeader.SizeOfImage;
            for (int offset = 0x28; offset < 0x400; offset += 8) {
                auto list = ue::Read<ue::Array<char[16]>>(object, offset);
                if (list.count < 1 || list.count > 8 || list.capacity < list.count || !ue::Readable(list.data)) continue;
                char found[512]; int used = sprintf_s(found, "+0x%X count=%d:", offset, list.count);
                bool ok = true;
                for (int i = 0; i < list.count && ok; ++i) {
                    void* item = ue::Read<void*>(list.data[i], 0);
                    void* table = ue::Readable(item) ? ue::Read<void*>(item, 0) : NULL;
                    ok = table >= (void*)image && table < (void*)end;
                    if (ok) used += sprintf_s(found + used, sizeof(found) - used, " %p(vt+0x%llX)", item, (unsigned long long)((char*)table - image));
                }
                if (ok) Out("%s", found);
            }
            return;
        }
        // call <class> hook <last slot>: counts calls into each virtual slot up
        // to that one (the object gets its own table of counting thunks);
        // call <class> hits: slots called since, then the counts are cleared.
        static void* hookedObject;
        static volatile LONG hits[1024];
        static int hookedSlots;
        if (!strcmp(function, "hook")) {
            int last = value > 0 && value < 1024 ? value : 120;
            void** table = ue::Read<void**>(object, 0);
            void** copy = (void**)VirtualAlloc(NULL, (last + 2) * sizeof(void*), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            unsigned char* code = (unsigned char*)VirtualAlloc(NULL, (last + 1) * 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (!copy || !code) { Out("no memory"); return; }
            copy[0] = table[-1]; // RTTI locator
            for (int slot = 0; slot <= last; ++slot) {
                unsigned char* thunk = code + slot * 32;
                volatile LONG* counter = &hits[slot];
                void* original = table[slot];
                thunk[0] = 0x48; thunk[1] = 0xB8; memcpy(thunk + 2, &counter, 8);   // mov rax, counter
                thunk[10] = 0xF0; thunk[11] = 0xFF; thunk[12] = 0x00;                // lock inc dword [rax]
                thunk[13] = 0x48; thunk[14] = 0xB8; memcpy(thunk + 15, &original, 8); // mov rax, original
                thunk[23] = 0xFF; thunk[24] = 0xE0;                                  // jmp rax
                copy[slot + 1] = thunk;
            }
            memset((void*)hits, 0, sizeof(hits));
            *(void***)object = copy + 1;
            hookedObject = object; hookedSlots = last;
            Out("counting slots 0-%d", last);
            return;
        }
        if (!strcmp(function, "hits")) {
            for (int slot = 0; slot <= hookedSlots; ++slot)
                if (hits[slot]) Out("slot %d: %ld", slot, hits[slot]);
            memset((void*)hits, 0, sizeof(hits));
            return;
        }
        // call <class> @<offset hex> <int>: writes an int32 into the object.
        if (function[0] == '@') {
            int offset = (int)strtol(function + 1, NULL, 16);
            if (offset < 0x28 || offset > 0x1000) { Out("bad offset"); return; }
            Out("+0x%X: %d -> %d", offset, ue::Read<int>(object, offset), value);
            memcpy((char*)object + offset, &value, sizeof(value));
            return;
        }
        // call <class> ret#<slot>: a getter's result, read both as a plain
        // return value and as a string written through a hidden result pointer.
        if (!strncmp(function, "ret#", 4)) {
            int slot = atoi(function + 4);
            void** table = ue::Read<void**>(object, 0);
            if (slot <= 0 || slot > 1024) { Out("bad slot"); return; }
            alignas(16) unsigned char result[64] = {};
            long long returned = ((long long (*)(void*, void*))table[slot])(object, result);
            auto s = ue::Read<ue::String>(result, 0);
            char text[128] = {}; int k = 0;
            if ((void*)returned == (void*)result && ue::Readable(s.data))
                for (; k < s.count - 1 && k < 127; ++k) text[k] = (char)s.data[k];
            Out("slot %d -> 0x%llX string='%s'", slot, returned, text);
            return;
        }
        // call <class> #<slot> [int]: a virtual function void(self, int).
        if (function[0] == '#') {
            int slot = atoi(function + 1);
            void** table = ue::Read<void**>(object, 0);
            if (slot <= 0 || slot > 1024) { Out("bad slot"); return; }
            ((void (*)(void*, int))table[slot])(object, value);
            Out("called slot %d(%d)", slot, value);
            return;
        }
        void* target = ue::Function(object, function);
        if (!target) { Out("no function %s", function); return; }
        ue::Call call(object, function);
        char* first = ue::Read<char*>(target, ue::kStructChildProperties);
        // "&<property>": that object property of the target (as object and
        // interface), e.g. the sender a view model's click handler compares.
        const char* reference = strchr(argument, '&');
        if (reference && first) {
            // "&<class>@<name>": that live object instead.
            char other[128] = {}; sscanf_s(reference + 1, "%127s", other, (unsigned)sizeof(other));
            char* at2 = strchr(other, '@');
            void* pointer = NULL;
            if (at2) {
                *at2++ = 0;
                void* cls = ue::FindClass(other);
                for (int i = 0, n = cls ? ue::Count() : 0; i < n && !pointer; ++i) {
                    void* o = ue::At(i, NULL);
                    if (o && ue::ClassOf(o) == cls && !ue::Template(o) && ue::NameEquals(ue::Read<uint32_t>(o, ue::kObjectName), at2)) pointer = o;
                }
            } else pointer = ue::Get<void*>(object, other, NULL);
            unsigned char* at = call.parameters + ue::Read<int>(first, ue::kPropertyOffset);
            memcpy(at, &pointer, sizeof(pointer)); memcpy(at + 8, &pointer, sizeof(pointer));
        } else if (fields >= 3 && first) memcpy(call.parameters + ue::Read<int>(first, ue::kPropertyOffset), &value, sizeof(value));
        call.Run();
        char called[128]; Name(object, called, sizeof(called));
        Out("called %s.%s(%d) -> %d", called, function, value, ue::Read<int>(call.parameters, 0));
        // A returned text array (GetDiscreteOptions): its texts.
        for (char* p = ue::Read<char*>(target, ue::kStructChildProperties); p; p = ue::Read<char*>(p, ue::kPropertyNext)) {
            char kind[64]; FieldType(p, kind, sizeof(kind));
            if (strcmp(kind, "ArrayProperty")) continue;
            auto texts = ue::Read<ue::Array<ue::Text>>(call.parameters, ue::Read<int>(p, ue::kPropertyOffset));
            static void* library;
            if (!library) library = ue::FindDefault("KismetTextLibrary");
            for (int i = 0; i < texts.count && i < 32; ++i) {
                ue::Call convert(library, "Conv_TextToString");
                convert.Arg("InText", texts.data[i]).Run();
                auto s = convert.Result<ue::String>("ReturnValue");
                char text[128] = {}; int k = 0;
                for (; s.data && k < s.count - 1 && k < 127; ++k) text[k] = (char)s.data[k];
                Out("  [%d] %s", i, text);
            }
        }
    } else if (!strcmp(line, "trace")) {
        int seconds = 20;
        if (sscanf_s(argument, "%127s %d", g_traceName, (unsigned)sizeof(g_traceName), &seconds) >= 1) {
            g_traceUntil = GetTickCount64() + seconds * 1000ull;
            Out("tracing %s for %d s into the log", g_traceName, seconds);
        }
    } else if (!strcmp(line, "press")) {
        // press <text>: Click() on the live button whose TitleText is <text>.
        char text[256];
        for (int i = 0, n = ue::Count(); i < n; ++i) {
            void* object = ue::At(i, NULL);
            if (!object || ue::Template(object) || ue::OffsetOf(object, "TitleText") < 0) continue;
            if (!WidgetText(object, text, sizeof(text), "TitleText") || _stricmp(text, argument)) continue;
            ue::Call click(object, "Click");
            Name(ue::ClassOf(object), name, sizeof(name));
            Out("press '%s' on %s: %s", text, name, click.Run() ? "clicked" : "no Click()");
            return;
        }
        Out("no button '%s'", argument);
    } else if (!strcmp(line, "script")) {
        // script <class> <function>: objects and properties referenced by its
        // bytecode, in order (UStruct::Script at 0x60, UE 4.27).
        char type[128] = {}, wanted[128] = {};
        sscanf_s(argument, "%127s %127s", type, (unsigned)sizeof(type), wanted, (unsigned)sizeof(wanted));
        char* function = NULL;
        for (const void* c = ue::FindClass(type); c && !function; c = ue::Read<void*>(c, ue::kStructSuper))
            for (char* f = ue::Read<char*>(c, ue::kStructChildren); f && !function; f = ue::Read<char*>(f, ue::kFieldNext))
                if (ue::NameEquals(ue::Read<uint32_t>(f, ue::kObjectName), wanted)) function = f;
        if (!function) { Out("no function %s", wanted); return; }
        auto script = ue::Read<ue::Array<uint8_t>>(function, 0x60);
        Out("%d bytes", script.count);
        char line2[1024]; int used = 0;
        for (int i = 0; i + 8 <= script.count; ++i) {
            void* p = *(void**)(script.data + i);
            if ((uintptr_t)p < 0x10000 || ((uintptr_t)p & 7) || !ue::Readable(p) || !ue::Readable((char*)p + 0x30)) continue;
            char kind[64] = {}, label[128] = {};
            void* cls = ue::Read<void*>(p, ue::kObjectClass);
            void* fieldClass = ue::Read<void*>(p, 8);
            if (cls && ue::Readable(cls) && (ue::Is(cls, "Function") || ue::Is(cls, "DelegateFunction"))) {
                Name(p, label, sizeof(label)); Name(ue::Outer(p), kind, sizeof(kind));
                used += sprintf_s(line2 + used, sizeof(line2) - used, "@%d CALL %s::%s; ", i, kind, label);
            } else if (fieldClass && ue::Readable(fieldClass)) {
                ue::NameText(ue::Read<uint32_t>(fieldClass, 0), kind, sizeof(kind));
                if (!strstr(kind, "Property")) continue;
                FieldName(p, label, sizeof(label));
                used += sprintf_s(line2 + used, sizeof(line2) - used, "@%d %s %s; ", i, kind, label);
            } else continue;
            i += 7;
            if (used > 800) { Out("%s", line2); used = 0; line2[0] = 0; }
        }
        if (used) Out("%s", line2);
    } else if (!strcmp(line, "cvar")) {
        // cvar <name>: integer and float value of a console variable.
        static void* library;
        if (!library) library = ue::FindDefault("KismetSystemLibrary");
        wchar_t wide[128]; size_t converted = 0;
        mbstowcs_s(&converted, wide, argument, _TRUNCATE);
        ue::Call asInt(library, "GetConsoleVariableIntValue"), asFloat(library, "GetConsoleVariableFloatValue");
        asInt.Arg("VariableName", ue::MakeString(wide)).Run();
        asFloat.Arg("VariableName", ue::MakeString(wide)).Run();
        Out("%s = %d (%f)", argument, asInt.Result<int>("ReturnValue"), asFloat.Result<float>("ReturnValue"));
    } else if (!strcmp(line, "array")) {
        // array <class>[@name] <array property> <struct>: each element's object
        // members (names, texts) and the strings in its unreflected bytes.
        char type[128] = {}, property[96] = {}, element[96] = {};
        sscanf_s(argument, "%127s %95s %95s", type, (unsigned)sizeof(type), property, (unsigned)sizeof(property), element, (unsigned)sizeof(element));
        char* named = strchr(type, '@');
        if (named) *named++ = 0;
        void* cls = ue::FindClass(type), *object = NULL;
        for (int i = 0, n = cls ? ue::Count() : 0; i < n && !object; ++i) {
            void* o = ue::At(i, NULL);
            if (o && ue::ClassOf(o) == cls && !ue::Template(o) && (!named || ue::NameEquals(ue::Read<uint32_t>(o, ue::kObjectName), named))) object = o;
        }
        void* structType = NULL;
        for (int i = 0, n = ue::Count(); i < n && !structType; ++i) {
            void* o = ue::At(i, NULL);
            if (o && ue::Is(o, element) && ue::Is(ue::ClassOf(o), "ScriptStruct")) structType = o;
        }
        int at = object ? ue::OffsetOf(object, property) : -1;
        if (at < 0 || !structType) { Out("no %s.%s or %s", type, property, element); return; }
        int stride = ue::Read<int>(structType, 0x58);
        auto list = ue::Read<ue::Array<char>>(object, at);
        Out("%d elements, stride %d", list.count, stride);
        for (int e = 0; e < list.count && e < 64; ++e) {
            char* item = list.data + (size_t)e * stride;
            char found[1024]; int used = sprintf_s(found, "[%d]", e);
            for (char* f = ue::Read<char*>(structType, ue::kStructChildProperties); f; f = ue::Read<char*>(f, ue::kPropertyNext)) {
                char field[64]; FieldName(f, field, sizeof(field));
                void* o = ue::Read<void*>(item, ue::Read<int>(f, ue::kPropertyOffset));
                char shown[128] = "null";
                if (o && ue::Readable(o)) { Name(o, shown, sizeof(shown)); char text[96]; if (WidgetText(o, text, sizeof(text))) { strcat_s(shown, "'"); strcat_s(shown, text); strcat_s(shown, "'"); } }
                used += sprintf_s(found + used, sizeof(found) - used, " %s=%s", field, shown);
            }
            for (int off = 24; off + 16 <= stride && used < 900; off += 8) {
                auto s = ue::Read<ue::String>(item, off);
                if (s.count < 2 || s.count > 80 || s.capacity < s.count || !ue::Readable((void*)s.data)) continue;
                char text[96] = {}; int k = 0; bool printable = true;
                for (; k < s.count - 1 && k < 95; ++k) { wchar_t c = s.data[k]; printable &= c >= 32 && c < 127; text[k] = (char)c; }
                if (printable && !s.data[s.count - 1]) used += sprintf_s(found + used, sizeof(found) - used, " +%d'%s'", off, text);
            }
            Out("%s", found);
        }
    } else if (!strcmp(line, "setstrs")) {
        // setstrs <class>@<name> <string array property> <a,b,c>: replaces the
        // array with an engine-allocated one (UKismetStringLibrary::ParseIntoArray).
        char type[128] = {}, property[96] = {}, values[1024] = {};
        sscanf_s(argument, "%127s %95s %1023s", type, (unsigned)sizeof(type), property, (unsigned)sizeof(property), values, (unsigned)sizeof(values));
        char* named = strchr(type, '@');
        if (named) *named++ = 0;
        void* cls = ue::FindClass(type), *object = NULL;
        for (int i = 0, n = cls ? ue::Count() : 0; i < n && !object; ++i) {
            void* o = ue::At(i, NULL);
            if (o && ue::ClassOf(o) == cls && (!named || ue::NameEquals(ue::Read<uint32_t>(o, ue::kObjectName), named))) object = o;
        }
        int at = object ? ue::OffsetOf(object, property) : -1;
        if (at < 0) { Out("no %s.%s", type, property); return; }
        static void* library;
        if (!library) library = ue::FindDefault("KismetStringLibrary");
        static wchar_t wide[1024]; size_t converted = 0;
        mbstowcs_s(&converted, wide, values, _TRUNCATE);
        ue::Call call(library, "ParseIntoArray");
        call.Arg("SourceString", ue::MakeString(wide)).Arg("Delimiter", ue::MakeString(L",")).Arg("CullEmptyStrings", true).Run();
        auto result = call.Result<ue::Array<ue::String>>("ReturnValue");
        memcpy((char*)object + at, &result, sizeof(result)); // the old array is left to leak
        Out("%s.%s = %d strings", type, property, result.count);
    } else if (!strcmp(line, "rows")) {
        // rows <data table> [max]: each row's name and member values
        // (UDataTable: RowStruct, then RowMap's set elements of 24 bytes).
        char tableName[128] = {}; int most = 400;
        sscanf_s(argument, "%127s %d", tableName, (unsigned)sizeof(tableName), &most);
        void* table = NULL;
        for (int i = 0, n = ue::Count(); i < n && !table; ++i) {
            void* object = ue::At(i, NULL);
            if (object && ue::Is(object, tableName) && !ue::Template(object)) table = object;
        }
        if (!table) { Out("no %s", tableName); return; }
        void* rowStruct = ue::Get<void*>(table, "RowStruct", NULL);
        int structAt = ue::OffsetOf(table, "RowStruct");
        auto rows = ue::Read<ue::Array<char[24]>>(table, structAt + 8);
        Name(rowStruct, name, sizeof(name));
        Out("%s: %s, %d rows", tableName, name, rows.count);
        static void* library;
        if (!library) library = ue::FindDefault("KismetTextLibrary");
        for (int r = 0; r < rows.count && r < most; ++r) {
            char* data = ue::Read<char*>(rows.data[r], 8);
            if (!ue::Readable(data)) continue;
            char row[2048]; int used = 0;
            ue::NameText(ue::Read<uint32_t>(rows.data[r], 0), row, 128); used = (int)strlen(row);
            for (const void* s = rowStruct; s; s = ue::Read<void*>(s, ue::kStructSuper))
                for (char* f = ue::Read<char*>(s, ue::kStructChildProperties); f && used < 1800; f = ue::Read<char*>(f, ue::kPropertyNext)) {
                    char field[96], kind[64], value[256] = "?";
                    FieldName(f, field, sizeof(field)); FieldType(f, kind, sizeof(kind));
                    char* at = data + ue::Read<int>(f, ue::kPropertyOffset);
                    if (!strcmp(kind, "IntProperty")) sprintf_s(value, "%d", ue::Read<int>(at, 0));
                    else if (!strcmp(kind, "FloatProperty")) sprintf_s(value, "%g", ue::Read<float>(at, 0));
                    else if (!strcmp(kind, "BoolProperty") || !strcmp(kind, "ByteProperty") || !strcmp(kind, "EnumProperty")) sprintf_s(value, "%u", ue::Read<uint8_t>(at, 0));
                    else if (!strcmp(kind, "NameProperty")) ue::NameText(ue::Read<uint32_t>(at, 0), value, sizeof(value));
                    else if (!strcmp(kind, "StrProperty") || !strcmp(kind, "TextProperty")) {
                        ue::String s2 = ue::Read<ue::String>(at, 0);
                        if (kind[0] == 'T') {
                            ue::Call convert(library, "Conv_TextToString");
                            convert.Arg("InText", ue::Read<ue::Text>(at, 0)).Run();
                            s2 = convert.Result<ue::String>("ReturnValue");
                        }
                        int k = 0;
                        for (; s2.data && ue::Readable((void*)s2.data) && k < s2.count - 1 && k < 60; ++k) value[k] = (char)s2.data[k];
                        value[k] = 0;
                    } else if (!strcmp(kind, "ArrayProperty")) sprintf_s(value, "[%d]", ue::Read<int>(at, 8));
                    else if (!strcmp(kind, "ObjectProperty") || !strcmp(kind, "SoftObjectProperty") || !strcmp(kind, "ClassProperty")) {
                        void* o = ue::Read<void*>(at, 0);
                        if (o && kind[0] != 'S' && ue::Readable(o)) Name(o, value, sizeof(value)); else strcpy_s(value, o ? "set" : "null");
                    } else sprintf_s(value, "<%s>", kind);
                    used += sprintf_s(row + used, sizeof(row) - used, " %s=%s", field, value);
                }
            Out("%s", row);
        }
    } else if (!strcmp(line, "slots")) {
        // Virtual slots of a class's native functions, from their exec thunks.
        for (const void* type = ue::FindClass(argument); type; type = ue::Read<void*>(type, ue::kStructSuper))
            for (char* f = ue::Read<char*>(type, ue::kStructChildren); f; f = ue::Read<char*>(f, ue::kFieldNext)) {
                Name(f, name, sizeof(name));
                Out("%s slot=%d", name, ue::VirtualSlot(f));
            }
    } else {
        Out("unknown command %s", line);
    }
}

static void Probe() {
    WCHAR command[MAX_PATH], output[MAX_PATH];
    wcscpy_s(command, g_directory); wcscat_s(command, L"dlss_mfg_sm86_probe.txt");
    wcscpy_s(output, g_directory); wcscat_s(output, L"dlss_mfg_sm86_probe_out.txt");
    FILE* in = NULL;
    if (_wfopen_s(&in, command, L"rb") || !in) return;
    char text[4096] = {};
    fread(text, 1, sizeof(text) - 1, in);
    fclose(in);
    DeleteFileW(command);
    if (_wfopen_s(&g_out, output, L"wb") || !g_out) return;
    char* next = NULL;
    for (char* line = strtok_s(text, "\r\n", &next); line; line = strtok_s(NULL, "\r\n", &next)) {
        Out("> %s", line);
        __try { Command(line); } __except (EXCEPTION_EXECUTE_HANDLER) { Out("fault in command"); }
    }
    Out("DONE");
    fclose(g_out); g_out = NULL;
}

typedef void Module();
static Module* g_tick;
// Runs after each ProcessEvent call (results in `parameters`), on the game thread.
typedef void After(void* object, void* function, void* parameters);
static After* g_after;
static BOOL g_probe;
static volatile bool g_ticking; // the hook runs (and routes classes loaded later)

// ProcessEvent is virtual: every class's virtual table holding it gets our
// function in that slot. No code is patched (protected executables refuse
// trampolines near their image), and the original stays directly callable.
static void H_processEvent(void* object, void* function, void* parameters);

static int PatchTables() {
    static void* seen[4096];
    static int count;
    int patched = 0;
    for (int i = 0, n = ue::Count(); i < n; ++i) {
        void* object = ue::At(i, NULL);
        void** table = object ? ue::Read<void**>(object, 0) : NULL;
        if (!table) continue;
        bool known = false;
        for (int k = 0; k < count && !known; ++k) known = seen[k] == table;
        if (known) continue;
        if (count < 4096) seen[count++] = table;
        void** slot = table + ue::g_processEventIndex;
        if (*slot != (void*)ue::g_processEvent) continue;
        DWORD old;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) continue;
        InterlockedExchangePointer(slot, (void*)H_processEvent);
        VirtualProtect(slot, sizeof(void*), old, &old);
        patched++;
    }
    return patched;
}

static void H_processEvent(void* object, void* function, void* parameters) {
    static ULONGLONG next, nextProbe, nextPatch;
    static bool inside;
    ULONGLONG now = GetTickCount64();
    if (g_traceUntil && now < g_traceUntil && object) {
        // Comma-separated object names; "Opt*" matches every name starting with
        // Opt. Per-frame Tick calls are left out.
        char objectName[128], name[128]; Name(object, objectName, sizeof(objectName));
        Name(function, name, sizeof(name));
        bool match = false;
        for (const char* pattern = g_traceName; *pattern && !match && strcmp(name, "Tick");) {
            size_t length = strcspn(pattern, ",");
            bool prefix = length && pattern[length - 1] == '*';
            match = prefix ? !strncmp(objectName, pattern, length - 1)
                : strlen(objectName) == length && !strncmp(objectName, pattern, length);
            pattern += length + (pattern[length] == ',');
        }
        if (match) Log("Trace %s.%s", objectName, name);
    }
    if (!inside && now >= next) {
        next = now + 100;
        inside = true;
        g_ticking = true;
        if (g_tick) g_tick();
        if (g_probe && now >= nextProbe) { nextProbe = now + 1000; Probe(); }
        // Classes loaded later bring new tables.
        if (!nextPatch) nextPatch = now + 10000; // Install has just patched
        else if (now >= nextPatch) { nextPatch = now + 10000; PatchTables(); }
        inside = false;
    }
    ue::g_processEvent(object, function, parameters);
    if (g_after) g_after(object, function, parameters);
}

// Known roots for this game's build (ue::Prime), or 0.
static size_t g_knownObjects, g_knownNames;

static DWORD WINAPI Install(void*) {
    ULONGLONG start = GetTickCount64();
    // Known roots are taken as soon as they pass (polled every 100 ms); the
    // search (slow) runs once a second, after 30 s when roots are known.
    for (int i = 0; i < 1200; ++i) {
        ue::Prime(g_knownObjects, g_knownNames);
        if ((ue::g_objects || (i % 10 == 0 && (!g_knownObjects || i >= 300))) && ue::Init()) break;
        Sleep(100);
    }
    Log("Menu: engine reflection search took %llu ms", GetTickCount64() - start);
    if (!ue::g_processEvent) { Log("Menu: engine reflection not found; menu left as shipped"); return 1; }
    int patched = PatchTables();
    // Found early (known roots), few classes exist yet: more are routed until
    // the hook runs, which then routes the rest itself.
    for (int i = 0; i < 600 && !g_ticking; ++i) { Sleep(500); if (!g_ticking) patched += PatchTables(); }
    Log("Menu: ProcessEvent routed in %d class tables%s", patched, g_probe ? " (probe on)" : "");
    return patched ? 0 : 2;
}

// Called once the executable is known to be Unreal (monolithic builds only:
// modular ones such as Returnal export their engine and are handled directly).
static void Start(const WCHAR* executable) {
    g_probe = Setting(g_config, L"Menu", L"Probe", 0, 0, 1);
    if (Setting(g_config, L"Menu", L"Enabled", 1, 0, 1)) {
        if (wcsstr(executable, L"Ghostrunner2-Win64-Shipping.exe")) { g_tick = menu_gr2::Tick; g_after = menu_gr2::After; }
        else if (wcsstr(executable, L"BBQ-Win64-Shipping.exe")) g_tick = menu_khazan::Tick;
        else if (wcsstr(executable, L"AceCombat8.exe")) { g_tick = menu_ac8::Tick; g_knownObjects = 0xdc60cd0; g_knownNames = 0xe37d990; }
        else if (wcsstr(executable, L"NINJAGAIDEN2BLACK-Win64-Shipping.exe")) { g_tick = menu_ng2b::Tick; g_after = menu_ng2b::After; }
    }
    if (!g_tick && !g_probe) return;
    HANDLE thread = CreateThread(NULL, 0, Install, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
}

} // namespace host
