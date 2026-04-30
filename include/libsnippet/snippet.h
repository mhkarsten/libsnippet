#ifndef SNIPPET_H
#define SNIPPET_H

#include <stdbool.h>
#include <stdint.h>
#include <Zydis/Zydis.h>
#include <Zydis/Encoder.h>
#include <Zydis/Internal/SharedData.h>
#include <Zydis/Internal/EncoderData.h>
#include <Zydis/Internal/FormatterBase.h>

#include "libsnippet/list.h"
#include "libsnippet/arena.h"
#include "libsnippet/common.h"

// General and error defines
#define JUMP_INVALID    NULL
#define LENGTH_INVALID  0
#define ADDRESS_INVALID 0
#define INIT_SNIPPET_SZ 32
#define PRINT_BUF_SZ    PAGE_SIZE * 2

typedef struct instruction_ {
    ZydisEncoderRequest req;
    struct instruction_ *jump_target;   // NULL when no jmp target is known, < 0 otherwise

    // Note: These cannot always be assumed to be valid, ctx changes ect.
    uint64_t target_addrs;
    uint64_t address;
    size_t length;                      // 0 when unknown or invalid < 0 otherwise
    size_t idx;
    bool privileged;

    // These are generally needed for structured encoding
    size_t eosz; // Effective operand size (in bytes read/written), > 0 when known/required otherwise 0
    size_t easz; // Effective address size, the size of addresses used and represented, > 0 when known/required otherwise 0
    ZydisEncoderRexType rex_state; // unknown, required, or forbidden

    list_node node;
} instruction_t;

typedef struct basic_block_ {
    instruction_t *start;
    instruction_t *end;
    list_node node;
    size_t index;
    struct basic_block_ *next[2];
} basic_block_t;

typedef struct snippet_ {
    // Snippet IR state
    size_t count;
    list_node head;
    basic_block_t *blocks;
    arena_t *instructions;
    arena_t *basic_blocks;
} snippet_t;

// Basic operations
instruction_t *snippet_get(snippet_t *snip, size_t idx);
bool snippet_contains(snippet_t *snip, instruction_t *ins);
instruction_t *snippet_allocate(snippet_t *snip);
int snippet_init(snippet_t *snip);
int snippet_free(snippet_t *snip);
int snippet_destroy(snippet_t *snip);
int snippet_remove(snippet_t *snip, instruction_t *ins);
int snippet_append(snippet_t *snip, instruction_t *ins);
int snippet_insert_at(snippet_t *snip, instruction_t *ins, size_t idx);
int snippet_insert_before(snippet_t *snip, instruction_t *ins, instruction_t *position);
int snippet_swap(snippet_t *snip, size_t idx1, size_t idx2);

// Encoding / decoding bytes
int snippet_encode(snippet_t *snip, ZyanU8 *buf, ZyanUSize buf_sz);
int snippet_decode(snippet_t *snip, ZyanU64 rt_address, const ZyanU8 *buf, ZyanUSize buf_sz);

bool is_jump(ZydisMnemonic ins); 
bool is_terminator(instruction_t *ins);
bool is_conditional(instruction_t *ins);
bool is_exception(instruction_t *ins);
bool is_rip_relative(instruction_t *ins, ZydisEncoderOperand *op);
bool is_imm_jmp(instruction_t *ins);
int get_jmp_op_idx(instruction_t *ins);
ZydisInstructionCategory get_category(instruction_t *ins);

// Debug & Printing
int dump_snippet(snippet_t *snip, uint8_t *bin, size_t bin_sz, char *dir_name, bool with_text, bool is_error, int signal, uint64_t rip);
void test_list(snippet_t *snip);

#endif
