#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <math.h>
#include <assert.h>
#include <execinfo.h>
#include <stdbool.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/random.h>

#define PAGE_SIZE 4096
#define RNG_SEED 1234           // TODO: Should the rng be statically seeded
#define LENGTH(arr) sizeof(arr) / sizeof(arr[0])

#define MMAP_PROT   PROT_READ | PROT_WRITE
#define MMAP_OPT    MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE

static inline long long int min(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

static inline long long int max(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

static inline void bitmap_set(uint8_t bitmap[], size_t idx)
{
    bitmap[idx/8] |= (1U << (idx % 8));
}

static inline int bitmap_get(uint8_t bitmap[], size_t idx)
{
    return bitmap[idx/8] & (1U << (idx % 8));
}

static inline uint64_t sum(uint64_t *arr, size_t sz)
{
    uint64_t sum = 0;
    for (size_t i=0; i < sz; i++)
        sum += arr[i];

    return sum;
}

static inline uint64_t sum_fp(double *arr, size_t sz)
{
    double sum = 0;
    for (size_t i=0; i < sz; i++)
        sum += arr[i];

    return sum;
}

static inline bool contains(int *arr, size_t sz, int val)
{
    for (size_t i = 0; i < sz; i++) {
        if (arr[i] == val)
            return true;
    }

    return false;
}

static inline double mean(uint64_t values[], size_t sz)
{
    return (double)sum(values, sz)/sz;
}

static inline double stdev(uint64_t values[], size_t sz) 
{
    double diff[sz];
    double val_mean = mean(values, sz);
    //printf("The mean that is found is %f\n", val_mean);
    for (size_t i = 0; i < sz; i++) {
        double conv_val = (double)values[i];
        diff[i] = pow(conv_val - val_mean, 2);
        //printf("Found diff %f(%f)\n", conv_val - val_mean, conv_val);
    }
    
    return sqrt(sum_fp(diff, sz));
}

// Median absolute deviation
static inline int mad(int values[], size_t sz, int median, int (* comparitor)(const void *, const void *))
{
    int diff[sz];
    
    for (size_t i = 0; i < sz; i++)
        diff[i] = abs(values[i] - median);

    qsort(diff, sz, sizeof(int), comparitor);

    return diff[sz/2];
}
static inline int gen_random(uint8_t *buf, size_t n)
{
    // TODO: Could maybe do this 2 bytes at a time
    for (size_t i=0; i < n; i++)
        buf[i] = random() & 0xff;

    // size_t len = 0, ret;
    // while (len < n) {
    //     ret = getrandom(buf + len, n - len, 0);
    //     if (ret < 0) return -1;
    //     len += ret;
    // }
    return 0;
}
   
// Arguments must be long long to support (up to) 64 bit signed values.
// Additionally, this returns a random value between min and max, exclusive: [min, max)
static inline uint64_t rand_between(uint64_t min, uint64_t max)
{
    // Handle edge cases here so we dont crash, helpful in some cases when min and max = 0
    if (min == max)
        return min;

    return min + (random() % (max - min));
}

static inline uint64_t rand_exclude(uint64_t min, uint64_t max, uint64_t excl_min, uint64_t excl_max)
{
    //fprintf(stderr, "Generating random value between %lu - %lu, excluding %lu - %lu\n", min, max, excl_min, excl_max);
    assert(min < max);
    assert(excl_min <= excl_max);
    assert(min <= excl_min && excl_max <= max);
    assert(excl_min > min || excl_max < max);

    uint64_t count_lo = excl_min - min;
    uint64_t count_hi = max - excl_max;
    uint64_t count_valid = count_lo + count_hi;

    if (count_valid == 0) {
        void *buffer[10];
        int n = backtrace(buffer, 10);

        char **symbols = backtrace_symbols(buffer, n);
        if (!symbols)
            exit(-20);

        if (n > 1)
            printf("Caller: %s\n", symbols[0]);

        free(symbols);
        printf("Got invalid with min %ld, max %ld, excl_min %ld, excl_max %ld\n", min, max, excl_min, excl_max);
        exit(69);
    }

    uint64_t rand = random() % count_valid;

    if (rand < count_lo)
        return min + rand;

    return (excl_max + 1) + (rand - count_lo);
}

#endif
