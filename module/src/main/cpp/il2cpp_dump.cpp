#include "il2cpp_dump.h"
#include <dlfcn.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cinttypes>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <unistd.h>
#include <link.h>
#include <csetjmp>
#include <csignal>
#include <ctime>
#include "xdl.h"
#include "log.h"
#include "il2cpp-tabledefs.h"
#include "il2cpp-class.h"

#define DO_API(r, n, p) r (*n) p
#include "il2cpp-api-functions.h"
#undef DO_API

static uint64_t il2cpp_base = 0;

/* ═══════════════════════════════════════════════════════════
   TARGETED DUMPING — input.json reader (no exceptions, no nlohmann)
   ═══════════════════════════════════════════════════════════ */
static std::vector<std::string> g_target_classes;
static bool g_config_loaded = false;
static bool g_full_dump = false;

static void load_input_config(const char* game_data_dir) {
    g_target_classes.clear();
    g_config_loaded = true;
    g_full_dump = false;

    std::string path = std::string(game_data_dir) + "/files/input.json";
    FILE* f = fopen(path.c_str(), "r");
    if (!f) {
        LOGI("input.json not found at %s — doing full dump", path.c_str());
        g_full_dump = true;
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 1024 * 1024) {
        fclose(f);
        g_full_dump = true;
        return;
    }

    char* buf = (char*)malloc(size + 1);
    if (!buf) { fclose(f); g_full_dump = true; return; }

    size_t rd = fread(buf, 1, size, f);
    fclose(f);
    buf[rd] = '\0';

    /* lightweight JSON parser: extract strings inside the "classes" array */
    bool in_classes_array = false;
    bool in_string = false;
    std::string current;
    bool saw_classes_key = false;

    for (size_t i = 0; i < rd; i++) {
        char c = buf[i];
        if (c == '"') {
            if (in_string) {
                if (in_classes_array && !current.empty()) {
                    g_target_classes.push_back(current);
                } else if (!in_classes_array && current == "classes") {
                    saw_classes_key = true;
                }
                current.clear();
                in_string = false;
            } else {
                in_string = true;
            }
        } else if (in_string) {
            current += c;
        } else if (c == '[') {
            if (saw_classes_key || current.empty()) in_classes_array = true;
            current.clear();
        } else if (c == ']') {
            in_classes_array = false;
            saw_classes_key = false;
            current.clear();
        } else if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != ',') {
            if (!in_classes_array) current += c;
        }
    }

    free(buf);

    if (g_target_classes.empty()) {
        LOGI("input.json empty/invalid — doing full dump");
        g_full_dump = true;
    } else {
        LOGI("Loaded %zu target classes from input.json", g_target_classes.size());
    }
}

static bool is_target_class(const char* class_name) {
    if (g_full_dump) return true;
    if (!class_name) return false;
    for (const auto& t : g_target_classes) {
        if (t == class_name) return true;
    }
    return false;
}

/* ═══════════════════════════════════════════════════════════
   IL2CPP API INIT
   ═══════════════════════════════════════════════════════════ */
void init_il2cpp_api(void *handle) {
#define DO_API(r, n, p) { n = (r (*) p)xdl_sym(handle, #n, nullptr); if(!n) LOGW("api not found %s", #n); }
#include "il2cpp-api-functions.h"
#undef DO_API
}

/* ═══════════════════════════════════════════════════════════
   METHOD MODIFIER / TYPE HELPERS
   ═══════════════════════════════════════════════════════════ */
