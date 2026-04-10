#ifndef SCORE_H
#define SCORE_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "xstate.h"
#include "config.h"

typedef struct xsave_buf_ { 
    uint8_t __padding[PAGE_SIZE * 3]; // The maximum size needed should be around here. XTILE can produce very large saves
} xsave_buf_t __attribute__((aligned(64)));

typedef struct state_ {
    int                     final_sig;
    uint64_t                final_rip;
    long long               perf[PAPI_MAX_EVENTS]; // Each processor can have a different number of counters available
    size_t                  perf_count;
    struct user_regs_struct gpr_state;
    xsave_buf_t             x_state;    // Make sure this gets cast properly
    size_t                  xstate_sz;
} state_t;

typedef struct states_ {
    state_t original;
    state_t serial;
} states_t;

// Normalization helper methods
#define NORMALIZE_32(bytes, count, factor, dst)     normalize_bytes((uint8_t *)bytes, count * sizeof(uint32_t), sizeof(uint32_t), factor, dst)
#define NORMALIZE_64(bytes, count, factor, dst)     normalize_bytes((uint8_t *)bytes, count * sizeof(uint64_t), sizeof(uint64_t), factor, dst)
#define NORMALIZE_128(bytes, count, factor, dst)    normalize_bytes((uint8_t *)bytes, count * sizeof(struct reg_128_bit), sizeof(struct reg_128_bit), factor, dst)
#define NORMALIZE_256(bytes, count, factor, dst)    normalize_bytes((uint8_t *)bytes, count * sizeof(struct reg_256_bit), sizeof(struct reg_256_bit), factor, dst)
#define NORMALIZE_512(bytes, count, factor, dst)    normalize_bytes((uint8_t *)bytes, count * sizeof(struct reg_512_bit), sizeof(struct reg_512_bit), factor, dst)
#define NORMALIZE_1024_BYTE(bytes, count, factor, dst)   normalize_bytes((uint8_t *)bytes, count * sizeof(struct reg_1024_byte), sizeof(struct reg_1024_byte), factor, dst)

// Debug methods
void print_bytes(uint32_t *data, size_t num_bits, bool swap, FILE *out);
void print_buffer(uint8_t *buf, size_t num_bytes, FILE *out);
void print_supported(void);
void print_prstatus(struct user_regs_struct *regs, FILE *out);
void print_xstate(struct xregs_state *regs, FILE *out);
void print_events(long long *papi_stats, int *events, size_t num_events, FILE *out);
int print_score_diff(states_t *states, bool binary, bool log);

// Score / State generation methods
uint8_t *normalize_bytes(uint8_t *bytes, size_t sz, size_t stride, size_t factor, uint8_t *dst);
int normalize_xstate(struct xregs_state *regs, uint8_t *cov, size_t buf_size);
int normalize_gpr(struct user_regs_struct *regs, uint8_t *cov, size_t buf_size);
int normalize_perf(long long *stats, size_t num_stats, uint8_t *cov, size_t buf_size);
int encode_score(config *cfg, states_t *states, uint8_t *coverage_buf, size_t buf_size, uint8_t *test_case, size_t case_len, bool save);

#endif
