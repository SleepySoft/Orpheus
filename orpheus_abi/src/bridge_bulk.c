#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "orpheus_bridge_bulk.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

enum { UNUSED, READING, SNAPSHOT, WRITING, COMMITTING, COMMITTED, CANCELLED, EXPIRED };
enum { JOB_IDLE, JOB_QUEUED, JOB_RUNNING, JOB_DONE, JOB_EXPIRED, JOB_CANCELLED };

typedef struct {
    atomic_uint job;
    atomic_uint cancel;
    uint32_t state;
    uint32_t id;
    uint32_t route;
    uint32_t total;
    uint32_t received;
    uint64_t touched;
    uint64_t queued_at;
    uint64_t sequence;
    uint8_t* data;
    uint8_t* coverage;
    size_t allocated;
    bool write;
    OrpheusBridgeBulkTarget target;
} Transfer;

struct OrpheusBridgeBulkState {
    void* context;
    OrpheusBridgeBulkTargetFn target;
    OrpheusBridgeClockFn clock;
    size_t allocated;
    uint64_t sequence;
    Transfer transfers[ORPHEUS_BULK_MAX_TRANSFERS];
};

static uint64_t now_ms(OrpheusBridgeBulkState* state) {
    if (state->clock != NULL) return state->clock(state->context);
#ifdef _WIN32
    return GetTickCount64();
#elif defined(__ADSP21000__) || (defined(__STDC_HOSTED__) && !__STDC_HOSTED__)
    return (uint64_t)clock() * 1000u / CLOCKS_PER_SEC;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) abort();
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}

static void release_data(OrpheusBridgeBulkState* state, Transfer* transfer) {
    free(transfer->data);
    free(transfer->coverage);
    transfer->data = NULL;
    transfer->coverage = NULL;
    state->allocated -= transfer->allocated;
    transfer->allocated = 0u;
}

/* 发布后的数据只归 Task 使用；取消不能释放 Task 正在拷贝的缓冲。 */
static bool retire(OrpheusBridgeBulkState* state, Transfer* transfer, uint32_t terminal) {
    unsigned job = atomic_load_explicit(&transfer->job, memory_order_acquire);
    if (job == JOB_QUEUED) {
        unsigned expected = JOB_QUEUED;
        if (!atomic_compare_exchange_strong_explicit(
                &transfer->job, &expected, JOB_IDLE,
                memory_order_acq_rel, memory_order_acquire)) job = expected;
        else job = JOB_IDLE;
    }
    if (job == JOB_RUNNING) {
        atomic_store_explicit(&transfer->cancel, 1u, memory_order_release);
        return false;
    }
    if (job == JOB_DONE && transfer->state == COMMITTING &&
        (terminal == EXPIRED || terminal == CANCELLED)) terminal = COMMITTED;
    if (job == JOB_DONE || job == JOB_EXPIRED || job == JOB_CANCELLED)
        atomic_store_explicit(&transfer->job, JOB_IDLE, memory_order_relaxed);
    release_data(state, transfer);
    transfer->state = terminal;
    return true;
}

static void reap(OrpheusBridgeBulkState* state, uint64_t now) {
    for (size_t i = 0u; i < ORPHEUS_BULK_MAX_TRANSFERS; ++i) {
        Transfer* t = &state->transfers[i];
        if (t->state == COMMITTING &&
            atomic_load_explicit(&t->job, memory_order_acquire) == JOB_DONE) {
            retire(state, t, COMMITTED);
            t->touched = now;
            t->sequence = ++state->sequence;
            continue;
        }
        if (t->state == UNUSED || now - t->touched < ORPHEUS_BULK_TIMEOUT_MS) continue;
        if (t->state == COMMITTED || t->state == CANCELLED || t->state == EXPIRED) {
            retire(state, t, UNUSED);
        } else if (retire(state, t, EXPIRED)) {
            t->touched = now;
        }
    }
}

