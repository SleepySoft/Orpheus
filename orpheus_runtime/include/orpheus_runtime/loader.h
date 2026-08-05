#ifndef ORPHEUS_RUNTIME_LOADER_H
#define ORPHEUS_RUNTIME_LOADER_H

#include "orpheus_abi.h"

#include <string>

namespace orpheus {

// Opaque library handle
struct LibraryHandle;

class ComponentLoader {
public:
    ComponentLoader();
    ~ComponentLoader();

    // Load a component library and return its interface.
    // The interface lifetime is tied to the library.
    const OrpheusComponentInterface* load(const std::string& library_path);

    // Close all loaded libraries.
    void unload_all();

private:
    struct Impl;
    Impl* impl_;
};

} // namespace orpheus

#endif
