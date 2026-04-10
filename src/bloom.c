#include <string.h>
#include <stdint.h>

#include <xxhash.h>

#include "error.h"
#include "bloom.h"
#include "common.h"

uint64_t hash1(uint8_t *element, size_t sz)
{
    return XXH64(element, sz, SEED_1);
}

uint64_t hash2(uint8_t *element, size_t sz) 
{
    return XXH64(element, sz, SEED_2);
}

size_t bloom_index(bloom_t *bloom, uint8_t *element, size_t sz, size_t hash_idx)
{
    return (hash1(element, sz) + (hash_idx * hash2(element, sz))) % bloom->filter_bits;
}

int bloom_init(bloom_t *bloom, size_t num_elements, size_t num_hashes)
{
    if (!bloom)
        return -1;
    
    bloom->num_hashes = num_hashes;
    bloom->filter_bits = ceil(((double)num_elements)/8) * 8;
    bloom->filter = mmap(NULL, 
            ceil(((double)num_elements)/8),
            MMAP_PROT,
            (MMAP_OPT | MAP_SHARED) & ~MAP_PRIVATE, 
            -1,
            0);
    CHECK_MMAP(bloom->filter);

    return 0;
}

int bloom_destroy(bloom_t *bloom)
{
    if (!bloom)
        return -1;

    munmap(bloom->filter, bloom->filter_bits / 8);
    bloom->filter_bits = 0;
    bloom->num_hashes = 0;

    return 0;
}

int bloom_add(bloom_t *bloom, uint8_t *element, size_t sz) 
{
    if (!bloom)
        return -1;
    
    for (size_t i=0; i < bloom->num_hashes; i++)
        bitmap_set(bloom->filter, bloom_index(bloom, element, sz, i));

    return 0;
}

bool bloom_contains(bloom_t *bloom, uint8_t *element, size_t sz)
{
    if (!bloom)
        return -1;

    for (size_t i=0; i < bloom->num_hashes; i++)
        if (!bitmap_get(bloom->filter, bloom_index(bloom, element, sz, i)))
            return false;

    return true;
}

bool bloom_check_add(bloom_t *bloom, uint8_t *element, size_t sz)
{
    bool contains = bloom_contains(bloom, element, sz);

    if (!contains)
        bloom_add(bloom, element, sz);

    return contains;
}

int bloom_get_error(size_t num_elements, size_t num_hashes, size_t bloom_size)
{
    return 0;
}