OrpheusBridgeBulkState* orpheus_bridge_bulk_create(
    void* context, OrpheusBridgeBulkTargetFn target, OrpheusBridgeClockFn clock_fn) {
    if (target == NULL) return NULL;
    OrpheusBridgeBulkState* state = (OrpheusBridgeBulkState*)calloc(1u, sizeof(*state));
    if (state == NULL) return NULL;
    state->context = context;
    state->target = target;
    state->clock = clock_fn;
    for (size_t i = 0u; i < ORPHEUS_BULK_MAX_TRANSFERS; ++i) {
        atomic_init(&state->transfers[i].job, JOB_IDLE);
        atomic_init(&state->transfers[i].cancel, 0u);
    }
    if (!atomic_is_lock_free(&state->transfers[0].job)) {
        free(state);
        return NULL;
    }
    return state;
}

void orpheus_bridge_bulk_destroy(OrpheusBridgeBulkState* state) {
    if (state == NULL) return;
    for (size_t i = 0u; i < ORPHEUS_BULK_MAX_TRANSFERS; ++i)
        release_data(state, &state->transfers[i]);
    free(state);
}

void orpheus_bridge_bulk_reset_session(OrpheusBridgeBulkState* state) {
    if (state == NULL) return;
    for (size_t i = 0u; i < ORPHEUS_BULK_MAX_TRANSFERS; ++i) {
        Transfer* t = &state->transfers[i];
        if (!retire(state, t, UNUSED)) {
            /* 保留在途拷贝的存储，旧会话 ID 立即失效。 */
            t->id = 0u;
            t->state = EXPIRED;
            t->touched = now_ms(state) - ORPHEUS_BULK_TIMEOUT_MS;
        }
    }
}

void orpheus_bridge_bulk_service(OrpheusBridgeBulkState* state, const char* task) {
    if (state == NULL) return;
    for (size_t i = 0u; i < ORPHEUS_BULK_MAX_TRANSFERS; ++i) {
        Transfer* t = &state->transfers[i];
        unsigned expected = JOB_QUEUED;
        if (!atomic_compare_exchange_strong_explicit(
                &t->job, &expected, JOB_RUNNING,
                memory_order_acq_rel, memory_order_acquire)) continue;
        if (atomic_load_explicit(&t->cancel, memory_order_acquire) != 0u) {
            atomic_store_explicit(&t->job, JOB_CANCELLED, memory_order_release);
            continue;
        }
        if (now_ms(state) - t->queued_at >= ORPHEUS_BULK_TIMEOUT_MS) {
            atomic_store_explicit(&t->job, JOB_EXPIRED, memory_order_release);
            continue;
        }
        if (task != NULL && (t->target.task == NULL || strcmp(task, t->target.task) != 0)) {
            atomic_store_explicit(&t->job, JOB_QUEUED, memory_order_release);
            continue;
        }
        const size_t bytes = (size_t)t->total * 4u;
        if (!t->write) {
            memcpy(t->data, t->target.active, bytes);
        } else {
            memcpy(t->target.active, t->data, bytes);
            if (t->target.shadow != NULL)
                memcpy(t->target.shadow, t->target.active, t->target.count * 4u);
            if (t->target.clear_pending != NULL) t->target.clear_pending(t->target.pending);
        }
        atomic_store_explicit(&t->job, JOB_DONE, memory_order_release);
    }
}

bool orpheus_bridge_bulk_write_locked(OrpheusBridgeBulkState* state, uint32_t id) {
    if (state == NULL) return false;
    reap(state, now_ms(state));
    OrpheusBridgeBulkTarget target;
    memset(&target, 0, sizeof(target));
    const bool resolved = state->target(state->context, id, &target) == ORPHEUS_OK;
    for (size_t i = 0u; i < ORPHEUS_BULK_MAX_TRANSFERS; ++i) {
        const Transfer* t = &state->transfers[i];
        const bool same_array = t->route == id || (resolved && target.active != NULL &&
            (uintptr_t)target.active < (uintptr_t)t->target.active + t->target.count * 4u &&
            (uintptr_t)t->target.active < (uintptr_t)target.active + target.count * 4u);
        if (same_array && t->write &&
            (t->state == WRITING || t->state == COMMITTING ||
             atomic_load_explicit(&t->job, memory_order_acquire) != JOB_IDLE)) return true;
    }
    return false;
}

