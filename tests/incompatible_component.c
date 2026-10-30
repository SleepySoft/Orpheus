#include "orpheus_abi.h"

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.test.old_abi", .version="1.0.0", .abi_version=4
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static const OrpheusComponentInterface interface_ = {.get_descriptor=get_descriptor};
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void) { return &interface_; }
