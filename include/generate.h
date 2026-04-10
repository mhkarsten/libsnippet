#ifndef GENERATE_H
#define GENERATE_H

#include <stdint.h>
#include <Zydis/Zydis.h>
#include <Zydis/MetaInfo.h>
#include <stdbool.h>

#include "snippet.h"
#include "config.h"
#include "arena.h"
#include "pipeline.h"

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

// Two methods of creating an instruction
int create_random_instruction(config *cfg, instruction_t *ins);
int create_instruction(config *cfg, instruction_t *ins, ZydisMnemonic mnemonic, ZydisOperandType *operands);

// Helper functions
int generate_operand(config *cfg, const ZydisOperandDefinition *def, const ZydisEncodableInstruction *ins_def, instruction_t *ins, size_t idx);
int get_instruction_defs(instruction_defs_t *defs, bool exact);
int create_index(config *cfg);

// Snippet level functions
int create_snippet(config *cfg, snippet_t *snip, size_t length, generate_method_t method);
int mutate_snippet(config *cfg, snippet_t *snip, enum mutation type);

// Debugging and Testing
int walk_instruction_defs(ZydisMachineMode mode, FILE *f);
int test_generate_all(config *cfg, ZydisMachineMode mode, bool count, ZydisMnemonic start);


#endif

