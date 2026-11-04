#include "orpheus_abi.h"

int main(void) {
    return (ORPHEUS_ABI_VERSION == 6) ? 0 : 1;
}
