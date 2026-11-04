#include "orpheus_bridge_bulk.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "并发分片测试失败：%s（行 %d）\n", #condition, __LINE__); return 1; \
} } while (0)

static thread_local bool pause_boundary = false;

struct Harness {
    uint32_t live[2048]{}, coefficients[2048]{};
    std::atomic<bool> stop{false}, torn{false};
    std::atomic<bool> boundary_entered{false}, release_boundary{false};
    OrpheusBridgeBulkState* bulk;
    std::thread realtime;

    static int target(void* context, uint32_t id, OrpheusBridgeBulkTarget* output) {
        Harness* self = static_cast<Harness*>(context);
        if (id != 1 && id != 2) return ORPHEUS_ERR_NOT_FOUND;
        *output = {};
        output->active = id == 1 ? self->live : self->coefficients;
        output->type = ORPHEUS_VALUE_INT;
        output->count = 2048;
        output->writable = id == 2;
        output->task = "task";
        return ORPHEUS_OK;
    }

    static uint64_t clock_ms(void* context) {
        Harness* self = static_cast<Harness*>(context);
        if (pause_boundary) {
            /* 仅测试中暂停时钟回调，确定性复现“取消时另一 Task 已占住邮箱”的竞态。 */
            self->boundary_entered.store(true);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!self->release_boundary.load() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
        }
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    explicit Harness(bool start = true) : bulk(orpheus_bridge_bulk_create(this, target, clock_ms)) {
        if (bulk == nullptr || !start) return;
        realtime = std::thread([this]() {
            uint32_t version = 0;
            while (!stop.load()) {
                orpheus_bridge_bulk_service(bulk, "task");
                for (uint32_t value : coefficients)
                    if (value != coefficients[0]) torn.store(true);
                ++version;
                for (uint32_t& value : live) value = version;
            }
        });
    }
    ~Harness() {
        stop.store(true);
        if (realtime.joinable()) realtime.join();
        orpheus_bridge_bulk_destroy(bulk);
    }

    uint32_t call(uint32_t route, uint32_t operation, uint32_t transfer,
                  uint32_t offset, uint32_t count, uint32_t total,
                  const uint32_t* input, std::vector<uint32_t>* output = nullptr,
                  bool wait = true) {
        uint8_t payload[4092], response[4100];
        OrpheusBulkFragment f{operation, transfer, offset, count, total};
        std::memcpy(payload, &f, 20);
        if (input != nullptr) std::memcpy(payload + 20, input, count * 4);
        const size_t size = 20 + (input != nullptr ? count * 4 : 0);
        OrpheusMessageHeader request{
            route, ORPHEUS_MSG_MAKE(ORPHEUS_MSG_CALL, ORPHEUS_MSG_FLAG_FRAGMENT, 1, size / 4)};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        for (;;) {
            size_t length = 0;
            if (orpheus_bridge_bulk_message(bulk, &request, payload, size,
                    response, sizeof(response), &length) != ORPHEUS_OK) return UINT32_MAX;
            OrpheusMessageHeader reply;
            std::memcpy(&reply, response, 8);
            if (!(ORPHEUS_MSG_FLAGS(&reply) & ORPHEUS_MSG_FLAG_ERROR)) {
                if (output != nullptr) {
                    for (size_t i = 28; i < length; i += 4) {
                        uint32_t value;
                        std::memcpy(&value, response + i, 4);
                        output->push_back(value);
                    }
                }
                return 0;
            }
            uint32_t error;
            std::memcpy(&error, response + 28, 4);
            if (!wait || error != ORPHEUS_BULK_BUSY || std::chrono::steady_clock::now() >= deadline)
                return error;
            std::this_thread::yield();
        }
    }
};

int main() {
    {
        Harness reset(false);
        CHECK(reset.bulk != nullptr);
        std::vector<uint32_t> values(2048, 999);
        for (uint32_t offset = 0; offset < 2048; offset += 1018) {
            const uint32_t count = 2048 - offset < 1018 ? 2048 - offset : 1018;
            CHECK(reset.call(2, ORPHEUS_BULK_WRITE, 1, offset, count, 2048,
                             values.data() + offset) == 0);
        }
        CHECK(reset.call(2, ORPHEUS_BULK_COMMIT, 1, 0, 0, 2048, nullptr, nullptr, false) ==
              ORPHEUS_BULK_BUSY);
        std::thread wrong_task([&]() {
            pause_boundary = true;
            orpheus_bridge_bulk_service(reset.bulk, "other");
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (!reset.boundary_entered.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        const bool paused = reset.boundary_entered.load();
        if (paused) orpheus_bridge_bulk_reset_session(reset.bulk);
        reset.release_boundary.store(true);
        wrong_task.join();
        CHECK(paused);
        orpheus_bridge_bulk_service(reset.bulk, "task");
        for (uint32_t value : reset.coefficients) CHECK(value == 0);
        CHECK(reset.call(2, ORPHEUS_BULK_COMMIT, 1, 0, 0, 2048, nullptr) == ORPHEUS_BULK_UNKNOWN);
    }
    Harness harness;
    CHECK(harness.bulk != nullptr);
    for (uint32_t step = 1; step <= 200; ++step) {
        const uint32_t read = step * 2, write = read + 1;
        CHECK(harness.call(1, ORPHEUS_BULK_READ, read, 0, 0, 0, nullptr) == 0);
        std::vector<uint32_t> snapshot;
        std::vector<uint32_t> coefficients(2048, step);
        for (uint32_t offset = 0; offset < 2048; offset += 1018) {
            const uint32_t count = 2048 - offset < 1018 ? 2048 - offset : 1018;
            CHECK(harness.call(1, ORPHEUS_BULK_READ, read, offset, count, 2048, nullptr, &snapshot) == 0);
            CHECK(harness.call(2, ORPHEUS_BULK_WRITE, write, offset, count, 2048,
                              coefficients.data() + offset) == 0);
            std::this_thread::yield();
        }
        CHECK(snapshot.size() == 2048);
        for (uint32_t value : snapshot) CHECK(value == snapshot[0]);
        CHECK(harness.call(2, ORPHEUS_BULK_COMMIT, write, 0, 0, 2048, nullptr) == 0);
        CHECK(harness.call(2, ORPHEUS_BULK_COMMIT, write, 0, 0, 2048, nullptr) == 0);
        CHECK(harness.call(1, ORPHEUS_BULK_CANCEL, read, 0, 0, 2048, nullptr) == 0);
        CHECK(!harness.torn.load());
    }
    return 0;
}
