#ifndef ORACLE_H
#define ORACLE_H

#include <semaphore.h>
#include <stdint.h>
#include <setjmp.h>
#include <ucontext.h>

#include <Zydis/Zydis.h>

#include "xstate.h"
#include "pipeline.h"
#include "config.h"
#include "score.h"

typedef struct oracle_ctx_ {
    // Snippet execution variables
    void (*target)(void);
    uint8_t *target_mem;
    uint8_t *target_stack;
    
    // Process management variables
    pid_t fork_pid;
    sigjmp_buf fork_env;
    struct user_regs_struct reg_state; // Save a copy of general register state here to restore from
    
    // Measurement and processing variables
    snippet_t original;     // Snippets use memory arenas, so should be allocated once.
    snippet_t serialized;
    int papi_fd;            // Used by papi to measure state
} oracle_ctx_t;

extern ZydisMnemonic nop_instructions[];

// Utility Functions
size_t load_code(uint8_t *buf, size_t buf_sz, const char *f_name);

// Main oracle interface
int init_oracle(config *cfg);
int destroy_oracle(config *cfg);
int measure_snippet(config *cfg, const uint8_t *buf, size_t buf_sz, states_t *states);
int serialize_snippet(config *cfg, snippet_t *orig, snippet_t *serial);
int gather_state(config *cfg, state_t *state);

int fork_snippet(config *cfg);
oracle_ctx_t *get_ctx(void);
void print_wait_status(int status);
int load_snippet(config *cfg, snippet_t *snip);
int reap_snippet(config *cfg);

#endif
