#include "orpheus_runtime/runtime.h"
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 5 || (std::string(argv[3]) != "global" && std::string(argv[3]) != "task")) {
        std::cerr << "参数：plan.json component_dir global|task node...\n";
        return 1;
    }
    const orpheus::Plan plan = orpheus::Plan::load_from_file(argv[1]);
    orpheus::Runtime runtime;
    if (runtime.load_plan(plan, argv[2]) != ORPHEUS_OK) return 1;
    const unsigned blocks[] = {160, 1, 7, 5, 4, 8};
    const int32_t delays[] = {0, 0, 6, 1, 3, -1};
    for (unsigned step = 0; step < 6; ++step) {
        if (step) {
            OrpheusValue value = {};
            value.type = ORPHEUS_VALUE_INT;
            value.value.i32 = delays[step];
            for (int i = 4; i < argc; ++i) {
                OrpheusValue maximum = {};
                if (runtime.get_parameter(argv[i], "max_delay_samples", &maximum) != ORPHEUS_OK) return 1;
                value.value.i32 = step == 2 ? maximum.value.i32 : delays[step];
                if (value.value.i32 > maximum.value.i32) value.value.i32 = maximum.value.i32;
                int rc = runtime.set_parameter(argv[i], "delay_samples", value);
                if (rc != ORPHEUS_OK) {
                    std::cerr << argv[i] << ": SET错误 " << rc << "\n";
                    return 1;
                }
            }
            if (step == 4) {
                for (int i = 4; i < argc; ++i) {
                    OrpheusValue channels = {};
                    if (runtime.get_parameter(argv[i], "channels", &channels) != ORPHEUS_OK) return 1;
                    std::vector<float> delays(static_cast<size_t>(channels.value.i32));
                    for (size_t c = 0; c < delays.size(); ++c) delays[c] = float(c % 2 + 1);
                    int rc = runtime.write_bulk(argv[i], "delays_samples", delays.data(), delays.size());
                    if (rc != ORPHEUS_OK) {
                        std::cerr << argv[i] << ": BULK写入错误 " << rc << "\n";
                        return 1;
                    }
                }
            }
        }
        for (unsigned i = 0; i < blocks[step]; ++i) {
            int rc = std::string(argv[3]) == "task"
                ? runtime.process_task("default", 0) : runtime.process_block(plan.block_size);
            if (rc != ORPHEUS_OK) {
                std::cerr << "执行错误 " << rc << "\n";
                return 1;
            }
        }
    }
    return 0;
}
