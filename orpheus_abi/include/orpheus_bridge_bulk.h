#ifndef ORPHEUS_BRIDGE_BULK_H
#define ORPHEUS_BRIDGE_BULK_H

#include "orpheus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORPHEUS_BULK_READ   1u
#define ORPHEUS_BULK_WRITE  2u
#define ORPHEUS_BULK_COMMIT 3u
#define ORPHEUS_BULK_CANCEL 4u

#define ORPHEUS_BULK_INVALID    1u
#define ORPHEUS_BULK_UNKNOWN    2u
#define ORPHEUS_BULK_BUSY       3u
#define ORPHEUS_BULK_CONFLICT   4u
#define ORPHEUS_BULK_INCOMPLETE 5u
#define ORPHEUS_BULK_LIMIT      6u
#define ORPHEUS_BULK_CLOSED     7u
#define ORPHEUS_BULK_EXPIRED    8u

#define ORPHEUS_BULK_MAX_TRANSFERS 8u
#define ORPHEUS_BULK_MAX_BYTES (1024u * 1024u)
#define ORPHEUS_BULK_TIMEOUT_MS 30000u
#define ORPHEUS_BULK_HEADER_BYTES 20u
#define ORPHEUS_BULK_MAX_ELEMENTS (ORPHEUS_MSG_MAX_PAYLOAD_WORDS - 5u)

typedef struct {
    uint32_t operation;
    uint32_t transfer_id;
    uint32_t offset;
    uint32_t count;
    uint32_t total;
} OrpheusBulkFragment;

typedef struct {
    void* active;
    void* shadow;
    const char* task;
    size_t count;
    uint32_t type;
    bool writable;
    int32_t min_i32;
    int32_t max_i32;
    void* pending;
    void (*clear_pending)(void* pending);
} OrpheusBridgeBulkTarget;

typedef int (*OrpheusBridgeBulkTargetFn)(
    void* context, uint32_t id, OrpheusBridgeBulkTarget* target);
/* 时钟在控制面和 Task 边界调用，必须单调、线程安全且实时安全。 */
typedef uint64_t (*OrpheusBridgeClockFn)(void* context);
typedef struct OrpheusBridgeBulkState OrpheusBridgeBulkState;

OrpheusBridgeBulkState* orpheus_bridge_bulk_create(
    void* context, OrpheusBridgeBulkTargetFn target, OrpheusBridgeClockFn clock);
/* 只能在 Task 全部停止后销毁；运行中的取消通过 reset_session 延迟回收。 */
void orpheus_bridge_bulk_destroy(OrpheusBridgeBulkState* state);
void orpheus_bridge_bulk_reset_session(OrpheusBridgeBulkState* state);
void orpheus_bridge_bulk_service(OrpheusBridgeBulkState* state, const char* task);
bool orpheus_bridge_bulk_write_locked(OrpheusBridgeBulkState* state, uint32_t id);
int orpheus_bridge_bulk_message(
    OrpheusBridgeBulkState* state, const OrpheusMessageHeader* request,
    const uint8_t* payload, size_t payload_len,
    uint8_t* response, size_t response_cap, size_t* response_len);

#ifdef __cplusplus
}
#endif
#endif
