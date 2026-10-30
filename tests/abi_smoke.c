#include "orpheus_abi.h"

int main(void) {
    return (ORPHEUS_ABI_VERSION == 5) ? 0 : 1;
}
