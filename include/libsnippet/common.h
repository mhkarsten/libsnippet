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

#define HUGE_PAGE_SIZE  (1 << 21)
#define MMAP_PROT       PROT_READ | PROT_WRITE
#define MMAP_OPT        MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE
#define RNG_SEED 1234           // TODO: Should the rng be statically seeded
#define PAPI_MAX_EVENTS 50 // This should match the papi value

#define LENGTH(arr) sizeof(arr) / sizeof(arr[0])

extern char *cfg_strings[];

static inline void interrupt()
{
    asm volatile("int3\n":::"rax","rbx","rcx","rdx","rsi","rdi","r8","r9","r10","r11","r12","r13","r14","r15","rbp");
}

static inline int pidfd_open(pid_t pid, unsigned int flags)
{
   return syscall(SYS_pidfd_open, pid, flags);
}

// From the linux kernel, to help reading / understanding xsave state
static inline void cpuid_count(uint32_t id, uint32_t count, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
	asm volatile("cpuid"
		     : "=a" (*a), "=b" (*b), "=c" (*c), "=d" (*d)
		     : "0" (id), "2" (count)
	);
}

static inline int set_affinity(int core)
{
    cpu_set_t set;
    // Make sure were running on the correct core
    CPU_ZERO(&set);
    CPU_SET(core, &set);

    if (sched_setaffinity(0, sizeof(set), &set) < 0) {
        printf("Unable to set core affinity\n");
        return -1;
    }

    return 0;
}

static inline void bitmap_set(uint8_t bitmap[], size_t idx)
{
    bitmap[idx/8] |= (1U << (idx % 8));
}

static inline int bitmap_get(uint8_t bitmap[], size_t idx)
{
    return bitmap[idx/8] & (1U << (idx % 8));
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

static inline long long int min(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

static inline long long int max(int64_t a, int64_t b)
{
    return a > b ? a : b;
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

static inline int comp_times(const void *a, const void *b)
{
    return *((uint64_t *)a) - *((uint64_t *)b);
}

static inline int comp_times_fp(const void *a, const void *b)
{
    return *((double *)a) > *((double *)b) ? 1 : -1;
}

// Median absolute deviation
static inline int mad(int values[], size_t sz, int median)
{
    int diff[sz];
    
    for (size_t i = 0; i < sz; i++)
        diff[i] = abs(values[i] - median);

    qsort(diff, sz, sizeof(int), comp_times);

    return diff[sz/2];
}

#endif