std::string get_method_modifier(uint32_t flags) {
    std::stringstream outPut;
    auto access = flags & METHOD_ATTRIBUTE_MEMBER_ACCESS_MASK;
    switch (access) {
        case METHOD_ATTRIBUTE_PRIVATE: outPut << "private "; break;
        case METHOD_ATTRIBUTE_PUBLIC: outPut << "public "; break;
        case METHOD_ATTRIBUTE_FAMILY: outPut << "protected "; break;
        case METHOD_ATTRIBUTE_ASSEM:
        case METHOD_ATTRIBUTE_FAM_AND_ASSEM: outPut << "internal "; break;
        case METHOD_ATTRIBUTE_FAM_OR_ASSEM: outPut << "protected internal "; break;
    }
    if (flags & METHOD_ATTRIBUTE_STATIC) outPut << "static ";
    if (flags & METHOD_ATTRIBUTE_ABSTRACT) {
        outPut << "abstract ";
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT) outPut << "override ";
    } else if (flags & METHOD_ATTRIBUTE_FINAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT) outPut << "sealed override ";
    } else if (flags & METHOD_ATTRIBUTE_VIRTUAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_NEW_SLOT) outPut << "virtual ";
        else outPut << "override ";
    }
    if (flags & METHOD_ATTRIBUTE_PINVOKE_IMPL) outPut << "extern ";
    return outPut.str();
}

bool _il2cpp_type_is_byref(const Il2CppType *type) {
    auto byref = type->byref;
    if (il2cpp_type_is_byref) byref = il2cpp_type_is_byref(type);
    return byref;
}

