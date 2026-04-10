#include <Zydis/Zydis.h>
#include <Zydis/Utils.h>
#include <Zydis/Encoder.h>
#include <Zydis/SharedTypes.h>
#include <Zycore/Types.h>
#include <Zycore/API/Memory.h>
#include <Zydis/Register.h>
#include <Zydis/Wrapper.h>
#include <Zydis/Internal/SharedData.h>
#include <Zydis/Internal/EncoderData.h>
#include <Zydis/Internal/FormatterBase.h>

#include "error.h"
#include "snippet.h"
#include "config.h"
#include "generate.h"
#include "pipeline.h"
#include "debug_strings.h"


const imm_size_t imm_encoding_sizes[] = {
    [ZYDIS_OPERAND_ENCODING_UIMM8] = {1, {8}, false},
    [ZYDIS_OPERAND_ENCODING_UIMM16] = {1, {16}, false},
    [ZYDIS_OPERAND_ENCODING_UIMM32] = {1, {32}, false},
    [ZYDIS_OPERAND_ENCODING_UIMM64] = {1, {64}, false},
    [ZYDIS_OPERAND_ENCODING_UIMM16_32_64] = {3, {16, 32, 64}, false},
    [ZYDIS_OPERAND_ENCODING_UIMM32_32_64] = {2, {32, 64}, false},
    [ZYDIS_OPERAND_ENCODING_UIMM16_32_32] = {2, {16, 32}, false},
    [ZYDIS_OPERAND_ENCODING_SIMM8] = {1, {8}, true},
    [ZYDIS_OPERAND_ENCODING_SIMM16] = {1, {16}, true},
    [ZYDIS_OPERAND_ENCODING_SIMM32] = {1, {32}, true},
    [ZYDIS_OPERAND_ENCODING_SIMM64] = {1, {64}, true},
    [ZYDIS_OPERAND_ENCODING_SIMM16_32_64] = {3, {16, 32, 64}, true},
    [ZYDIS_OPERAND_ENCODING_SIMM32_32_64] = {2, {32, 64}, true},
    [ZYDIS_OPERAND_ENCODING_SIMM16_32_32] = {2, {16, 32}, true},
    [ZYDIS_OPERAND_ENCODING_JIMM8] = {1, {8}, true},
    [ZYDIS_OPERAND_ENCODING_JIMM16] = {1, {16}, true},
    [ZYDIS_OPERAND_ENCODING_JIMM32] = {1, {32}, true},
    [ZYDIS_OPERAND_ENCODING_JIMM64] = {1, {64}, true},
    [ZYDIS_OPERAND_ENCODING_JIMM16_32_64] = {3, {16, 32, 64}, true},
    [ZYDIS_OPERAND_ENCODING_JIMM32_32_64] = {2, {32, 64}, true},
    [ZYDIS_OPERAND_ENCODING_JIMM16_32_32] = {2, {16, 32}, true}
};

// UNUSED REGCLASS
// ZYDIS_REGCLASS_TABLE
// ZYDIS_REGCLASS_IP
// ZYDIS_REGCLASS_TEST
// ZYDIS_REGCLASS_FLAGS
const reg_class_t reg_classes[] = {
    [ZYDIS_SEMANTIC_OPTYPE_GPR8] = {1, {ZYDIS_REGCLASS_GPR8}},
    [ZYDIS_SEMANTIC_OPTYPE_GPR16] = {1, {ZYDIS_REGCLASS_GPR16}},
    [ZYDIS_SEMANTIC_OPTYPE_GPR32] = {1, {ZYDIS_REGCLASS_GPR32}},
    [ZYDIS_SEMANTIC_OPTYPE_GPR64] = {1, {ZYDIS_REGCLASS_GPR64}},
    [ZYDIS_SEMANTIC_OPTYPE_GPR16_32_64] = {3, {ZYDIS_REGCLASS_GPR16, ZYDIS_REGCLASS_GPR32, ZYDIS_REGCLASS_GPR64}},
    [ZYDIS_SEMANTIC_OPTYPE_GPR32_32_64] = {2, {ZYDIS_REGCLASS_GPR32, ZYDIS_REGCLASS_GPR64}},
    [ZYDIS_SEMANTIC_OPTYPE_GPR16_32_32] = {2, {ZYDIS_REGCLASS_GPR16, ZYDIS_REGCLASS_GPR32}},
    [ZYDIS_SEMANTIC_OPTYPE_FPR] = {1, {ZYDIS_REGCLASS_X87}},
    [ZYDIS_SEMANTIC_OPTYPE_MMX] = {1, {ZYDIS_REGCLASS_MMX}},
    [ZYDIS_SEMANTIC_OPTYPE_ZMM] = {1, {ZYDIS_REGCLASS_ZMM}},
    [ZYDIS_SEMANTIC_OPTYPE_XMM] = {1, {ZYDIS_REGCLASS_XMM}},
    [ZYDIS_SEMANTIC_OPTYPE_YMM] = {1, {ZYDIS_REGCLASS_YMM}},
    [ZYDIS_SEMANTIC_OPTYPE_TMM] = {1, {ZYDIS_REGCLASS_TMM}},
    [ZYDIS_SEMANTIC_OPTYPE_BND] = {1, {ZYDIS_REGCLASS_BOUND}},
    [ZYDIS_SEMANTIC_OPTYPE_SREG] = {1, {ZYDIS_REGCLASS_SEGMENT}},
    [ZYDIS_SEMANTIC_OPTYPE_CR] = {1, {ZYDIS_REGCLASS_CONTROL}},
    [ZYDIS_SEMANTIC_OPTYPE_DR] = {1, {ZYDIS_REGCLASS_DEBUG}},
    [ZYDIS_SEMANTIC_OPTYPE_MASK] = {1, {ZYDIS_REGCLASS_MASK}},
    // TODO: Handle these two separately
    [ZYDIS_SEMANTIC_OPTYPE_IMPLICIT_REG] = {1, {ZYDIS_REGCLASS_INVALID}},
    [ZYDIS_SEMANTIC_OPTYPE_GPR_ASZ] = {1, {ZYDIS_REGCLASS_INVALID}},
};

static const int mode_address_sizes[ZYDIS_MACHINE_MODE_MAX_VALUE+1][2] = {
    [ZYDIS_MACHINE_MODE_LONG_64] = {8, 8}, // Restrict instructions to 64 bit encoding while we dont support address size
    [ZYDIS_MACHINE_MODE_LONG_COMPAT_16] = {2, 4},
    [ZYDIS_MACHINE_MODE_LONG_COMPAT_32] = {2, 4},
    [ZYDIS_MACHINE_MODE_LEGACY_16] = {2, 4},
    [ZYDIS_MACHINE_MODE_LEGACY_32] = {2, 4},
    [ZYDIS_MACHINE_MODE_REAL_16] = {2, 4},
};

void print_flags(int max_value, ZyanU64 flags, const char *strings[], char *name)
{
    printf("%s: [ ", name);
    for (int i=0; i < max_value; i++) {
        if (strings[i] == NULL)
            continue;

        if (flags & i)
            printf("%s ", strings[i]);
    }

    printf("]\n");
}

