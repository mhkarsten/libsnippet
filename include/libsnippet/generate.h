#ifndef GENERATE_H
#define GENERATE_H

#include <stdint.h>
#include <Zydis/Zydis.h>
#include <Zydis/MetaInfo.h>
#include <stdbool.h>

#include "Zydis/SharedTypes.h"
#include "libsnippet/snippet.h"

// Index generation stuff
#define INDEX_ITERATIONS    100     // How many generation attempts per instruction
#define INDEX_THRESHOLD     1.0     // What % of attempted generation must be correct to include an instruction

// Zydis stuff
#define MACHINE_MODE        ZYDIS_MACHINE_MODE_LONG_64
#define MEM_BASE_REGISTER   ZYDIS_REGISTER_RSI
#define MEM_INDEX_REGISTER  ZYDIS_REGISTER_RDI
#define MEM_VSIBX_REGISTER  ZYDIS_REGISTER_XMM0
#define MEM_VSIBY_REGISTER  ZYDIS_REGISTER_YMM0
#define MEM_VSIBZ_REGISTER  ZYDIS_REGISTER_ZMM0

// Snippet Execution stuff
#define MEM_PADDING         64                  // Gap in bytes to the end of the memory area, avoid OOB writes

#define MEM_ADDR            0x000000000d00000
#define MEM_SIZE            PAGE_SIZE * 2
#define CODE_ADDR           0x0000000050000000  // This sets where the code is dynamically loaded
#define CODE_SIZE           PAGE_SIZE * 2
#define STACK_ADDR          0x0000000060000000  // This sets where the stack used by the snippet is located
#define STACK_SIZE          PAGE_SIZE * 4

typedef enum generate_method_ {
    METHOD_NONE,
    METHOD_GENERATE,
    METHOD_CREATE,
    METHOD_MIXED,
} generate_method_t;

enum mutation {
    MUT_ADD_RAND,
    MUT_ADD_DET,
    MUT_REPLACE,
    MUT_REPLACE_NOP,
    MUT_REPEAT,
    MUT_SWAP,
    MUT_REMOVE,
    MUT_REPLACE_ARGS,
    MUT_RANDOMIZE_ARGS,
    MUT_SWAP_ARGS,
    MUT_ADD_JMP,
    MUT_ADD_VZEROUPPER,
    MUT_SNIPPET,
};

typedef struct instruction_defs_ {
    ZydisMnemonic mnemonic;
    ZydisOperandType operands[ZYDIS_MAX_OPERAND_COUNT_VISIBLE];
    const ZydisEncodableInstruction *matching_defs[36];
    size_t count;
    size_t operand_count;
} instruction_defs_t;

typedef struct imm_size_ {
    size_t count;
    size_t sizes[3];
    bool is_signed;
} imm_size_t;

typedef struct reg_class_ {
    size_t count;
    ZydisRegisterClass classes[3];
} reg_class_t;

typedef struct mem_ {
    uint64_t address;
    size_t size;
} mem_t;

typedef struct gen_idx_ {
    size_t iterations;
    size_t count;
    double threshold;
    ZydisMnemonic instructions[ZYDIS_MNEMONIC_MAX_VALUE+1];
} gen_idx_t;

typedef struct exec_ctx_ {
    // General config
    ZydisMachineMode mode;

    // Execution environment
    mem_t memory;
    mem_t code;
    mem_t stack;
    
    // Needed for rewriting
    ZydisRegister base_reg;
    ZydisRegister index_reg;
    ZydisRegister index_xreg;
    ZydisRegister index_yreg;
    ZydisRegister index_zreg;
} exec_ctx_t;

// Two methods of creating an instruction
int create_random_instruction(exec_ctx_t *ctx, instruction_t *ins);
int create_instruction(exec_ctx_t *ctx, instruction_t *ins, ZydisMnemonic mnemonic, ZydisOperandType *operands);

// Helper functions
int generate_operand(exec_ctx_t *ctx, const ZydisOperandDefinition *def, const ZydisEncodableInstruction *ins_def, instruction_t *ins, size_t idx);
int get_instruction_defs(instruction_defs_t *defs, bool exact);
int create_index(exec_ctx_t *ctx, gen_idx_t *idx);
int default_context(exec_ctx_t *ctx);

// Snippet level functions
int create_snippet(exec_ctx_t *ctx, snippet_t *snip, gen_idx_t *idx, size_t length, generate_method_t method);
int mutate_snippet(exec_ctx_t *ctx, snippet_t *snip, gen_idx_t *idx, enum mutation type);

// Debugging and Testing
int walk_instruction_defs(ZydisMachineMode mode, FILE *f);
int test_generate_all(exec_ctx_t *ctx, ZydisMachineMode mode, bool count, ZydisMnemonic start);


#endif