std::string get_field_default_value(FieldInfo *field, const Il2CppType *field_type) {
    std::stringstream outPut;
    if (!il2cpp_field_static_get_value) return outPut.str();
    uint64_t val = 0;
    il2cpp_field_static_get_value(field, &val);
    switch (field_type->type) {
        case IL2CPP_TYPE_BOOLEAN: outPut << ((val & 0xff) ? "true" : "false"); break;
        case IL2CPP_TYPE_CHAR: outPut << (uint32_t)(uint16_t)val; break;
        case IL2CPP_TYPE_I1: outPut << (int32_t)(int8_t)val; break;
        case IL2CPP_TYPE_U1: outPut << (uint32_t)(uint8_t)val; break;
        case IL2CPP_TYPE_I2: outPut << (int32_t)(int16_t)val; break;
        case IL2CPP_TYPE_U2: outPut << (uint32_t)(uint16_t)val; break;
        case IL2CPP_TYPE_I4: outPut << (int32_t)val; break;
        case IL2CPP_TYPE_U4: outPut << (uint32_t)val; break;
        case IL2CPP_TYPE_I8: outPut << (int64_t)val; break;
        case IL2CPP_TYPE_U8: outPut << val; break;
        case IL2CPP_TYPE_R4: { float f = 0; memcpy(&f, &val, sizeof(f)); outPut << f; break; }
        case IL2CPP_TYPE_R8: { double d = 0; memcpy(&d, &val, sizeof(d)); outPut << d; break; }
        case IL2CPP_TYPE_STRING: {
            auto str = (Il2CppString *)val;
            if (!str) { outPut << "null"; }
            else if (il2cpp_string_chars && il2cpp_string_length) {
                auto chars = il2cpp_string_chars(str);
                auto len = il2cpp_string_length(str);
                outPut << "\"";
                for (int i = 0; i < len; ++i) {
                    Il2CppChar c = chars[i];
                    switch (c) {
                        case '\0': outPut << "\\0"; break;
                        case '\"': outPut << "\\\""; break;
                        case '\n': outPut << "\\n"; break;
                        case '\r': outPut << "\\r"; break;
                        case '\t': outPut << "\\t"; break;
                        default:
                            if (c >= 0x20 && c < 0x7f) outPut << (char)c;
                            else { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", c); outPut << buf; }
                    }
                }
                outPut << "\"";
            }
            break;
        }
        default: break;
    }
    return outPut.str();
}

/* ═══════════════════════════════════════════════════════════
   DUMP FIELD / METHOD / TYPE
   ═══════════════════════════════════════════════════════════ */
std::string dump_field(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Fields\n";
    void *iter = nullptr;
    while (auto field = il2cpp_class_get_fields(klass, &iter)) {
        outPut << "\t";
        auto attrs = il2cpp_field_get_flags(field);
        auto access = attrs & FIELD_ATTRIBUTE_FIELD_ACCESS_MASK;
        switch (access) {
            case FIELD_ATTRIBUTE_PRIVATE: outPut << "private "; break;
            case FIELD_ATTRIBUTE_PUBLIC: outPut << "public "; break;
            case FIELD_ATTRIBUTE_FAMILY: outPut << "protected "; break;
            case FIELD_ATTRIBUTE_ASSEMBLY:
            case FIELD_ATTRIBUTE_FAM_AND_ASSEM: outPut << "internal "; break;
            case FIELD_ATTRIBUTE_FAM_OR_ASSEM: outPut << "protected internal "; break;
        }
        if (attrs & FIELD_ATTRIBUTE_LITERAL) outPut << "const ";
        else if (attrs & FIELD_ATTRIBUTE_STATIC) outPut << "static ";
        else if (attrs & FIELD_ATTRIBUTE_INIT_ONLY) outPut << "readonly ";

        auto field_type = il2cpp_field_get_type(field);
        auto field_class = il2cpp_class_from_type(field_type);
        outPut << il2cpp_class_get_name(field_class) << " " << il2cpp_field_get_name(field);

        if (attrs & FIELD_ATTRIBUTE_LITERAL) {
            auto is_enum = il2cpp_class_is_enum(klass);
            if (is_enum) {
                uint64_t val = 0;
                il2cpp_field_static_get_value(field, &val);
                outPut << " = " << std::dec << val;
            } else {
                auto default_value = get_field_default_value(field, field_type);
                if (!default_value.empty()) outPut << " = " << default_value;
            }
        }
        outPut << "; // 0x" << std::hex << il2cpp_field_get_offset(field) << "\n";
    }
    return outPut.str();
}

std::string dump_method(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Methods\n";
    void *iter = nullptr;
    while (auto method = il2cpp_class_get_methods(klass, &iter)) {
        if (method->methodPointer) {
            outPut << "\t// RVA: 0x" << std::hex << ((uint64_t)method->methodPointer - il2cpp_base);
            outPut << " VA: 0x" << std::hex << (uint64_t)method->methodPointer << "\n";
        } else {
            outPut << "\t// RVA: 0x VA: 0x0\n";
        }
        outPut << "\t" << get_method_modifier(il2cpp_method_get_flags(method, nullptr));
        auto return_class = il2cpp_class_from_type(il2cpp_method_get_return_type(method));
        outPut << il2cpp_class_get_name(return_class) << " " << il2cpp_method_get_name(method) << "();\n";
    }
    return outPut.str();
}

std::string dump_type(const Il2CppType *type) {
    std::stringstream outPut;
    auto *klass = il2cpp_class_from_type(type);
    outPut << "\n// Namespace: " << il2cpp_class_get_namespace(klass) << "\n";
    auto flags = il2cpp_class_get_flags(klass);
    if (flags & TYPE_ATTRIBUTE_SERIALIZABLE) outPut << "[Serializable]\n";
    auto is_valuetype = il2cpp_class_is_valuetype(klass);
    auto is_enum = il2cpp_class_is_enum(klass);
    auto visibility = flags & TYPE_ATTRIBUTE_VISIBILITY_MASK;
    switch (visibility) {
        case TYPE_ATTRIBUTE_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_PUBLIC: outPut << "public "; break;
        case TYPE_ATTRIBUTE_NOT_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_FAM_AND_ASSEM:
        case TYPE_ATTRIBUTE_NESTED_ASSEMBLY: outPut << "internal "; break;
        case TYPE_ATTRIBUTE_NESTED_PRIVATE: outPut << "private "; break;
        case TYPE_ATTRIBUTE_NESTED_FAMILY: outPut << "protected "; break;
        case TYPE_ATTRIBUTE_NESTED_FAM_OR_ASSEM: outPut << "protected internal "; break;
    }
    if (flags & TYPE_ATTRIBUTE_ABSTRACT && flags & TYPE_ATTRIBUTE_SEALED) outPut << "static ";
    else if (!(flags & TYPE_ATTRIBUTE_INTERFACE) && flags & TYPE_ATTRIBUTE_ABSTRACT) outPut << "abstract ";
    else if (!is_valuetype && !is_enum && flags & TYPE_ATTRIBUTE_SEALED) outPut << "sealed ";

    if (flags & TYPE_ATTRIBUTE_INTERFACE) outPut << "interface ";
    else if (is_enum) outPut << "enum ";
    else if (is_valuetype) outPut << "struct ";
    else outPut << "class ";

    outPut << il2cpp_class_get_name(klass);
    std::vector<std::string> extends;
    auto parent = il2cpp_class_get_parent(klass);
    if (!is_valuetype && !is_enum && parent) {
        auto parent_type = il2cpp_class_get_type(parent);
        if (parent_type->type != IL2CPP_TYPE_OBJECT) {
            extends.emplace_back(il2cpp_class_get_name(parent));
        }
    }
    void *iter = nullptr;
    while (auto itf = il2cpp_class_get_interfaces(klass, &iter)) {
        extends.emplace_back(il2cpp_class_get_name(itf));
    }
    if (!extends.empty()) {
        outPut << " : " << extends[0];
        for (size_t i = 1; i < extends.size(); ++i) outPut << ", " << extends[i];
    }
    outPut << "\n{\n";
    outPut << dump_field(klass);
    outPut << dump_method(klass);
    outPut << "}\n";
    return outPut.str();
}

/* ═══════════════════════════════════════════════════════════
   API INIT + DUMP GUARD
   ═══════════════════════════════════════════════════════════ */
void il2cpp_api_init(void *handle) {
    LOGI("il2cpp_handle: %p", handle);
    init_il2cpp_api(handle);
    xdl_info_t xinfo{};
    if (xdl_info(handle, XDL_DI_DLINFO, &xinfo) == 0 && xinfo.dli_fbase) {
        il2cpp_base = reinterpret_cast<uint64_t>(xinfo.dli_fbase);
    } else if (il2cpp_domain_get_assemblies) {
        Dl_info dlInfo;
        if (dladdr((void*)il2cpp_domain_get_assemblies, &dlInfo)) {
            il2cpp_base = reinterpret_cast<uint64_t>(dlInfo.dli_fbase);
        }
    }
    LOGI("il2cpp_base: %" PRIx64, il2cpp_base);
    if (il2cpp_is_vm_thread) {
        for (int i = 0; i < 120 && !il2cpp_is_vm_thread(nullptr); ++i) {
            LOGI("Waiting for il2cpp_init...");
            sleep(1);
        }
    }
    if (il2cpp_domain_get && il2cpp_thread_attach) {
        auto domain = il2cpp_domain_get();
        if (domain) il2cpp_thread_attach(domain);
    }
}

static sigjmp_buf g_dump_jmp;
static volatile sig_atomic_t g_dump_guard_active = 0;
static pid_t g_dump_tid = 0;
static struct sigaction g_old_segv{};
static struct sigaction g_old_bus{};

static void dump_fault_handler(int sig, siginfo_t *info, void *ucontext) {
    if (g_dump_guard_active && gettid() == g_dump_tid) {
        siglongjmp(g_dump_jmp, sig);
    }
    struct sigaction *old = (sig == SIGBUS) ? &g_old_bus : &g_old_segv;
    if (old->sa_flags & SA_SIGINFO) {
        if (old->sa_sigaction) old->sa_sigaction(sig, info, ucontext);
    } else if (old->sa_handler == SIG_IGN || old->sa_handler == SIG_DFL) {
        signal(sig, SIG_DFL);
        raise(sig);
    } else if (old->sa_handler) {
        old->sa_handler(sig);
    }
}

static void install_dump_guard() {
    g_dump_tid = gettid();
    struct sigaction sa{};
    sa.sa_sigaction = dump_fault_handler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &g_old_segv);
    sigaction(SIGBUS, &sa, &g_old_bus);
}

