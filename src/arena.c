#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "arena.h"
#include "list.h"
#include "common.h"
#include "error.h"

arena_t *arena_init(size_t block_size, size_t n)
{
    list_node *node;
    arena_t *arena = mmap(NULL, (block_size * n) + sizeof(arena_t), MMAP_PROT, MMAP_OPT, -1, 0);
    CHECK_MMAP_NULL(arena);
    if (arena == MAP_FAILED)
        exit(EXIT_FAILURE);

    memset(arena, 0, sizeof(arena_t));
    
    arena->next = NULL;
    arena->size = n;
    arena->block_size = block_size;
    CHECK_INT_NULL(list_init(&arena->free), EGENERIC);
    
    for (size_t i=0; i < n; i++) {
        node = (list_node *) (arena->data + (i * block_size));
        CHECK_INT_NULL(list_insert_before(node, &arena->free), EGENERIC);
        arena->free_count++;
    }

    return arena;
}

void *arena_allocate(arena_t *arena)
{
    list_node *node;
    arena_t *sub_arena = arena;

    while (sub_arena->free_count == 0 && sub_arena->next != NULL)
        sub_arena = sub_arena->next;

    if (sub_arena->free_count == 0) {
        sub_arena->next = arena_init(sub_arena->block_size, sub_arena->size);
        CHECK_PTR_NULL(sub_arena->next, EGENERIC);
        sub_arena = sub_arena->next;
    }
    
    node = list_get(&sub_arena->free, 0);
    CHECK_PTR_NULL(node, EGENERIC);

    CHECK_INT_NULL(list_remove(node), EGENERIC);
    sub_arena->free_count--;
    memset(node, 0, arena->block_size);
    return (void *)node;
}

int arena_free(arena_t *arena, void *block)
{
    void *start, *end;
    arena_t *next_arena;

    if (!arena || !block)
        return -1;
    
    next_arena = arena;
    do {
        arena = next_arena;
        next_arena = arena->next;

        start = arena->data;
        end = start + (arena->block_size * arena->size);
    } while (!(block >= start && block < end) && next_arena);

    if (!(block >= start && block < end))
        return -1;

    CHECK_INT(list_insert_before((list_node *)block, &arena->free), EGENERIC);
    arena->free_count++;

    return 0;
}

int arena_destroy(arena_t *arena)
{
    arena_t *current, *next;
    
    next = arena;
    while (next) {
        current = next;
        next = current->next;
        CHECK_INT(munmap(current, (current->block_size * current->size) + sizeof(arena_t)), EGENERIC);
    }

    return 0;
}
