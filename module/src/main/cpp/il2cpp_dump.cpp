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

// Targeted dumping structures
struct TargetClass {
    std::string class_name;
    std::vector<std::string> fields;
};

static std::vector<TargetClass> g_targets;
static std::string g_package_name = "com.dts.freefiremax";

// Load targets from file
static void load_targets(const char* game_data_dir) {
    g_targets.clear();
    
    // Build path: /data/data/[package]/files/targets.txt
    std::string path = std::string(game_data_dir) + "/files/targets.txt";
    
    std::ifstream file(path);
        LOGW("Cannot open %s, dumping all classes", path.c_str());
        return;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        
        // Format: ClassName|Field1,Field2,Field3
        size_t pipe_pos = line.find('|');
        if (pipe_pos == std::string::npos) continue;
        
        TargetClass target;
        target.class_name = line.substr(0, pipe_pos);
        
        std::string fields_str = line.substr(pipe_pos + 1);
        std::stringstream ss(fields_str);
        std::string field;
        while (std::getline(ss, field, ',')) {
                target.fields.push_back(field);
            }
        }
        
        g_targets.push_back(target);
    }
    
    file.close();
    LOGI("Loaded %zu target classes from %s", g_targets.size(), path.c_str());
}

// Check if class is targeted
static bool is_target_class(const char* class_name) {
    if (g_targets.empty()) return true; // No targets = dump all
    
    std::string name_str(class_name);
    for (const auto& target : g_targets) {
        if (name_str == target.class_name) return true;
    }
    return false;
}

// Check if field is targeted
static bool is_target_field(const char* class_name, const char* field_name) {
    
    std::string cls(class_name);
    std::string fld(field_name);
    
    for (const auto& target : g_targets) {
        if (target.class_name == cls) {
            if (target.fields.empty()) return true; // No fields specified = dump all
            for (const auto& f : target.fields) {
                if (fld == f) return true;
            }
            return false;
        }
    }
    return false;
}

