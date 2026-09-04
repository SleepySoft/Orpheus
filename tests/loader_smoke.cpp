#include "orpheus_runtime/loader.h"

#include <cstring>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) return 2;

    orpheus::ComponentLoader loader;
    if (loader.load("definitely_missing_orpheus_component") != nullptr) return 3;

    const OrpheusComponentInterface* iface = loader.load(argv[1]);
    if (iface == nullptr || iface->get_descriptor == nullptr) return 4;
    const OrpheusComponentDescriptor* descriptor = iface->get_descriptor();
    if (descriptor == nullptr || descriptor->id == nullptr) return 5;
    if (std::strcmp(descriptor->id, "orpheus.builtin.gain") != 0) return 6;
    if (descriptor->abi_version != ORPHEUS_ABI_VERSION) return 7;

    loader.unload_all();
    return 0;
}