void print_ins_def(const ZydisInstructionDefinition *def) {
    printf("mnemonic %s\n", ZydisMnemonicGetString(def->mnemonic));                      
    printf("operand_count %d\n", def->operand_count);                  
    printf("operand_count_visible %d\n", def->operand_count_visible);          
    printf("operand_size_map %d\n", def->operand_size_map);               
    printf("address_size_map %d\n", def->address_size_map);
    printf("requires_protected_mode %d\n", def->requires_protected_mode);      
    printf("no_compat_mode %d\n", def->no_compat_mode);      
    printf("category %s\n", zydis_category_strings[def->category]); 
    printf("op_reg %d\n", def->op_reg);                         
    printf("op_rm %d\n", def->op_rm);    
    printf("isa_set %d\n", def->isa_set);                        
    printf("isa_ext %d\n", def->isa_ext);                        
    printf("branch_type %s\n", zydis_branch_type_strings[def->branch_type]);                    
    printf("exception_class %d\n", def->exception_class);                
    printf("cpu_state %d\n", def->cpu_state);                      
    printf("fpu_state %d\n", def->fpu_state);                      
    printf("xmm_state %d\n", def->xmm_state);                      
    printf("accepts_segment %d\n", def->accepts_segment);    

    //printf("operand_reference %d\n", def->operand_reference);      
    //printf("flags_reference %d\n", def->flags_reference); 
}

void print_op_def(const ZydisOperandDefinition *def) 
{
    printf("type %s\n", zydis_semantic_optype_strings[def->type]);                            
    printf("visibility %s\n", zydis_operand_visibility_strings[def->visibility]);                     
    printf("actions %s\n", zydis_operand_action_strings[def->actions]);                         
    printf("element_type %s\n", zydis_ielement_type_strings[def->element_type]);
    printf("is_multisource4 %d\n", def->is_multisource4);               
    printf("ignore_seg_override %d\n", def->ignore_seg_override);
    printf("encoding: %s\n", zydis_operand_encoding_strings[def->op.encoding]);
    
    switch (def->type) {
    case ZYDIS_SEMANTIC_OPTYPE_IMPLICIT_REG:
        printf("implicit reg type: %s\n", zydis_implreg_type_strings[def->op.reg.type]);
        break;
    case ZYDIS_SEMANTIC_OPTYPE_IMPLICIT_MEM:
        printf("implicit reg type: %s\n", zydis_implmem_type_strings[def->op.mem.base]);
        break;
    }

    printf("sizes: [ ");
    for (int i=0; i < 3; i++) {
        printf("%d ", def->size[i]);
    }
    printf("]\n");
}

void print_enc_ins(const ZydisEncodableInstruction *def)
{
    printf("operand_mask %d\n", def->operand_mask);
    printf("opcode 0x%02x\n", def->opcode);
    printf("modrm %d\n", def->modrm);
    printf("encoding %s\n", zydis_instruction_encoding_strings[def->encoding]);                 
    printf("opcode_map %d\n", def->opcode_map);               
    print_flags(ZYDIS_WIDTH_MAX_VALUE, def->modes, zydis_width_strings, "modes");
    print_flags(ZYDIS_WIDTH_MAX_VALUE, def->address_sizes, zydis_width_strings, "address_sizes");
    printf("operand_sizes plain: 0x%02hhx\n", def->operand_sizes);
    print_flags(ZYDIS_WIDTH_MAX_VALUE, def->operand_sizes, zydis_width_strings, "operand_sizes");
    printf("mandatory_prefix %s\n", zydis_mandatory_prefix_strings[def->mandatory_prefix]);         
    printf("rex_w %d\n", def->rex_w);                    
    printf("vector_length %s\n", zydis_vector_length_strings[def->vector_length]);            
    printf("accepts_hint %s\n", zydis_size_hint_strings[def->accepts_hint]);             
    printf("swappable %d\n", def->swappable);

    //printf("instruction_reference %d\n", def->instruction_reference);
}

ZydisOperandType get_generic_operand_type(ZydisSemanticOperandType type)
{
    
    switch (type) {
    // Register Operands
    case ZYDIS_SEMANTIC_OPTYPE_IMPLICIT_REG:
    case ZYDIS_SEMANTIC_OPTYPE_GPR8:
    case ZYDIS_SEMANTIC_OPTYPE_GPR16:
    case ZYDIS_SEMANTIC_OPTYPE_GPR32:
    case ZYDIS_SEMANTIC_OPTYPE_GPR64:
    case ZYDIS_SEMANTIC_OPTYPE_GPR16_32_64:
    case ZYDIS_SEMANTIC_OPTYPE_GPR32_32_64:
    case ZYDIS_SEMANTIC_OPTYPE_GPR16_32_32:
    case ZYDIS_SEMANTIC_OPTYPE_GPR_ASZ:
    case ZYDIS_SEMANTIC_OPTYPE_FPR:
    case ZYDIS_SEMANTIC_OPTYPE_MMX:
    case ZYDIS_SEMANTIC_OPTYPE_ZMM:
    case ZYDIS_SEMANTIC_OPTYPE_XMM:
    case ZYDIS_SEMANTIC_OPTYPE_YMM:
    case ZYDIS_SEMANTIC_OPTYPE_TMM:
    case ZYDIS_SEMANTIC_OPTYPE_BND:
    case ZYDIS_SEMANTIC_OPTYPE_SREG:
    case ZYDIS_SEMANTIC_OPTYPE_CR:
    case ZYDIS_SEMANTIC_OPTYPE_DR:
    case ZYDIS_SEMANTIC_OPTYPE_MASK:
        return ZYDIS_OPERAND_TYPE_REGISTER;
    // Memory Operands
    case ZYDIS_SEMANTIC_OPTYPE_IMPLICIT_MEM:
    case ZYDIS_SEMANTIC_OPTYPE_AGEN:
    case ZYDIS_SEMANTIC_OPTYPE_MOFFS:
    case ZYDIS_SEMANTIC_OPTYPE_MIB:
    case ZYDIS_SEMANTIC_OPTYPE_MEM_VSIBX:
    case ZYDIS_SEMANTIC_OPTYPE_MEM_VSIBY:
    case ZYDIS_SEMANTIC_OPTYPE_MEM_VSIBZ:
    case ZYDIS_SEMANTIC_OPTYPE_MEM:
        return ZYDIS_OPERAND_TYPE_MEMORY;
    case ZYDIS_SEMANTIC_OPTYPE_PTR:
        return ZYDIS_OPERAND_TYPE_POINTER;
    // Immediate Operands
    case ZYDIS_SEMANTIC_OPTYPE_IMPLICIT_IMM1:
    case ZYDIS_SEMANTIC_OPTYPE_REL:
    case ZYDIS_SEMANTIC_OPTYPE_IMM:
        return ZYDIS_OPERAND_TYPE_IMMEDIATE;
    case ZYDIS_SEMANTIC_OPTYPE_UNUSED:
    default:
        return ZYDIS_OPERAND_TYPE_UNUSED;
    }

    return ZYDIS_OPERAND_TYPE_UNUSED;
}

// TODO: Expand this as we go
bool is_size_dependent(const ZydisOperandDefinition *def)
{
    switch (def->type) {
    case ZYDIS_SEMANTIC_OPTYPE_IMPLICIT_REG:
        switch (def->op.reg.type) {
        case ZYDIS_IMPLREG_TYPE_GPR_OSZ:
            return true;
        }
    }

    return false;
}

ZydisEncoderRexType rex_requirement(ZydisRegister reg, bool addressing_mode)
{
    switch (reg) {
    case ZYDIS_REGISTER_AL:
    case ZYDIS_REGISTER_CL:
    case ZYDIS_REGISTER_DL:
    case ZYDIS_REGISTER_BL:
        return ZYDIS_REX_TYPE_UNKNOWN; // Does not matter
    case ZYDIS_REGISTER_AH:
    case ZYDIS_REGISTER_CH:
    case ZYDIS_REGISTER_DH:
    case ZYDIS_REGISTER_BH:
        return ZYDIS_REX_TYPE_FORBIDDEN; // Forbidden
    case ZYDIS_REGISTER_SPL:
    case ZYDIS_REGISTER_BPL:
    case ZYDIS_REGISTER_SIL:
    case ZYDIS_REGISTER_DIL:
    case ZYDIS_REGISTER_R8B:
    case ZYDIS_REGISTER_R9B:
    case ZYDIS_REGISTER_R10B:
    case ZYDIS_REGISTER_R11B:
    case ZYDIS_REGISTER_R12B:
    case ZYDIS_REGISTER_R13B:
    case ZYDIS_REGISTER_R14B:
    case ZYDIS_REGISTER_R15B:
        return ZYDIS_REX_TYPE_REQUIRED; // Required
    default:
        if ((ZydisRegisterGetId(reg) > 7) ||
            (!addressing_mode && (ZydisRegisterGetClass(reg) == ZYDIS_REGCLASS_GPR64)))
        {
            return ZYDIS_REX_TYPE_REQUIRED;
        }
        break;
    }

    return ZYDIS_REX_TYPE_UNKNOWN;
}

