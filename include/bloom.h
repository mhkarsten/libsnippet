#ifndef BLOOM_H
#define BLOOM_H

#include <stdbool.h>
#include <string.h>
#include <stdint.h>

typedef struct bloom_ {
    uint8_t *filter;
    size_t filter_bits;
    size_t num_hashes;
} bloom_t;

// Two fun numbers here to use as seeds for the hash functions
#define SEED_1 12345678910987654321ULL
#define SEED_2 12233355555333221ULL

// These can essentially remain fixed, 
// update according to your intended finds size
// this is currently configured for 1 Mil entries
#define BLOOM_SZ    33547705    // around 4MiB
#define BLOOM_HSH   23

int bloom_init(bloom_t *bloom, size_t num_elements, size_t num_hashes);
int bloom_destroy(bloom_t *bloom);
int bloom_get_error(size_t num_elements, size_t num_hashes, size_t bloom_size);
int bloom_add(bloom_t *bloom, uint8_t *element, size_t sz);
bool bloom_contains(bloom_t *bloom, uint8_t *element, size_t sz);
bool bloom_check_add(bloom_t *bloom, uint8_t *element, size_t sz);

#endif
