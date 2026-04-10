#ifndef PIPELINE_H
#define PIPELINE_H

#include <stdbool.h>
#include <Zydis/Wrapper.h>

#include "snippet.h"

#define JMP_START(a, b) ({__typeof__(a) _a = (a); \
                        __typeof__(b) _b = (b); \
                        _a > _b ? _b + 1 : _a; })

#define JMP_END(a, b) ({__typeof__(a) _a = (a); \
                        __typeof__(b) _b = (b); \
                        _a > _b ? _a - 1 : _b; })

#define INIT_PASS_SIZE 20

typedef enum {
    PASS_TYPE_WALKER,
    PASS_TYPE_FN,
    PASS_TYPE_MAX
} pass_type;

typedef int (*pass_fn)(snippet_t *);
typedef int (*walk_fn)(snippet_t *, instruction_t *, void *);

typedef struct pass_ {
    list_node node;
    pass_type type;
    void *fn;
} pass_t;

typedef struct pipeline_ {
    list_node head;
    size_t count;
    pass_t passes[INIT_PASS_SIZE];
} pipeline_t;

// api
int pipeline_init(pipeline_t *pipe);
int pipeline_register(pipeline_t *pipe, void *fn, pass_type type);
int pipeline_execute(pipeline_t *pipe, snippet_t *snip);

// Helper passes
int generate_basic_blocks(snippet_t *snip);

// Available passes
int validate_metadata(snippet_t *snip);
int validate_jump_targets(snippet_t *snip);
int validate_jump_offsets(snippet_t *snip);
int validate_memory_operands(snippet_t *snip, instruction_t *ins, void *userdata);
int validate_rip_relative_mem(snippet_t *snip);
int validate_instructions(snippet_t *snip, instruction_t *ins, void *userdata);
int validate_jump_operands(snippet_t *snip, instruction_t *ins, void *userdata);
int validate_registers(snippet_t *snip, instruction_t *ins, void *userdata);

// Helper functions for running common pipelines
int pipeline_validate(snippet_t *snip);
int pipeline_encode(snippet_t *snip);
int pipeline_decode(snippet_t *snip);

#endif