ZydisRegisterClass get_gpr_class_from_size(size_t size)
{
    switch (size) {
    case 1:
        return ZYDIS_REGCLASS_GPR8;
    case 2:
        return ZYDIS_REGCLASS_GPR16;
    case 4:
        return ZYDIS_REGCLASS_GPR32;
    case 8:
        return ZYDIS_REGCLASS_GPR64;
    }

    return ZYDIS_REGCLASS_INVALID;
}

bool is_vector_operand(ZydisSemanticOperandType type)
{
    switch (type) {
    case ZYDIS_SEMANTIC_OPTYPE_MMX:
    case ZYDIS_SEMANTIC_OPTYPE_ZMM:
    case ZYDIS_SEMANTIC_OPTYPE_XMM:
    case ZYDIS_SEMANTIC_OPTYPE_YMM:
    case ZYDIS_SEMANTIC_OPTYPE_TMM:
    case ZYDIS_SEMANTIC_OPTYPE_FPR:
    case ZYDIS_SEMANTIC_OPTYPE_MEM_VSIBX:
    case ZYDIS_SEMANTIC_OPTYPE_MEM_VSIBY:
    case ZYDIS_SEMANTIC_OPTYPE_MEM_VSIBZ:
        return true;
    default:
        return false;
    }

    return false;
}

ZydisBranchWidth get_branch_width(instruction_t *ins)
{
    switch (ins->eosz) {
    case 2:
        return ZYDIS_BRANCH_WIDTH_16;
    case 4:
        return ZYDIS_BRANCH_WIDTH_32;
    case 8:
        return ZYDIS_BRANCH_WIDTH_64;
    }

    return ZYDIS_BRANCH_WIDTH_NONE;
}

bool has_matching_operands(const ZydisInstructionDefinition *def, ZydisOperandType *operands, int num_operands)
{
    const ZydisOperandDefinition *defs;
    
    if (def->operand_count_visible != num_operands)
        return false;

    defs = ZydisGetOperandDefinitions(def);
    for (int i=0; i < num_operands; i++) {
        if (get_generic_operand_type(defs[i].type) != operands[i])
            return false;
    }

    return true;
}

bool has_similar_operands(const ZydisInstructionDefinition *def, ZydisOperandType *operands, int num_operands)
{
    const ZydisOperandDefinition *defs;

    defs = ZydisGetOperandDefinitions(def);
    for (int i=0; i < num_operands; i++) {
        if (get_generic_operand_type(defs[i].type) == operands[i])
            return true;
    }

    return false;
}

int get_instruction_defs(instruction_defs_t *defs, bool exact)
{
    if (!defs || defs->mnemonic > ZYDIS_MNEMONIC_MAX_VALUE)
        return -1;

    const ZydisEncodableInstruction *variations;
    const ZydisInstructionDefinition *def;
    size_t num_variations = ZydisGetEncodableInstructions(defs->mnemonic, &variations);

    defs->count = 0;

    for (size_t i=0; i < num_variations; i++) {
        ZydisGetInstructionDefinition(variations[i].encoding, variations[i].instruction_reference, &def);
        
        if (defs->operand_count > 0 && 
                ((!exact && !has_similar_operands(def, defs->operands, defs->operand_count)) || 
                 (exact && !has_matching_operands(def,  defs->operands, defs->operand_count))))
            continue;
        
        defs->matching_defs[defs->count] = &variations[i];
        defs->count++;
    }

    return 0;
}


// ZydisRegister base;
// ZydisRegister index;
// ZyanU8 scale;
// ZyanI64 displacement;
// ZyanU16 size;
// (base) + (index * scale) + displacement
size_t generate_memory_operand(config *cfg, const ZydisOperandDefinition *def, const ZydisEncodableInstruction *enc, instruction_t *ins, size_t idx)
{
    if (!cfg || !def || !ins)
        return -1;

    ZydisEncoderOperand *op;
    const ZydisInstructionDefinition *ins_def;
    int scale_values[4] = {1, 2, 4, 8};
    
    op = &ins->req.operands[idx];
    op->type = ZYDIS_OPERAND_TYPE_MEMORY;

    // TODO: Support other address sizes
    // TODO: Index register should have some value between (cfg->snippet_mem_sz + op->mem.displacement) / op->mem.scale)
    // Get a valid index registers
    ZydisRegisterClassLookupItem item;
    CHECK_ZYAN(ZydisRegisterGetClassLookupItem(ZYDIS_REGCLASS_GPR64, &item));
    
    if (ins->rex_state == ZYDIS_REX_TYPE_FORBIDDEN)
        item.hi = ZYDIS_REGISTER_RDI;

    if (ins->eosz) {
        for (size_t i=0; i < 3; i++) {
            if ((16UL << i) == (ins->eosz * 8))
                op->mem.size = def->size[i];
        }
    }
    
    // Assume if no matching value is found, it must be fixed
    if (!op->mem.size) {
        ZydisGetInstructionDefinition(enc->encoding, enc->instruction_reference, &ins_def);
        if (ins_def->operand_size_map == 4) {
            size_t has_32 = 0;
            for (size_t i=0; i < 3; i++) {
                if (def->size[i] == 2)
                    has_32 = i;
            }
            
            if (has_32 != 0)
                op->mem.size = def->size[rand_exclude(0, 2, has_32, has_32)];
        }
            
        if (op->mem.size == 0)
            op->mem.size = def->size[rand_between(0, 3)];
    }
    
    // Maybe this is the first operand
    if (!ins->eosz)
        ins->eosz = op->mem.size;

    switch (def->type) {
    case ZYDIS_SEMANTIC_OPTYPE_IMPLICIT_MEM:
        return 0;                           // TODO: Handle this
    case ZYDIS_SEMANTIC_OPTYPE_PTR:
        op->ptr.offset = 0;
        op->ptr.segment = 0;
        break;
    case ZYDIS_SEMANTIC_OPTYPE_MOFFS:
        // Displacement
        // TODO: This is supposed to be based on address size, offset relative to segment base
        op->mem.displacement = rand_between(0, (cfg->snippet_memory.size - 64));
        op->mem.scale = 0;
        op->mem.index = ZYDIS_REGISTER_NONE;
        op->mem.base = ZYDIS_REGISTER_NONE;
        break;
    case ZYDIS_SEMANTIC_OPTYPE_MIB:
        // Base / Index / Displacement
        op->mem.base = ZYDIS_REGISTER_RSI;
        op->mem.displacement = rand_between(0, (cfg->snippet_memory.size - 64)); // Give displacement some room
        op->mem.index = rand_exclude(item.lo, item.hi, ZYDIS_REGISTER_RSP, ZYDIS_REGISTER_RSP);
        op->mem.scale = 0; // Not relevant for MIB
        break;
    case ZYDIS_SEMANTIC_OPTYPE_MEM:
        // Standard mem generation
        op->mem.base = ZYDIS_REGISTER_RSI;
        //op->mem.scale = scale_values[rand_between(0, LENGTH(scale_values))];
        op->mem.scale = 1;
        op->mem.displacement = rand_between(0, (cfg->snippet_memory.size - 64)); // Give displacement some room
        op->mem.index = rand_exclude(item.lo, item.hi, ZYDIS_REGISTER_RSP, ZYDIS_REGISTER_RSP);
        break;
    case ZYDIS_SEMANTIC_OPTYPE_AGEN:
        // AGEN memory operands must choose from valid address sizes;
        if (cfg->mode == ZYDIS_MACHINE_MODE_LONG_64)
            op->mem.size = scale_values[rand_between(3, 4)];
        else
            op->mem.size = scale_values[rand_between(1, 3)]; // Not sure how to handle this outside 64bit mode
        
        // Size has to match address size or somethign
        op->mem.base = ZYDIS_REGISTER_RSI;
        op->mem.scale = scale_values[rand_between(0, LENGTH(scale_values))];
        op->mem.displacement = rand_between(0, (cfg->snippet_memory.size - 64)); // Give displacement some room
        op->mem.index = rand_exclude(item.lo, item.hi, ZYDIS_REGISTER_RSP, ZYDIS_REGISTER_RSP); // RSP should not be used as an index
        break;
    // These are hard to make safely, index is an array of offsets (pos/neg)
    case ZYDIS_SEMANTIC_OPTYPE_MEM_VSIBX:
    case ZYDIS_SEMANTIC_OPTYPE_MEM_VSIBY:
    case ZYDIS_SEMANTIC_OPTYPE_MEM_VSIBZ:
    default:
        return -1;
    }
    
    // Set rex requirement based on index register
    if (op->mem.index != ZYDIS_REGISTER_NONE)
        ins->rex_state = rex_requirement(op->mem.index, true); 
    
    return 0;
}

