#include "orpheus_abi.h"

int main(void) {
    return (ORPHEUS_ABI_VERSION == 7) ? 0 : 1;
}
