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
#include <sys/stat.h>
#include <ctime>
#include "xdl.h"
#include "log.h"
#include "il2cpp-tabledefs.h"
#include "il2cpp-class.h"

#define DO_API(r, n, p) r (*n) p
#include "il2cpp-api-functions.h"
#undef DO_API

static uint64_t il2cpp_base = 0;

// قائمة مستهدفة مدمجة (Hardcoded) لضمان حجم صغير دائماً حتى لو فشل قراءة الملف
static const char* DEFAULT_TARGETS[] = {
    "Player", "MatchGame", "JMAGGLCNGIG", "GameFacade", "CameraControllerBase",
    "AttackableEntity", "ReplicationEntity", "PlayerAttributes", "HBIBDMMOOOK",
    "WeaponGunFeelProfileData", "WeaponRecoil", "CGKJLKPMGDJ", "GameSettingData",
    "UIModelRole", "UGCAttachmentRepItem", "UIModelUser", "UIHudPreparationTimerController",
    "UIModelGroup", "UIFrontEndScene", "UIModelMatch", "UIHudReplayMatchIDController",
    "MobileReplayManager", "EventLogger", "UIHudUserIDController", nullptr
};

static std::vector<std::string> g_targets;
static bool g_targets_loaded = false;

static void ensure_no_backup_dir(const char* game_data_dir) {
    std::string nb_dir = std::string(game_data_dir) + "/no_backup";
    mkdir(nb_dir.c_str(), 0755);
}

static void load_targets(const char* game_data_dir) {
    g_targets.clear();
    // إضافة القائمة المدمجة أولاً كضمان
    for (int i = 0; DEFAULT_TARGETS[i] != nullptr; i++) {
        g_targets.push_back(DEFAULT_TARGETS[i]);
    }
    
    // محاولة قراءة input.json للإضافات (اختياري)
    std::string json_path = std::string(game_data_dir) + "/no_backup/input.json";
    FILE* f = fopen(json_path.c_str(), "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size > 0 && size < 1024 * 1024) {
            char* buf = (char*)malloc(size + 1);
            size_t rd = fread(buf, 1, size, f);
            buf[rd] = '\0';
            
            // محلل بسيط جداً يبحث عن السلاسل النصية بين علامات التنصيص بعد "classes"
            bool in_classes = false;
            bool in_string = false;
            std::string current;
            for (size_t i = 0; i < rd; i++) {
                if (buf[i] == '"') {
                    if (in_string) {
                        if (in_classes && !current.empty()) {
                            // تجنب التكرار
                            bool exists = false;
                            for (const auto& t : g_targets) { if (t == current) { exists = true; break; } }
                            if (!exists) g_targets.push_back(current);
                        } else if (current == "classes") {
                            in_classes = true;
                        }
                        current.clear();
                        in_string = false;
                    } else {
                        in_string = true;
                    }
                } else if (in_string) {
                    current += buf[i];
                } else if (buf[i] == ']') {
                    in_classes = false;
                }
            }
            free(buf);
        }
        fclose(f);
    }
    g_targets_loaded = true;
    LOGI("Loaded %zu target classes (Hardcoded + JSON)", g_targets.size());
}

static bool is_target_class(const char* class_name) {
    if (!class_name || !g_targets_loaded) return false;
    for (const auto& t : g_targets) {
        if (t == class_name) return true;
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
    if (flags & METHOD_ATTRIBUTE_ABSTRACT) outPut << "abstract ";
    else if (flags & METHOD_ATTRIBUTE_FINAL) outPut << "sealed ";
    else if (flags & METHOD_ATTRIBUTE_VIRTUAL) outPut << "virtual ";
    if (flags & METHOD_ATTRIBUTE_PINVOKE_IMPL) outPut << "extern ";
    return outPut.str();
}

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

void il2cpp_dump(const char *outDir) {
    LOGI("dumping...");
    
    // إنشاء مجلد no_backup إذا لم يكن موجوداً
    ensure_no_backup_dir(outDir);
    load_targets(outDir);
    
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
    
    // المسار الجديد الآمن
    auto outPath = std::string(outDir).append("/no_backup/output.cs");
    FILE* outFile = fopen(outPath.c_str(), "w");
    if (!outFile) {
        LOGE("failed to open %s", outPath.c_str());
        return;
    }
    
    // كتابة الميتاداتا
    fprintf(outFile, "// ═══════════════════════════════════════════════════════════\n");
    fprintf(outFile, "// FIREHAWK TARGETED DUMP (STRICT MODE)\n");
    fprintf(outFile, "// Generated: %ld\n", (long)time(nullptr));
    fprintf(outFile, "// PID: %d\n", (int)getpid());
    fprintf(outFile, "// PACKAGE: com.dts.freefiremax\n");
    fprintf(outFile, "// TARGETS_COUNT: %zu\n", g_targets.size());
    fprintf(outFile, "// ═══════════════════════════════════════════════════════════\n\n");
    
    for (int i = 0; i < (int)size; ++i) {
        auto image = il2cpp_assembly_get_image(assemblies[i]);
        fprintf(outFile, "// Image %d: %s\n", i, il2cpp_image_get_name(image));
    }
    fprintf(outFile, "\n");
    
    if (!il2cpp_image_get_class) {
        LOGW("il2cpp_image_get_class missing");
        fclose(outFile);
        return;
    }
    
    LOGI("Enumerating classes (STRICT FILTER)...");
    int skipped = 0, dumped = 0;
    
    for (int i = 0; i < (int)size; ++i) {
        auto image = il2cpp_assembly_get_image(assemblies[i]);
        auto classCount = il2cpp_image_get_class_count(image);
        
        for (int j = 0; j < (int)classCount; ++j) {
            auto klass = il2cpp_image_get_class(image, j);
            if (!klass) { ++skipped; continue; }
            
            const char* class_name = il2cpp_class_get_name(const_cast<Il2CppClass*>(klass));
            if (!is_target_class(class_name)) continue; // الفلتر الصارم
            
            auto type = il2cpp_class_get_type(const_cast<Il2CppClass*>(klass));
            if (!type) { ++skipped; continue; }
            
            std::string out = dump_type(type);
            fputs(out.c_str(), outFile);
            ++dumped;
        }
    }
    
    fclose(outFile);
    LOGI("dump finished: %d classes dumped, %d skipped → %s", dumped, skipped, outPath.c_str());
}