// ZydisRegister value;
// ZyanBool is4;
int generate_register_operand(config *cfg, const ZydisOperandDefinition *def, const ZydisEncodableInstruction *enc, instruction_t *ins, size_t idx)
{
    if (!cfg || !def || !ins)
        return -1;
    
    ZydisEncoderOperand *op;
    ZydisRegisterClassLookupItem item;
    const ZydisInstructionDefinition *ins_def;
    ZydisRegisterClass class = ZYDIS_REGCLASS_INVALID;
    const reg_class_t *classes;
    size_t width;
    int reg_id = 0;
    
    op = &ins->req.operands[idx];
    op->type = ZYDIS_OPERAND_TYPE_REGISTER;
    
    if (def->type == ZYDIS_SEMANTIC_OPTYPE_IMPLICIT_REG) {
        switch (def->op.reg.type) {
        // TODO: This is duplicated code
        case ZYDIS_IMPLREG_TYPE_STATIC:
            op->reg.value = def->op.reg.reg.reg;
            if (!ins->eosz)
                ins->eosz = ZydisRegisterGetWidth(cfg->mode, op->reg.value) / 8;

            if (def->op.encoding == ZYDIS_OPERAND_ENCODING_IS4)
                op->reg.is4 = ZYAN_TRUE;

            //printf("Selected register %s\n", ZydisRegisterGetString(op->reg.value));
            break;
        case ZYDIS_IMPLREG_TYPE_GPR_OSZ: // Always dependent on other operand?
            if (!ins->eosz)
                return -1;

            class = get_gpr_class_from_size(ins->eosz);
            if (class == ZYDIS_REGCLASS_INVALID)
                return -1;
            
            op->reg.value = ZydisRegisterEncode(class, def->op.reg.reg.id);

            if (def->op.encoding == ZYDIS_OPERAND_ENCODING_IS4)
                op->reg.is4 = ZYAN_TRUE;

            //printf("Selected register %s\n", ZydisRegisterGetString(op->reg.value));
            break;
        case ZYDIS_IMPLREG_TYPE_GPR_ASZ: // Matches address size, register might already be set?
            if (!ins->eosz)
                ins->eosz = mode_address_sizes[cfg->mode][rand_between(0, 1)];
            
            class = get_gpr_class_from_size(ins->eosz);
            if (class == ZYDIS_REGCLASS_INVALID)
                return -1;

            op->reg.value = ZydisRegisterEncode(class, def->op.reg.reg.id);
            
            if (def->op.encoding == ZYDIS_OPERAND_ENCODING_IS4)
                op->reg.is4 = ZYAN_TRUE;

            //printf("Selected register %s\n", ZydisRegisterGetString(op->reg.value));
            break;
        case ZYDIS_IMPLREG_TYPE_IP_ASZ:  // Must match address, and be rip relative register
        case ZYDIS_IMPLREG_TYPE_IP_SSZ: // Must match stack size, and be rip relative register
        case ZYDIS_IMPLREG_TYPE_GPR_SSZ: // Must match stack size, and be gpr register
        case ZYDIS_IMPLREG_TYPE_FLAGS_SSZ: // Must match stack size, and be flag register
        default:
            return -1;
        }

        return 0;
    } else if (def->type == ZYDIS_SEMANTIC_OPTYPE_GPR_ASZ) {
        // Set the width of this register based on address size
        // TODO: This should use the new address size variable
        if (!ins->eosz)
            ins->eosz = mode_address_sizes[cfg->mode][rand_between(0, 1)];
        
        class = get_gpr_class_from_size(ins->eosz);
        if (class == ZYDIS_REGCLASS_INVALID)
            return -1;

        op->reg.value = ZydisRegisterEncode(class, def->op.reg.reg.id);
        
        if (def->op.encoding == ZYDIS_OPERAND_ENCODING_IS4)
            op->reg.is4 = ZYAN_TRUE;

        //printf("Selected register %s\n", ZydisRegisterGetString(op->reg.value));
        return 0;
    }

    classes = &reg_classes[def->type];
    if (classes->count == 0)
        return -1;
    
    if (!ins->eosz) {
        ZydisGetInstructionDefinition(enc->encoding, enc->instruction_reference, &ins_def);
        if (ins_def->operand_size_map == 4) {
            size_t has_32 = 0;
            for (size_t i=0; i < classes->count; i++) {
                if (ZydisRegisterClassGetWidth(cfg->mode, classes->classes[i]) == 32)
                    has_32 = i;
            }
            
            if (has_32 != 0)
                class = classes->classes[rand_exclude(0, classes->count-1, has_32, has_32)];
        }
            
        if (class == ZYDIS_REGCLASS_INVALID)
            class = classes->classes[rand_between(0, classes->count)];
    
        // Check if there is an element size that overrides the primary operand size (should not be the case for registers??)
        // TODO: Maybe make this check more exact
        // if (def->element_type != ZYDIS_IELEMENT_TYPE_INVALID && def->size[0])
        //     ins->eosz = def->size[rand_between(0, 2)];
        // else
        ins->eosz = ZydisRegisterClassGetWidth(cfg->mode, class) / 8;
        
    // We can avoid selecting width if the definition simply specifies size
    } else if (classes->count == 1) {
        class = classes->classes[0];
    } else {
        for (size_t i=0; i < classes->count; i++) {
            width = ZydisRegisterClassGetWidth(cfg->mode, classes->classes[i]) / 8;
            if (width == ins->eosz)
                class = classes->classes[i];
        }
    }
    
    // Default to selecting a random (known valid) size
    if (class == ZYDIS_REGCLASS_INVALID) {
        if (ZydisRegisterClassGetWidth(cfg->mode, classes->classes[0]) == 16
            && ins->eosz > 2)
            class = classes->classes[rand_between(1, classes->count)];
        else
            class = classes->classes[rand_between(0, classes->count)];
    }
    
    CHECK_ZYAN(ZydisRegisterGetClassLookupItem(class, &item));
    
    // This is for vector registers, which are only 0 - 15 in legacy mode
    if (enc->encoding == ZYDIS_INSTRUCTION_ENCODING_LEGACY && is_vector_operand(def->type)) {
        reg_id = rand_between(1, (item.hi - item.lo) < 16 ? (item.hi - item.lo): 16);

    // This is to handle the rex prefix, for small registers
    } else if (def->type == ZYDIS_SEMANTIC_OPTYPE_GPR8) {
        if (ins->rex_state == ZYDIS_REX_TYPE_REQUIRED)
            reg_id = rand_exclude(1, (item.hi - item.lo), (ZYDIS_REGISTER_AH - item.lo), (ZYDIS_REGISTER_BH - item.lo));
        else if (ins->rex_state == ZYDIS_REX_TYPE_FORBIDDEN)
            reg_id = rand_between(1, (ZYDIS_REGISTER_BH - item.lo));
        else 
            reg_id = rand_between(1, (item.hi - item.lo));

    } else if (def->type == ZYDIS_SEMANTIC_OPTYPE_DR) {
        reg_id = rand_between(1, 8);

    } else if (def->type == ZYDIS_SEMANTIC_OPTYPE_CR) {
        static uint8_t cr_lookup[5] = {0, 2, 3, 4, 8};
        if (cfg->mode != ZYDIS_MACHINE_MODE_LONG_64)
            reg_id = cr_lookup[rand_between(0, 4)];
        else
            reg_id = cr_lookup[rand_between(0, 5)];

    } else {
        reg_id = rand_between(1, (item.hi - item.lo));
    }
    
    //printf("Value between 1, %d choice %d\n", item.hi - item.lo, reg_id);
    op->reg.value = ZydisRegisterEncode(class, reg_id);

    if (op->reg.value == ZYDIS_REGISTER_NONE)
        return -1;

    if (ins->rex_state == ZYDIS_REX_TYPE_UNKNOWN)
        ins->rex_state = rex_requirement(op->reg.value, false);
    
    if (def->op.encoding == ZYDIS_OPERAND_ENCODING_IS4)
        op->reg.is4 = ZYAN_TRUE;
    else
        op->reg.is4 = ZYAN_FALSE;
    
    //printf("Selected register %s\n", ZydisRegisterGetString(op->reg.value));
    return 0;
}