static uint32_t begin_transfer(
    OrpheusBridgeBulkState* state, const OrpheusBulkFragment* f,
    uint32_t route, uint64_t now, Transfer** output) {
    OrpheusBridgeBulkTarget target;
    memset(&target, 0, sizeof(target));
    if (state->target(state->context, route, &target) != ORPHEUS_OK ||
        target.active == NULL || f->total > target.count ||
        (target.type != ORPHEUS_VALUE_INT && target.type != ORPHEUS_VALUE_FLOAT) ||
        (f->operation == ORPHEUS_BULK_WRITE && !target.writable)) return ORPHEUS_BULK_INVALID;
    if (f->operation == ORPHEUS_BULK_WRITE &&
        orpheus_bridge_bulk_write_locked(state, route)) return ORPHEUS_BULK_BUSY;
    const uint64_t required = (uint64_t)f->total * 4u +
        (f->operation == ORPHEUS_BULK_WRITE ? ((uint64_t)f->total + 7u) / 8u : 0u);
    if (required > ORPHEUS_BULK_MAX_BYTES - state->allocated) return ORPHEUS_BULK_LIMIT;
    Transfer* available = NULL;
    Transfer* completed = NULL;
    for (size_t i = 0u; i < ORPHEUS_BULK_MAX_TRANSFERS; ++i) {
        Transfer* t = &state->transfers[i];
        if (t->state == UNUSED) { available = t; break; }
        if ((t->state == COMMITTED || t->state == CANCELLED || t->state == EXPIRED) &&
            atomic_load_explicit(&t->job, memory_order_acquire) == JOB_IDLE &&
            (completed == NULL || t->sequence < completed->sequence)) completed = t;
    }
    if (available == NULL) available = completed;
    if (available == NULL) return ORPHEUS_BULK_LIMIT;
    uint8_t* data = (uint8_t*)malloc((size_t)f->total * 4u);
    uint8_t* coverage = f->operation == ORPHEUS_BULK_WRITE
        ? (uint8_t*)calloc(((size_t)f->total + 7u) / 8u, 1u) : NULL;
    if (data == NULL || (f->operation == ORPHEUS_BULK_WRITE && coverage == NULL)) {
        free(data); free(coverage); return ORPHEUS_BULK_LIMIT;
    }
    available->state = f->operation == ORPHEUS_BULK_READ ? READING : WRITING;
    available->id = f->transfer_id;
    available->route = route;
    available->total = f->total;
    available->received = 0u;
    available->touched = now;
    available->sequence = ++state->sequence;
    available->queued_at = now;
    available->target = target;
    available->write = f->operation == ORPHEUS_BULK_WRITE;
    available->data = data;
    available->coverage = coverage;
    available->allocated = (size_t)required;
    atomic_store_explicit(&available->cancel, 0u, memory_order_relaxed);
    state->allocated += available->allocated;
    if (available->state == READING)
        atomic_store_explicit(&available->job, JOB_QUEUED, memory_order_release);
    *output = available;
    return 0u;
}

