#include "orpheus_abi.h"

int main() {
    return (ORPHEUS_ABI_VERSION == 1) ? 0 : 1;
}
