#ifndef BYPASS_MEMPOOL_H
#define BYPASS_MEMPOOL_H

#include <stdint.h>
#include <stdbool.h>

struct mempool {
    uint32_t *free_stack;
    uint32_t head;       // points to the next available slot for free
    uint32_t capacity;
};

// Initialize mempool with `num_frames` of size `frame_size`
struct mempool *mempool_init(uint32_t num_frames, uint32_t frame_size);

// Get an offset from the mempool
uint32_t mempool_alloc(struct mempool *mp, bool *success);

// Return an offset back to the mempool
void mempool_free(struct mempool *mp, uint32_t offset);

// Cleanup mempool
void mempool_destroy(struct mempool *mp);

#endif // BYPASS_MEMPOOL_H