static uint32_t transact(
    OrpheusBridgeBulkState* state, uint32_t route, OrpheusBulkFragment* f,
    const uint8_t* data, size_t data_len, const uint8_t** output) {
    if (f->operation == ORPHEUS_BULK_READ && f->offset == 0u &&
        f->count == 0u && f->total == 0u) {
        OrpheusBridgeBulkTarget target;
        memset(&target, 0, sizeof(target));
        if (state->target(state->context, route, &target) != ORPHEUS_OK ||
            target.count > UINT32_MAX) return ORPHEUS_BULK_INVALID;
        f->total = (uint32_t)target.count;
    }
    if (f->operation == ORPHEUS_BULK_CANCEL && f->total == 0u) {
        for (size_t i = 0u; i < ORPHEUS_BULK_MAX_TRANSFERS; ++i) {
            const Transfer* t = &state->transfers[i];
            if (t->state != UNUSED && t->id == f->transfer_id) {
                if (t->route != route) return ORPHEUS_BULK_CONFLICT;
                f->total = t->total;
                break;
            }
        }
        if (f->total == 0u) return ORPHEUS_BULK_UNKNOWN;
    }
    if (f->transfer_id == 0u || f->total == 0u || f->offset > f->total ||
        f->count > f->total - f->offset || f->count > ORPHEUS_BULK_MAX_ELEMENTS ||
        f->operation < ORPHEUS_BULK_READ || f->operation > ORPHEUS_BULK_CANCEL)
        return ORPHEUS_BULK_INVALID;
    if (f->operation == ORPHEUS_BULK_WRITE) {
        if (f->count == 0u || data_len != (size_t)f->count * 4u) return ORPHEUS_BULK_INVALID;
    } else if (data_len != 0u ||
        ((f->operation == ORPHEUS_BULK_COMMIT || f->operation == ORPHEUS_BULK_CANCEL) &&
         (f->offset != 0u || f->count != 0u)) ||
        (f->operation == ORPHEUS_BULK_READ && f->count == 0u && f->offset != 0u)) {
        return ORPHEUS_BULK_INVALID;
    }
    const uint64_t now = now_ms(state);
    reap(state, now);
    Transfer* t = NULL;
    for (size_t i = 0u; i < ORPHEUS_BULK_MAX_TRANSFERS; ++i)
        if (state->transfers[i].state != UNUSED && state->transfers[i].id == f->transfer_id) {
            t = &state->transfers[i]; break;
        }
    if (t == NULL) {
        if (f->operation != ORPHEUS_BULK_READ && f->operation != ORPHEUS_BULK_WRITE)
            return ORPHEUS_BULK_UNKNOWN;
        uint32_t error = begin_transfer(state, f, route, now, &t);
        if (error != 0u) return error;
    }
    if (t->route != route || t->total != f->total) return ORPHEUS_BULK_CONFLICT;
    t->touched = now;
    t->sequence = ++state->sequence;
    if (atomic_load_explicit(&t->job, memory_order_acquire) == JOB_EXPIRED)
        retire(state, t, EXPIRED);
    if (atomic_load_explicit(&t->job, memory_order_acquire) == JOB_CANCELLED)
        retire(state, t, CANCELLED);
    if (t->state == COMMITTING &&
        atomic_load_explicit(&t->job, memory_order_acquire) == JOB_DONE)
        retire(state, t, COMMITTED);
    if (t->state == EXPIRED) return ORPHEUS_BULK_EXPIRED;
    if (t->state == COMMITTED)
        return f->operation == ORPHEUS_BULK_COMMIT ? 0u : ORPHEUS_BULK_CLOSED;
    if (t->state == CANCELLED)
        return f->operation == ORPHEUS_BULK_CANCEL ? 0u : ORPHEUS_BULK_CLOSED;
    if (f->operation == ORPHEUS_BULK_CANCEL) {
        if (!retire(state, t, CANCELLED)) return ORPHEUS_BULK_BUSY;
        return t->state == COMMITTED ? ORPHEUS_BULK_CLOSED : 0u;
    }
    if (f->operation == ORPHEUS_BULK_READ) {
        if (t->state != READING && t->state != SNAPSHOT) return ORPHEUS_BULK_CONFLICT;
        if (t->state == READING) {
            if (atomic_load_explicit(&t->job, memory_order_acquire) != JOB_DONE)
                return ORPHEUS_BULK_BUSY;
            atomic_store_explicit(&t->job, JOB_IDLE, memory_order_relaxed);
            t->state = SNAPSHOT;
        }
        if (f->count != 0u) *output = t->data + (size_t)f->offset * 4u;
        return 0u;
    }
    if (f->operation == ORPHEUS_BULK_WRITE) {
        if (t->state != WRITING) return ORPHEUS_BULK_CONFLICT;
        /* 先验证整片，冲突片不能留下部分覆盖。 */
        for (uint32_t i = 0u; i < f->count; ++i) {
            const uint32_t index = f->offset + i;
            if ((t->coverage[index / 8u] & (1u << (index % 8u))) != 0u &&
                memcmp(t->data + (size_t)index * 4u, data + (size_t)i * 4u, 4u) != 0)
                return ORPHEUS_BULK_CONFLICT;
            if (t->target.type == ORPHEUS_VALUE_INT &&
                (t->target.min_i32 != 0 || t->target.max_i32 != 0)) {
                int32_t value;
                memcpy(&value, data + (size_t)i * 4u, 4u);
                if (value < t->target.min_i32 || value > t->target.max_i32)
                    return ORPHEUS_BULK_INVALID;
            }
        }
        memcpy(t->data + (size_t)f->offset * 4u, data, data_len);
        for (uint32_t i = 0u; i < f->count; ++i) {
            const uint32_t index = f->offset + i;
            const uint8_t mask = (uint8_t)(1u << (index % 8u));
            if ((t->coverage[index / 8u] & mask) == 0u) {
                t->coverage[index / 8u] |= mask;
                ++t->received;
            }
        }
        return 0u;
    }
    if (t->state == WRITING) {
        if (t->received != t->total) return ORPHEUS_BULK_INCOMPLETE;
        t->state = COMMITTING;
        t->queued_at = now;
        atomic_store_explicit(&t->job, JOB_QUEUED, memory_order_release);
    }
    if (t->state != COMMITTING) return ORPHEUS_BULK_CONFLICT;
    if (atomic_load_explicit(&t->job, memory_order_acquire) != JOB_DONE)
        return ORPHEUS_BULK_BUSY;
    retire(state, t, COMMITTED);
    return 0u;
}

