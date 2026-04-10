#ifndef ARENA_H
#define ARENA_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "list.h"

typedef struct arena_ {
    struct arena_ *next;
    size_t size;
    size_t block_size;
    
    size_t free_count;
    list_node free;
    uint8_t data[];
} arena_t;

arena_t *arena_init(size_t block_size, size_t n);
void *arena_allocate(arena_t *arena);
int arena_free(arena_t *arena, void *block);

int arena_destroy(arena_t *arena);

#endif
