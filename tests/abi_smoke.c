#include "orpheus_abi.h"

int main(void) {
    return (ORPHEUS_ABI_VERSION == 4) ? 0 : 1;
}