void init_il2cpp_api(void *handle) {
#define DO_API(r, n, p) { n = (r (*) p)xdl_sym(handle, #n, nullptr); if(!n) LOGW("api not found %s", #n); }
#include "il2cpp-api-functions.h"
#undef DO_API
}

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
    uint64_t val = 0;
    il2cpp_field_static_get_value(field, &val);
    switch (field_type->type) {
        case IL2CPP_TYPE_BOOLEAN: outPut << ((val & 0xff) ? "true" : "false"); break;
        case IL2CPP_TYPE_CHAR: outPut << (uint32_t) (uint16_t) val; break;
        case IL2CPP_TYPE_I1: outPut << (int32_t) (int8_t) val; break;
        case IL2CPP_TYPE_U1: outPut << (uint32_t) (uint8_t) val; break;
        case IL2CPP_TYPE_I2: outPut << (int32_t) (int16_t) val; break;
        case IL2CPP_TYPE_U2: outPut << (uint32_t) (uint16_t) val; break;
        case IL2CPP_TYPE_I4: outPut << (int32_t) val; break;
        case IL2CPP_TYPE_U4: outPut << (uint32_t) val; break;
        case IL2CPP_TYPE_I8: outPut << (int64_t) val; break;
        case IL2CPP_TYPE_U8: outPut << val; break;
        case IL2CPP_TYPE_R4: { float f = 0; memcpy(&f, &val, sizeof(f)); outPut << f; break; }
        case IL2CPP_TYPE_R8: { double d = 0; memcpy(&d, &val, sizeof(d)); outPut << d; break; }
        case IL2CPP_TYPE_STRING: {
            auto str = (Il2CppString *) val;
            else if (il2cpp_string_chars && il2cpp_string_length) {
                auto chars = il2cpp_string_chars(str);
                auto len = il2cpp_string_length(str);
                outPut << "\"";
                for (int i = 0; i < len; ++i) {
                    Il2CppChar c = chars[i];
                    switch (c) {
                        case '\0': outPut << "\0"; break;
                        case '\"': outPut << "\\""; break;
                        case '\n': outPut << "\n"; break;
                        case '\r': outPut << "\r"; break;
                        case '\t': outPut << "\t"; break;
                        default:
                            if (c >= 0x20 && c < 0x7f) outPut << (char) c;
                            else { char buf[8]; snprintf(buf, sizeof(buf), "\u%04x", c); outPut << buf; }
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

std::string dump_method(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Methods\n";
    void *iter = nullptr;
    while (auto method = il2cpp_class_get_methods(klass, &iter)) {
        if (method->methodPointer) {
            outPut << "\t// RVA: 0x" << std::hex << ((uint64_t)method->methodPointer - il2cpp_base);
            outPut << " VA: 0x" << std::hex << (uint64_t)method->methodPointer << "\n";
        }
        outPut << "\t" << get_method_modifier(il2cpp_method_get_flags(method, nullptr));
        auto return_class = il2cpp_class_from_type(il2cpp_method_get_return_type(method));
        outPut << il2cpp_class_get_name(return_class) << " " << il2cpp_method_get_name(method) << "();\n";
    }
    return outPut.str();
}

std::string dump_field(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Fields\n";
    void *iter = nullptr;
    while (auto field = il2cpp_class_get_fields(klass, &iter)) {
        const char* field_name = il2cpp_field_get_name(field);
        
        // Check if field is targeted
            continue;
        }
        
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
        outPut << il2cpp_class_get_name(field_class) << " " << field_name;
        outPut << "; // 0x" << std::hex << il2cpp_field_get_offset(field) << "\n";
    }
    return outPut.str();
}

std::string dump_type(const Il2CppType *type) {
    std::stringstream outPut;
    auto *klass = il2cpp_class_from_type(type);
    outPut << "\n// Namespace: " << il2cpp_class_get_namespace(klass) << "\n";
    outPut << "public class " << il2cpp_class_get_name(klass) << "\n{\n";
    outPut << dump_field(klass);
    outPut << dump_method(klass);
    outPut << "}\n";
    return outPut.str();
}

void il2cpp_api_init(void *handle) {
    LOGI("il2cpp_handle: %p", handle);
    init_il2cpp_api(handle);
    xdl_info_t xinfo{};
    if (xdl_info(handle, XDL_DI_DLINFO, &xinfo) == 0 && xinfo.dli_fbase) {
        il2cpp_base = reinterpret_cast<uint64_t>(xinfo.dli_fbase);
    }
    LOGI("il2cpp_base: %" PRIx64, il2cpp_base);
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

void il2cpp_dump(const char *outDir) {
    LOGI("dumping...");
    
    // Load targets
    load_targets(outDir);
    
        LOGE("essential il2cpp api missing; cannot dump");
        return;
    }
    
    size_t size = 0;
    auto domain = il2cpp_domain_get();
        LOGE("il2cpp_domain_get returned null; cannot dump");
        return;
    }
    
    auto assemblies = il2cpp_domain_get_assemblies(domain, &size);
        LOGE("no assemblies; cannot dump");
        return;
    }
    
    // Output path: /data/data/[package]/files/output.cs
    auto outPath = std::string(outDir).append("/files/output.cs");
    std::ofstream outStream(outPath);
        LOGE("failed to open dump file: %s", outPath.c_str());
        return;
    }
    
    // Write metadata
    outStream << "// ═══════════════════════════════════════════════════════════\n";
    outStream << "// FIREHAWK TARGETED DUMP\n";
    outStream << "// PID: " << getpid() << "\n";
    outStream << "// TIMESTAMP: " << std::time(nullptr) << "\n";
    outStream << "// PACKAGE: com.dts.freefiremax\n";
    outStream << "// ═══════════════════════════════════════════════════════════\n\n";
    
    // Dump targeted classes
    install_dump_guard();
    int dumped = 0;
    int skipped = 0;
    
    for (int i = 0; i < size; ++i) {
        auto image = il2cpp_assembly_get_image(assemblies[i]);
        auto classCount = il2cpp_image_get_class_count(image);
        
        for (int j = 0; j < classCount; ++j) {
            g_dump_guard_active = 1;
            if (sigsetjmp(g_dump_jmp, 1) == 0) {
                auto klass = il2cpp_image_get_class(image, j);
                    ++skipped;
                    continue;
                }
                
                const char* class_name = il2cpp_class_get_name(klass);
                    continue;
                }
                
                auto type = il2cpp_class_get_type(const_cast<Il2CppClass *>(klass));
                    ++skipped;
                    continue;
                }
                
                outStream << dump_type(type);
                ++dumped;
            } else {
                ++skipped;
                LOGW("skipped decoy/broken class image=%d index=%d", i, j);
            }
            g_dump_guard_active = 0;
        }
    }
    
    remove_dump_guard();
    outStream.close();
    
    LOGI("dump finished: %d classes dumped, %d skipped", dumped, skipped);
    LOGI("output: %s", outPath.c_str());
}