static void remove_dump_guard() {
    g_dump_guard_active = 0;
    sigaction(SIGSEGV, &g_old_segv, nullptr);
    sigaction(SIGBUS, &g_old_bus, nullptr);
}

/* ═══════════════════════════════════════════════════════════
   MAIN DUMP FUNCTION — writes output.cs with metadata
   ═══════════════════════════════════════════════════════════ */
void il2cpp_dump(const char *outDir) {
    LOGI("dumping...");

    /* 1. Load input.json targets */
    load_input_config(outDir);

    if (!il2cpp_domain_get || !il2cpp_domain_get_assemblies ||
        !il2cpp_assembly_get_image || !il2cpp_image_get_name) {
        LOGE("essential il2cpp api missing; cannot dump");
        return;
    }

    size_t size = 0;
    auto domain = il2cpp_domain_get();
    if (!domain) {
        LOGE("il2cpp_domain_get returned null");
        return;
    }
    auto assemblies = il2cpp_domain_get_assemblies(domain, &size);
    if (!assemblies || size == 0) {
        LOGE("no assemblies");
        return;
    }

    /* 2. Open output.cs in game's files/ dir */
    auto outPath = std::string(outDir).append("/files/output.cs");
    FILE* outFile = fopen(outPath.c_str(), "w");
    if (!outFile) {
        LOGE("failed to open %s", outPath.c_str());
        return;
    }

    /* 3. Write metadata header */
    fprintf(outFile, "// ═══════════════════════════════════════════════════════════\n");
    fprintf(outFile, "// FIREHAWK TARGETED DUMP\n");
    fprintf(outFile, "// Generated: %ld\n", (long)time(nullptr));
    fprintf(outFile, "// PID: %d\n", (int)getpid());
    fprintf(outFile, "// PACKAGE: com.dts.freefiremax\n");
    fprintf(outFile, "// MODE: %s\n", g_full_dump ? "FULL" : "TARGETED");
    fprintf(outFile, "// ═══════════════════════════════════════════════════════════\n\n");

    /* 4. Dump assemblies list */
    for (int i = 0; i < (int)size; ++i) {
        auto image = il2cpp_assembly_get_image(assemblies[i]);
        fprintf(outFile, "// Image %d: %s\n", i, il2cpp_image_get_name(image));
    }
    fprintf(outFile, "\n");

    /* 5. Iterate classes — filter by input.json */
    if (!il2cpp_image_get_class) {
        LOGW("il2cpp_image_get_class missing — cannot enumerate classes");
        fclose(outFile);
        return;
    }

    LOGI("Version >= 2018.3 — enumerating classes");
    install_dump_guard();
    int skipped = 0, dumped = 0;

    for (int i = 0; i < (int)size; ++i) {
        auto image = il2cpp_assembly_get_image(assemblies[i]);
        auto classCount = il2cpp_image_get_class_count(image);

        for (int j = 0; j < (int)classCount; ++j) {
            g_dump_guard_active = 1;
            if (sigsetjmp(g_dump_jmp, 1) == 0) {
                auto klass = il2cpp_image_get_class(image, j);
                if (!klass) { ++skipped; continue; }

                const char* class_name = il2cpp_class_get_name(klass);
                if (!is_target_class(class_name)) continue;

                auto type = il2cpp_class_get_type(const_cast<Il2CppClass*>(klass));
                if (!type) { ++skipped; continue; }

                std::string out = dump_type(type);
                fputs(out.c_str(), outFile);
                ++dumped;
            } else {
                ++skipped;
                LOGW("skipped decoy/broken class image=%d index=%d", i, j);
            }
            g_dump_guard_active = 0;
        }
    }

    remove_dump_guard();
    fclose(outFile);
    LOGI("dump finished: %d classes dumped, %d skipped → %s", dumped, skipped, outPath.c_str());
}