// ZyanU64 u;
// ZyanI64 s;
int generate_immediate_operand(config *cfg, const ZydisOperandDefinition *def, instruction_t *ins, size_t idx)
{
    if (!cfg || !def || !ins)
        return -1;
    
    ZydisEncoderOperand *op;
    imm_size_t sizes;
    bool is_signed = false; // By default unsigned
    size_t width = 0;
    int max_sz = 0;
    
    // Width is only known if the encoding is set, the encoding is only not set of IMM1 (so far)
    if (def->op.encoding != ZYDIS_OPERAND_ENCODING_NONE) {
        sizes = imm_encoding_sizes[def->op.encoding];

        if (ins->eosz) {
            for (int i=(sizes.count-1); i >= 0; i--) {
                if (sizes.sizes[i] / 8 > ins->eosz)
                    max_sz = i;
            }
        }
        
        is_signed = sizes.is_signed;
        width = sizes.sizes[rand_between(0, max_sz)] / 8;
    } else if (def->type == ZYDIS_SEMANTIC_OPTYPE_IMPLICIT_IMM1) {
        width = 1;
    }

    if (width == 0)
        return -1;

    // Immediate might be used to determine operand size (IMPLREG_GPR_OSZ)
    if (!ins->eosz)
        ins->eosz = width;
    
    op = &ins->req.operands[idx];
    op->type = ZYDIS_OPERAND_TYPE_IMMEDIATE;

    switch (def->type){
    case ZYDIS_SEMANTIC_OPTYPE_IMPLICIT_IMM1:
        op->imm.u = 1;
        width = 1;
        break;
    case ZYDIS_SEMANTIC_OPTYPE_REL:
        // Relative operands enforce size constraints
        ins->eosz = width;
        ins->req.branch_width = get_branch_width(ins);
        // This is supposed to fallthrough
    case ZYDIS_SEMANTIC_OPTYPE_IMM:
        width = width * 8; // Set this back to bits
        if (is_signed) {
            op->imm.s = rand_between(0, (1LL << (width-1)) - 1) | (1LL << (width-2));
            op->imm.s = rand() % 2 == 0 ? -op->imm.s : op->imm.s; // Randomly make negative
        } else {
            op->imm.u = rand_between(0, (1LL << (width)) - 1) | (1LL << (width-1));
        }
        break;
    default:
        return -1;
    }
    
    return 0;
}

// typedef struct ZydisEncoderOperand_
// {
//     ZydisOperandType type;
//     struct ZydisEncoderOperandReg_
//     {
//         ZydisRegister value;
//         ZyanBool is4;
//     } reg;

//     struct ZydisEncoderOperandMem_
//     {
//         ZydisRegister base;
//         ZydisRegister index;
//         ZyanU8 scale;
//         ZyanI64 displacement;
//         ZyanU16 size;
//     } mem;
//     struct ZydisEncoderOperandPtr_
//     {
//         ZyanU16 segment;
//         ZyanU32 offset;
//     } ptr;
//     union ZydisEncoderOperandImm_
//     {
//         ZyanU64 u;
//         ZyanI64 s;
//     } imm;
// } ZydisEncoderOperand;
int generate_operand(config *cfg, const ZydisOperandDefinition *def, const ZydisEncodableInstruction *enc, instruction_t *ins, size_t idx)
{
    //print_op_def(def);
    
    switch (def->type) {
    // Register Operands
    case ZYDIS_SEMANTIC_OPTYPE_IMPLICIT_REG:
    case ZYDIS_SEMANTIC_OPTYPE_GPR8:
    case ZYDIS_SEMANTIC_OPTYPE_GPR16:
    case ZYDIS_SEMANTIC_OPTYPE_GPR32:
    case ZYDIS_SEMANTIC_OPTYPE_GPR64:
    case ZYDIS_SEMANTIC_OPTYPE_GPR16_32_64:
    case ZYDIS_SEMANTIC_OPTYPE_GPR32_32_64:
    case ZYDIS_SEMANTIC_OPTYPE_GPR16_32_32:
    case ZYDIS_SEMANTIC_OPTYPE_GPR_ASZ:
    case ZYDIS_SEMANTIC_OPTYPE_FPR:
    case ZYDIS_SEMANTIC_OPTYPE_MMX:
    case ZYDIS_SEMANTIC_OPTYPE_ZMM:
    case ZYDIS_SEMANTIC_OPTYPE_XMM:
    case ZYDIS_SEMANTIC_OPTYPE_YMM:
    case ZYDIS_SEMANTIC_OPTYPE_TMM:
    case ZYDIS_SEMANTIC_OPTYPE_BND:
    case ZYDIS_SEMANTIC_OPTYPE_SREG:
    case ZYDIS_SEMANTIC_OPTYPE_CR:
    case ZYDIS_SEMANTIC_OPTYPE_DR:
    case ZYDIS_SEMANTIC_OPTYPE_MASK:
        CHECK_INT(generate_register_operand(cfg, def, enc, ins, idx), ESLIENT);
        break;
    // Memory Operands
    case ZYDIS_SEMANTIC_OPTYPE_IMPLICIT_MEM:
    case ZYDIS_SEMANTIC_OPTYPE_PTR:
    case ZYDIS_SEMANTIC_OPTYPE_AGEN:
    case ZYDIS_SEMANTIC_OPTYPE_MOFFS:
    case ZYDIS_SEMANTIC_OPTYPE_MIB:
    case ZYDIS_SEMANTIC_OPTYPE_MEM_VSIBX:
    case ZYDIS_SEMANTIC_OPTYPE_MEM_VSIBY:
    case ZYDIS_SEMANTIC_OPTYPE_MEM_VSIBZ:
    case ZYDIS_SEMANTIC_OPTYPE_MEM:
        CHECK_INT(generate_memory_operand(cfg, def, enc, ins, idx), ESLIENT);
        break;
    // Immediate Operands
    case ZYDIS_SEMANTIC_OPTYPE_IMPLICIT_IMM1:
    case ZYDIS_SEMANTIC_OPTYPE_REL:
    case ZYDIS_SEMANTIC_OPTYPE_IMM:
        CHECK_INT(generate_immediate_operand(cfg, def, ins, idx), ESLIENT);
        break;
    case ZYDIS_SEMANTIC_OPTYPE_UNUSED:
    default:
        printf("The operand type is %s\n", zydis_semantic_optype_strings[def->type]);
        return -6;
    }

    return 0;
}

