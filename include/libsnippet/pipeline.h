#ifndef PIPELINE_H
#define PIPELINE_H

#include <stdbool.h>
#include <Zydis/Wrapper.h>

#include "libsnippet/generate.h"
#include "libsnippet/snippet.h"

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

typedef int (*pass_fn)(snippet_t *, exec_ctx_t *);
typedef int (*walk_fn)(snippet_t *, exec_ctx_t *, instruction_t *, void *);
typedef int (*walk_bb_fn)(snippet_t *, exec_ctx_t *, basic_block_t *bb, void *);

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

// Pipeline interface
int pipeline_init(pipeline_t *pipe);
int pipeline_register(pipeline_t *pipe, void *fn, pass_type type);
int pipeline_execute(pipeline_t *pipe, snippet_t *snip, exec_ctx_t *ctx);

// Helper functions for running common pipelines
int pipeline_validate(snippet_t *snip, exec_ctx_t *ctx);
int pipeline_encode(snippet_t *snip, exec_ctx_t *ctx);
int pipeline_decode(snippet_t *snip, exec_ctx_t *ctx);

// Helper passes
int generate_basic_blocks(snippet_t *snip);
int snippet_print(snippet_t *snip, uint64_t rt_address, FILE *dst, bool use_zydis);
int print_instruction(snippet_t *snip, exec_ctx_t *ctx, instruction_t *ins, void *userdata);

// Walker functions
int walk_basic_blocks(snippet_t *snip, exec_ctx_t *ctx, walk_bb_fn fn, void *userdata);
int walk_instructions(snippet_t *snip, exec_ctx_t *ctx, walk_fn fn, void *userdata);

// Available passes
int validate_metadata(snippet_t *snip, exec_ctx_t *ctx);
int validate_jump_targets(snippet_t *snip, exec_ctx_t *ctx);
int validate_jump_offsets(snippet_t *snip, exec_ctx_t *ctx);
int validate_rip_relative_mem(snippet_t *snip, exec_ctx_t *ctx);
int validate_memory_operands(snippet_t *snip, exec_ctx_t *ctx, instruction_t *ins, void *userdata);
int validate_instructions(snippet_t *snip, exec_ctx_t *ctx, instruction_t *ins, void *userdata);
int validate_jump_operands(snippet_t *snip, exec_ctx_t *ctx, instruction_t *ins, void *userdata);
int validate_registers(snippet_t *snip, exec_ctx_t *ctx, instruction_t *ins, void *userdata);

#endif