int orpheus_bridge_bulk_message(
    OrpheusBridgeBulkState* state, const OrpheusMessageHeader* request,
    const uint8_t* payload, size_t payload_len,
    uint8_t* response, size_t response_cap, size_t* response_len) {
    *response_len = 0u;
    if (response_cap < 8u + ORPHEUS_BULK_HEADER_BYTES + 4u)
        return ORPHEUS_ERR_OUT_OF_MEMORY;
    OrpheusBulkFragment fragment = {0};
    uint32_t error = ORPHEUS_BULK_INVALID;
    const uint8_t* data = NULL;
    if (state != NULL && payload_len >= ORPHEUS_BULK_HEADER_BYTES) {
        memcpy(&fragment, payload, ORPHEUS_BULK_HEADER_BYTES);
        const size_t read_size = 8u + ORPHEUS_BULK_HEADER_BYTES + (size_t)fragment.count * 4u;
        if (fragment.operation != ORPHEUS_BULK_READ || read_size <= response_cap)
            error = transact(state, request->route_id, &fragment,
                payload + ORPHEUS_BULK_HEADER_BYTES,
                payload_len - ORPHEUS_BULK_HEADER_BYTES, &data);
    }
    const size_t bytes = error != 0u ? 4u : (data != NULL ? (size_t)fragment.count * 4u : 0u);
    OrpheusMessageHeader header;
    header.route_id = request->route_id;
    header.bits = ORPHEUS_MSG_MAKE(ORPHEUS_MSG_RESPONSE,
        ORPHEUS_MSG_FLAG_FRAGMENT | (error != 0u ? ORPHEUS_MSG_FLAG_ERROR : 0u),
        ORPHEUS_MSG_CALL_ID(request), (uint32_t)(5u + bytes / 4u));
    memcpy(response, &header, 8u);
    memcpy(response + 8u, &fragment, ORPHEUS_BULK_HEADER_BYTES);
    if (error != 0u) memcpy(response + 8u + ORPHEUS_BULK_HEADER_BYTES, &error, 4u);
    else if (data != NULL) memcpy(response + 8u + ORPHEUS_BULK_HEADER_BYTES, data, bytes);
    *response_len = 8u + ORPHEUS_BULK_HEADER_BYTES + bytes;
    return ORPHEUS_OK;
}
