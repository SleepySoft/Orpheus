#include "orpheus_runtime/runtime.h"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 4) return 1;
    try {
        const orpheus::Plan plan = orpheus::Plan::load_from_file(argv[1]);
        orpheus::Runtime runtime;
        if (runtime.load_plan(plan, argv[2]) != 0) return 2;
        for (int i = 0; i < 7; ++i)
            if (runtime.process_task("default", plan.block_size) != 0) return 3;
        float metrics[32];
        if (runtime.get_bulk(argv[3], "metrics", metrics, 32) != 0) return 4;
        std::cout << "BULKVALUE " << argv[3] << " metrics";
        for (float value : metrics) std::cout << " " << value;
        std::cout << std::endl;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 5;
    }
    return 0;
}
