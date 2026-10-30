#include "orpheus_runtime/loader.h"

#include <vector>
#include <string>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace orpheus {

struct LoadedLib {
    std::string path;
#ifdef _WIN32
    HMODULE handle;
#else
    void* handle;
#endif
};

struct ComponentLoader::Impl {
    std::vector<LoadedLib> libs;
};

ComponentLoader::ComponentLoader() : impl_(new Impl()) {}

ComponentLoader::~ComponentLoader() {
    unload_all();
    delete impl_;
}

const OrpheusComponentInterface* ComponentLoader::load(const std::string& library_path) {
#ifdef _WIN32
    HMODULE h = LoadLibraryA(library_path.c_str());
    if (!h) {
        DWORD err = GetLastError();
        std::cerr << "[Loader] LoadLibraryA failed for " << library_path << ", error=" << err << std::endl;
        return nullptr;
    }
    OrpheusGetInterfaceFn fn = reinterpret_cast<OrpheusGetInterfaceFn>(
        GetProcAddress(h, "orpheus_get_interface"));
    if (!fn) {
        DWORD err = GetLastError();
        std::cerr << "[Loader] GetProcAddress failed for " << library_path << ", error=" << err << std::endl;
        FreeLibrary(h);
        return nullptr;
    }
#else
    void* h = dlopen(library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        return nullptr;
    }
    OrpheusGetInterfaceFn fn = reinterpret_cast<OrpheusGetInterfaceFn>(
        dlsym(h, "orpheus_get_interface"));
    if (!fn) {
        dlclose(h);
        return nullptr;
    }
#endif
    const OrpheusComponentInterface* interface_ = fn();
    const OrpheusComponentDescriptor* descriptor =
        interface_ && interface_->get_descriptor ? interface_->get_descriptor() : nullptr;
    if (!descriptor || descriptor->abi_version != ORPHEUS_ABI_VERSION) {
        std::cerr << "[Loader] ABI不兼容：" << library_path
                  << "，需要 " << ORPHEUS_ABI_VERSION << "，实际 "
                  << (descriptor ? descriptor->abi_version : 0) << std::endl;
#ifdef _WIN32
        FreeLibrary(h);
#else
        dlclose(h);
#endif
        return nullptr;
    }
    LoadedLib lib;
    lib.path = library_path;
    lib.handle = h;
    impl_->libs.push_back(lib);
    return interface_;
}

void ComponentLoader::unload_all() {
    for (auto& lib : impl_->libs) {
#ifdef _WIN32
        FreeLibrary(lib.handle);
#else
        dlclose(lib.handle);
#endif
    }
    impl_->libs.clear();
}

} // namespace orpheus