// Variable                                                 | Has been handled
// ZydisMnemonic mnemonic;                                  |       x
// ZyanU8 operand_count;                                    |       x
// ZydisMachineMode machine_mode;                           |       x
// ZydisBranchType branch_type;                             |       x
// ZydisBranchWidth branch_width;                           |       x
// ZydisInstructionAttributes prefixes;                     |
// ZydisEncodableEncoding allowed_encodings;                |       x
// ZydisAddressSizeHint address_size_hint;                  |       x
// ZydisOperandSizeHint operand_size_hint;                  |       x
// ZydisEncoderOperand operands[ZYDIS_ENCODER_MAX_OPERANDS];|       x
static int generate_instruction(config *cfg, instruction_t *ins, const ZydisEncodableInstruction *enc)
{
    ZyanStatus status;
    ZyanU64 size = ZYDIS_MAX_INSTRUCTION_LENGTH;
    uint8_t buf[ZYDIS_MAX_INSTRUCTION_LENGTH];
    const ZydisOperandDefinition *op_def;
    const ZydisInstructionDefinition *def;
    
    // Wipe out any existing definition
    memset(&ins->req, 0, sizeof(ZydisEncoderRequest));

    ZydisGetInstructionDefinition(enc->encoding, enc->instruction_reference, &def);
    if (!def)
        return -1;

    // Fill in what is known about the zydis request
    ins->req.machine_mode = ZYDIS_MACHINE_MODE_LONG_64;
    ins->req.mnemonic = def->mnemonic;
    ins->req.operand_count = def->operand_count_visible;
    ins->req.allowed_encodings = _ZydisGetEncodableEncoding(enc->encoding);
    ins->req.branch_type = def->branch_type;

    // TODO: Handle prefixes
    ins->req.prefixes = 0;

    //printf("++++ INSTRUCTION DEF ++++\n");
    //print_ins_def(def);
    //print_enc_ins(enc);
    
    // Set the eosz for branches early because it depends on mode
    switch (ins->req.branch_type) {
    case ZYDIS_BRANCH_TYPE_NONE:
        ins->req.branch_width = ZYDIS_BRANCH_WIDTH_NONE;
        break;
    case ZYDIS_BRANCH_TYPE_SHORT:
        ins->req.branch_width = ZYDIS_BRANCH_WIDTH_8;
        break;
    case ZYDIS_BRANCH_TYPE_NEAR:
    case ZYDIS_BRANCH_TYPE_FAR:
        if (cfg->mode == ZYDIS_MACHINE_MODE_LONG_64)
            ins->eosz = 8; // 64 bit mode only supports r/m64 operands
        else
            ins->eosz = mode_address_sizes[cfg->mode][rand_between(0, 1)];
        
        //printf("I have chosen a size of %ld\n", ins->eosz);
        ins->req.branch_width = get_branch_width(ins); 
        break;
    default:
        return -1;
    }

    op_def = ZydisGetOperandDefinitions(def);
    for (int i=0; i < def->operand_count_visible; i++) {
        // TODO: Maybe rework this if there are instructions with more than one dependent operand
        if (is_size_dependent(&op_def[i]) && i == 0) {
            if (i+1 >= def->operand_count_visible)
                return -2;


            //printf("----- OPERAND %d ----\n", i+1);
            CHECK_INT(generate_operand(cfg, &op_def[i+1], enc, ins, i+1), ESLIENT);

            //printf("----- OPERAND %d ----\n", i);
            CHECK_INT(generate_operand(cfg, &op_def[i], enc, ins, i), ESLIENT);

            i+=1;
            continue;
        }

        //printf("----- OPERAND %d ----\n", i);
        CHECK_INT(generate_operand(cfg, &op_def[i], enc, ins, i), ESLIENT);
    }
    
    // TODO: handle extra encoding types here
    const ZydisInstructionDefinitionLEGACY* legacy;
    switch (enc->encoding) {
    case ZYDIS_INSTRUCTION_ENCODING_LEGACY:
         legacy = (const ZydisInstructionDefinitionLEGACY*)def;
         ins->privileged = legacy->is_privileged;
         break;
    case ZYDIS_INSTRUCTION_ENCODING_MVEX:
        break;
    case ZYDIS_INSTRUCTION_ENCODING_EVEX:
        break;
    }
    
    status = ZydisEncoderEncodeInstruction(&ins->req, buf, &size);
    CHECK_ZYAN_WERR(status, ESLIENT);
    
    // Fill in the last bits of metadata that are known
    ins->idx = 0;
    ins->address = cfg->snippet_code.address;
    ins->length = size;
    ins->jump_target = JUMP_INVALID;
    
    if (ins->req.branch_type != ZYDIS_BRANCH_TYPE_NONE 
        && ins->req.operands[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
        ins->target_addrs = ins->address + ins->req.operands[0].imm.s;

    return 0;
}

// Generates a completely random instruction
int create_random_instruction(config *cfg, instruction_t *ins)
{
    ZyanStatus status_dec, status_enc;
    uint8_t buf[16];
    int attempts = 0;
    ZydisDisassembledInstruction ins_buf;

    do {
        // TODO: Restrict which instructions can be used (no privaledged ect.)
        if (gen_random(buf, 16) < 0)
            return -1;
        
        //printf("Random bytes 0x%llx 0x%llx\n", ((uint64_t *)buf)[0], ((uint64_t *)buf)[1]);
        
        status_dec = ZydisDisassembleIntel(cfg->mode, CFG_SNIPPET_CODE_ADDR, buf, 16, &ins_buf);

        status_enc = ZydisEncoderDecodedInstructionToEncoderRequest(
                    &ins_buf.info,
                    ins_buf.operands,
                    ins_buf.info.operand_count,
                    &ins->req);
        attempts += 1;
    } while (ZYAN_FAILED(status_enc) || ZYAN_FAILED(status_dec));

    //printf("[%d] %d %s\n", attempts, ins_buf.info.length, ins_buf.text);

    ins->idx = 0;
    ins->address = ins_buf.runtime_address;
    ins->target_addrs = ADDRESS_INVALID;
    ins->jump_target = JUMP_INVALID;
    ins->rex_state = ZYDIS_REX_TYPE_UNKNOWN; // TODO: this is probably known
    ins->length = ins_buf.info.length;
    ins->privileged = ins_buf.info.attributes & ZYDIS_ATTRIB_IS_PRIVILEGED;

    if (ins->req.branch_type == ZYDIS_BRANCH_TYPE_NONE)
        return attempts;
    
    switch (ins->req.operands[0].type) {
    case ZYDIS_OPERAND_TYPE_IMMEDIATE:
        ins->target_addrs = ins_buf.runtime_address + ins->req.operands[0].imm.s;
        break;
    case ZYDIS_OPERAND_TYPE_MEMORY:
    case ZYDIS_OPERAND_TYPE_POINTER:
    case ZYDIS_OPERAND_TYPE_REGISTER:
    default:
        break;
    }

    return attempts;
}

// Creates a pseudo random instruction, according to some requirements
int create_instruction(config *cfg, instruction_t *ins, ZydisMnemonic mnemonic, ZydisOperandType *operands)
{
    instruction_defs_t defs = {0};
    defs.mnemonic = mnemonic;
    
    if (operands) {
        // Operands are expected to be contigous for exact matching
        for (ZydisOperandType *op=operands; op != ZYDIS_OPERAND_TYPE_UNUSED; op++) {
            defs.operands[defs.operand_count] = *op;
            defs.operand_count++;
        }

        CHECK_INT(get_instruction_defs(&defs, true), EGENERIC);
    } else {
        defs.operands[0] = ZYDIS_OPERAND_TYPE_UNUSED;
        CHECK_INT(get_instruction_defs(&defs, false), EGENERIC);
    }

    if (defs.count == 0) {
        printf("There were no definitions found for %s (%d)\n", ZydisMnemonicGetString(mnemonic), mnemonic);
        return -100; // TODO: More informative error here
    }

    return generate_instruction(cfg, ins, defs.matching_defs[rand_between(0, defs.count)]);
}

int create_snippet(config *cfg, snippet_t *snip, size_t length, generate_method_t method)
{
    instruction_t *ins;
    ZydisMnemonic mnemonic;
    int ret;
    
    // Gotta be sure that these are initialized
    snip->count = 0;
    snip->start_address = cfg->snippet_code.address;
    snip->mem_start = cfg->snippet_memory.address;
    snip->mem_sz = cfg->snippet_memory.size;
    snip->index_reg = cfg->mem_index_register;
    snip->base_reg = cfg->mem_base_register;

    for (size_t i=0; i < length; i++) {
        ins = snippet_allocate(snip);
        CHECK_PTR(ins, EGENERIC);

        // These can be set beforehand
        ins->privileged = false;
        
        switch (method) {
        case METHOD_GENERATE:
            CHECK_INT(create_random_instruction(cfg, ins), EGENERIC);
            break;
        case METHOD_CREATE:
            mnemonic = cfg->index.instructions[rand_between(0, cfg->index.count)];
            ret = create_instruction(cfg, ins, mnemonic, NULL);
            //CHECK_INT(ret, EGENERIC);
            if (ret < 0)
                CHECK_INT(create_random_instruction(cfg, ins), EGENERIC);
            break;
        case METHOD_MIXED:
            ret = 0;
            if (i % 2 == 0) {
                mnemonic = cfg->index.instructions[rand_between(0, cfg->index.count)];
                ret = create_instruction(cfg, ins, mnemonic, NULL);
                //CHECK_INT(ret, EGENERIC);
            } 

            if (i % 2 != 0 || ret < 0) {
                CHECK_INT(create_random_instruction(cfg, ins), EGENERIC);
            }
            break;
        default:
            return -1;
        }
        
        CHECK_INT(snippet_append(snip, ins), EGENERIC);
    }
    
    return pipeline_validate(snip);
}

// Pretty much all of these invalidate length/address metadata
int mutate_snippet(config *cfg, snippet_t *snip, enum mutation type)
{
    instruction_t *tmp;
    list_node pos;
    uint64_t idx1, idx2;
    int ret;
    ZydisOperandType ops[ZYDIS_MAX_OPERAND_COUNT_VISIBLE+1];
    ZydisEncoderOperand op;
    
    // (almost) all cases require this, do it beforehand
    idx1 = rand_between(0, snip->count);

    switch (type) {
    case MUT_ADD_RAND:
        idx1 = rand_between(0, snip->count+1);
        tmp = snippet_allocate(snip);
        
        CHECK_PTR(tmp, EGENERIC);
        CHECK_INT(create_random_instruction(cfg, tmp), EGENERIC);
        if (idx1 >= snip->count)
            CHECK_INT(snippet_append(snip, tmp), EGENERIC);
        else
            CHECK_INT(snippet_insert_at(snip, tmp, idx1), EGENERIC);
        break;
    case MUT_ADD_DET:
        idx1 = rand_between(0, snip->count+1);
        idx2 = rand_between(0, cfg->index.count);
        tmp = snippet_allocate(snip);

        CHECK_PTR(tmp, EGENERIC);
        ret = create_instruction(cfg, tmp, cfg->index.instructions[idx2], NULL);
        if (ret < 0) {
            CHECK_INT(arena_free(snip->instructions, tmp), EGENERIC);
            //CHECK_INT(ret, EGENERIC);
            return mutate_snippet(cfg, snip, MUT_ADD_RAND); // Fallback incase generation fails
        }

        if (idx1 >= snip->count)
            CHECK_INT(snippet_append(snip, tmp), EGENERIC);
        else
            CHECK_INT(snippet_insert_at(snip, tmp, idx1), EGENERIC);
        break;
    case MUT_REMOVE:
        CHECK_INT(snippet_remove(snip, snippet_get(snip, idx1)), EGENERIC);
        break;
    case MUT_REPLACE:
        tmp = snippet_get(snip, idx1);
        pos = tmp->node;
        
        CHECK_PTR(tmp, EGENERIC);
        CHECK_INT(create_random_instruction(cfg, tmp), EGENERIC);
        tmp->node = pos;
        break;
    case MUT_REPLACE_NOP:
        tmp = snippet_get(snip, idx1);
        CHECK_PTR(tmp, EGENERIC);
        pos = tmp->node;
        memset(tmp, 0, sizeof(instruction_t));

        CHECK_INT(create_instruction(cfg, tmp, ZYDIS_MNEMONIC_NOP, NULL), EGENERIC);
        tmp->node = pos;
        break;
    case MUT_REPEAT: // TODO: Should this be repeat n times?
        tmp = snippet_allocate(snip);
        CHECK_PTR(tmp, EGENERIC);

        *tmp = *snippet_get(snip, idx1);
        CHECK_INT(snippet_insert_at(snip, tmp, idx1), EGENERIC);
        break;
    case MUT_SWAP:
        idx2 = rand_exclude(0, snip->count-1, idx1, idx1); 
        CHECK_INT(snippet_swap(snip, idx1, idx2), EGENERIC);
        break;
    case MUT_RANDOMIZE_ARGS:
        tmp = snippet_get(snip, idx1);
        CHECK_PTR(tmp, EGENERIC);

        if (tmp->req.operand_count == 0)
            return 0;

        for (int i=0; i < tmp->req.operand_count; i++)
            ops[i] = tmp->req.operands[i].type;
        ops[tmp->req.operand_count] = ZYDIS_OPERAND_TYPE_UNUSED;

        pos = tmp->node;
        // Create a new instruction with the same type and operand types
        CHECK_INT(create_instruction(cfg, tmp, tmp->req.mnemonic, ops), EGENERIC);
        tmp->node = pos;
        break;
    case MUT_REPLACE_ARGS:
        tmp = snippet_get(snip, idx1);
        CHECK_PTR(tmp, EGENERIC);

        if (tmp->req.operand_count == 0)
            return 0;
        
        pos = tmp->node;
        // Create a new instruction of the same type with (possibly) different operands
        CHECK_INT(create_instruction(cfg, tmp, tmp->req.mnemonic, NULL), EGENERIC);
        tmp->node = pos;
        break;
    case MUT_SWAP_ARGS: // TODO: Maybe remove this one, as it has a fairly large chance of not working
        tmp = snippet_get(snip, idx1);
        CHECK_PTR(tmp, EGENERIC);
        
        if (tmp->req.operand_count == 0)
            return 0;
        
        // Select the two operands to be swapped
        idx1 = rand_between(0, tmp->req.operand_count);
        idx2 = rand_between(0, tmp->req.operand_count);
        
        op = tmp->req.operands[idx1];
        
        tmp->req.operands[idx1] = tmp->req.operands[idx2];
        tmp->req.operands[idx2] = op;
        break;
    case MUT_ADD_JMP:
        idx1 = rand_between(0, snip->count+1);
        idx2 = rand_between(ZYDIS_MNEMONIC_JB, ZYDIS_MNEMONIC_JZ + 1);
        tmp = snippet_allocate(snip);

        CHECK_PTR(tmp, EGENERIC);
        ret = create_instruction(cfg, tmp, (ZydisMnemonic)idx2, NULL);
        if (ret < 0) {
            CHECK_INT(arena_free(snip->instructions, tmp), EGENERIC);
            //CHECK_INT(ret, EGENERIC);
            return mutate_snippet(cfg, snip, MUT_ADD_RAND); // Fallback incase generation fails
        }

        if (idx1 >= snip->count)
            CHECK_INT(snippet_append(snip, tmp), EGENERIC);
        else
            CHECK_INT(snippet_insert_at(snip, tmp, idx1), EGENERIC);
        break;
    case MUT_ADD_VZEROUPPER:
        idx1 = rand_between(0, snip->count+1);
        tmp = snippet_allocate(snip);

        CHECK_PTR(tmp, EGENERIC);
        ret = create_instruction(cfg, tmp, ZYDIS_MNEMONIC_VZEROUPPER, NULL);
        if (ret < 0) {
            CHECK_INT(arena_free(snip->instructions, tmp), EGENERIC);
            //CHECK_INT(ret, EGENERIC);
            return mutate_snippet(cfg, snip, MUT_ADD_RAND); // Fallback incase generation fails
        }

        if (idx1 >= snip->count)
            CHECK_INT(snippet_append(snip, tmp), EGENERIC);
        else
            CHECK_INT(snippet_insert_at(snip, tmp, idx1), EGENERIC);
        break;
    case MUT_SNIPPET:
    default:
        fprintf(stderr, "Unsupported mutation\n");
        return -1;
    }

    return pipeline_validate(snip);
}

int create_index(config *cfg)
{
    double successes = 0.0;
    instruction_t ins = {0};
    
    for (ZydisMnemonic mnemonic=1; mnemonic <= ZYDIS_MNEMONIC_MAX_VALUE; mnemonic++) {
        successes = 0.0;
        memset(&ins, 0, sizeof(instruction_t));

        for (size_t i=0; i < cfg->index.iterations; i++) {
            if (create_instruction(cfg, &ins, mnemonic, NULL) != 0)
                continue;
            
            successes++;
        }

        // We only want reliable instructions
        if ((successes / cfg->index.iterations) < cfg->index.threshold)
            continue;
        
        cfg->index.instructions[cfg->index.count++] = mnemonic;
    }

    if (cfg->index.count == 0)
        return -1;

    return 0;
}

int test_generate_all(config *cfg, ZydisMachineMode mode, bool count, ZydisMnemonic start)
{
    size_t num_variations;
    size_t size;
    ZyanStatus status;
    int ret;
    uint8_t buf[ZYDIS_MAX_INSTRUCTION_LENGTH];
    ZydisDisassembledInstruction ins;
    const ZydisEncodableInstruction *variations;
    instruction_t req;

    size_t most_vars = 0;
    size_t total_variations = 0;
    size_t encoded = 0;
    size_t skipped = 0; // Amount skipped due to encoding constraints
    
    for (int i=start; i < ZYDIS_MNEMONIC_MAX_VALUE; i++) {
        ZydisMnemonic mnemonic = i;
        num_variations = ZydisGetEncodableInstructions(mnemonic, &variations);
        total_variations += num_variations;
        
        if (num_variations > most_vars)
            most_vars = num_variations;

        for (size_t j=0; j < num_variations; ++j) {
            // Some things we cannot encode in a given machine mode
            if (!(variations[j].modes & (_ZydisGetMachineModeWidth(cfg->mode) >> 4))) {
                skipped += 1;
                continue;
            }

            memset(&req, 0, sizeof(instruction_t));
            printf("===== %s VARIATION %ld =====\n", ZydisMnemonicGetString(mnemonic), j);
            
            ret = generate_instruction(cfg, &req, &variations[j]);
            if (count && (ret < 0))
                continue;
            else
                CHECK_INT(ret, EGENERIC);

            encoded++;

            size = ZYDIS_MAX_INSTRUCTION_LENGTH; 
            status = ZydisEncoderEncodeInstruction(&req.req, buf, &size);
            CHECK_ZYAN(status);

            status = ZydisDisassembleIntel(mode, 0, buf, ZYDIS_MAX_INSTRUCTION_LENGTH, &ins);
            CHECK_ZYAN(status);

            printf("Successfully encoded: %s\n", ins.text);
        }
    }
    
    printf("!Successfully encoded %ld/%ld instruction variations (most %ld, skipped %ld)\n", encoded, total_variations, most_vars, skipped);
    return 0;
}

int walk_instruction_defs(ZydisMachineMode mode, FILE *f)
{
    int num_variations;
    size_t total = 0;
    const ZydisEncodableInstruction *variations;
    const ZydisInstructionDefinition *def;
    const ZydisOperandDefinition *op_def;

    for (int i=0; i < ZYDIS_MNEMONIC_MAX_VALUE; i++) {
        ZydisMnemonic mnemonic = i;
        num_variations = ZydisGetEncodableInstructions(mnemonic, &variations);
        //printf("There are %d variations for instruction %s\n", num_variations, ZydisMnemonicGetString(mnemonic));

        int total_defs = 0;
        for (int j=0; j < num_variations; ++j) {
            ZydisGetInstructionDefinition(variations[j].encoding, variations[j].instruction_reference, &def);
            //printf("[%d] %s ", variations[j].instruction_reference, ZydisMnemonicGetString(mnemonic));

            if (def == NULL)
                _exit(EXIT_FAILURE);

            if (def->requires_protected_mode
                && mode == ZYDIS_MACHINE_MODE_REAL_16)
                continue;

            if (def->no_compat_mode
                && ((mode == ZYDIS_MACHINE_MODE_LONG_COMPAT_16) ||
                    (mode == ZYDIS_MACHINE_MODE_LONG_COMPAT_32)))
                continue;
            
            fprintf(f, "%s,%s,%s\n", zydis_instruction_encoding_strings[variations->encoding], ZydisMnemonicGetString(def->mnemonic), ZydisCategoryGetString(def->category));

            // op_def = ZydisGetOperandDefinitions(def);
            // for (int k=0; k < def->operand_count; k++) {
            //     if (k >= def->operand_count_visible)
            //         break;
            //     
            //     //printf("%s ", zydis_operand_type_strings[get_operand_type(op_def)]);
            //     printf("%s ", zydis_semantic_optype_strings[op_def[k].type]);
            //     printf("(%s) ", zydis_operand_encoding_strings[op_def[k].op.encoding]);
            //     
            //     if (op_def[k].type == ZYDIS_SEMANTIC_OPTYPE_IMPLICIT_REG)
            //         printf("[%s] ", zydis_implreg_type_strings[op_def[k].op.reg.type]);
            //
            //     if (op_def[k].type == ZYDIS_SEMANTIC_OPTYPE_IMPLICIT_MEM)
            //         printf("[%s] ", zydis_implmem_type_strings[op_def[k].op.mem.base]);
            //
            //     //print_op_def(&op_def[k]);
            // }

            //printf("\n");
            total_defs += 1;
        }
        
        total += total_defs;
        //printf("There were a total of %d valid defs for %s\n", total_defs, ZydisMnemonicGetString(mnemonic));
    }
    
    //printf("There were amount of defs: %ld\n", total);
    return 0;
}


