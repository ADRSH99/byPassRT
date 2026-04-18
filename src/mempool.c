#include <stdlib.h>
#include <stdio.h>
#include "mempool.h"

struct mempool *mempool_init(uint32_t num_frames, uint32_t frame_size) {
    struct mempool *mp = calloc(1, sizeof(struct mempool));
    if (!mp) return NULL;

    mp->free_stack = calloc(num_frames, sizeof(uint32_t));
    if (!mp->free_stack) {
        free(mp);
        return NULL;
    }

    mp->capacity = num_frames;
    mp->head = num_frames;

    // Fill stack with all available offsets
    for (uint32_t i = 0; i < num_frames; i++) {
        mp->free_stack[i] = i * frame_size;
    }

    return mp;
}

uint32_t mempool_alloc(struct mempool *mp, bool *success) {
    if (mp->head == 0) {
        *success = false;
        return 0;
    }
    *success = true;
    mp->head--;
    return mp->free_stack[mp->head];
}

void mempool_free(struct mempool *mp, uint32_t offset) {
    if (mp->head < mp->capacity) {
        mp->free_stack[mp->head] = offset;
        mp->head++;
    } else {
        fprintf(stderr, "Mempool capacity exceeded (double free?)\n");
    }
}

void mempool_destroy(struct mempool *mp) {
    if (mp) {
        if (mp->free_stack) free(mp->free_stack);
        free(mp);
    }
}
