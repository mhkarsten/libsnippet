#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <sys/random.h>
#include <stdbool.h>
#include <Zydis/Zydis.h>
#include <Zydis/Mnemonic.h>
#include <Zydis/Wrapper.h>
#include <Zydis/Utils.h>
#include <Zydis/Encoder.h>
#include <Zydis/SharedTypes.h>
#include <Zycore/Types.h>
#include <Zycore/API/Memory.h>
#include <Zydis/Internal/SharedData.h>
#include <Zydis/Internal/EncoderData.h>
#include <Zydis/Internal/FormatterBase.h>

#include "Zydis/DecoderTypes.h"

#include "snippet.h"
#include "common.h"
#include "list.h"
#include "error.h"
#include "arena.h"
#include "pipeline.h"

typedef struct bb_ctx_ {
    basic_block_t *bb;
    list_node bb_list;
    size_t idx;
    uint8_t *leaders;
    basic_block_t **block_map;
} bb_ctx_t;

int pipeline_init(pipeline_t *pipe)
{
    memset(pipe, 0, sizeof(pipeline_t));

    pipe->count = 0;
    CHECK_INT(list_init(&pipe->head), EGENERIC);
    return 0;
}

int pipeline_register(pipeline_t *pipe, void *fn, pass_type type)
{
    if ((pipe->count + 1) == INIT_PASS_SIZE)
        return -1;

    pass_t *pass = &pipe->passes[pipe->count];
    
    pass->type = type;
    pass->fn = fn;

    // Insert new pass into the list
    CHECK_INT(list_insert_before(&pass->node, &pipe->head), EGENERIC);
    pipe->count++;

    return 0;
}

// Best case O(n * (2 * N))
int pipeline_execute(pipeline_t *pipe, snippet_t *snip)
{
    list_node *temp, *pos;
    pass_t *curr_pass;

    list_for_each_safe(pos, temp, &pipe->head) {
        curr_pass = container_of(pos, pass_t, node);
        
        if (curr_pass->fn == NULL)
            continue;
        
        // Always validate the metadata first, it might be stale
        CHECK_INT(validate_metadata(snip), EGENERIC);

        switch (curr_pass->type) {
        case PASS_TYPE_FN:
            CHECK_INT(((pass_fn)curr_pass->fn)(snip), EGENERIC); // TODO: Something errors here as well
            break;
        case PASS_TYPE_WALKER:
            CHECK_INT(walk_instructions(snip, ((walk_fn)curr_pass->fn), NULL), EGENERIC);
            break;
        default:
            return -1;
        }
    }

    return 0;
}

// Calculates the offset between two instructions
static inline int64_t offset_between(instruction_t *src, instruction_t *dst)
{
    return dst->address - (src->address + src->length);
}

static basic_block_t *find_bb_containing(list_node *bb_list, instruction_t *ins)
{
    list_node *temp, *pos;
    basic_block_t *bb;

    list_for_each_safe(pos, temp, bb_list) {
        bb = container_of(pos, basic_block_t, node);
        
        if (ins->address == ADDRESS_INVALID)
            return NULL;
        
        // Basic blocks are always in address order
        if (ins->address >= bb->start->address 
            && ins->address <= bb->end->address)
            return bb;
    }

    return NULL;
}

static instruction_t *find_target(instruction_t *targets[], size_t min, size_t max, uint64_t target_addr)
{
    if (min > max)
        return NULL;

    //size_t mid = min + ((max - min) / 2);
    size_t mid = (max + min) / 2;

    //printf("Attempting search: %ld - %ld (0x%lx) - %ld\n", min, mid, targets[mid]->address, max);
    if (targets[mid]->address == target_addr)
        return targets[mid];

    if (targets[mid]->address > target_addr) {
        if (mid == 0)
            return NULL;

        return find_target(targets, min, mid - 1, target_addr);
    }
    //if (targets[mid]->address < target_addr)
    return find_target(targets, mid + 1, max, target_addr);
    
    // This should never be reached
    //return NULL;
}

static int mark_leaders(snippet_t *snip, instruction_t *ins, void *userdata)
{
    uint8_t *leaders = (uint8_t *)userdata;

    if (!is_jump(ins->req.mnemonic))
        return 0;
    
    if (is_conditional(ins) && !list_is_last(&ins->node, &snip->head))
        bitmap_set(leaders, ins->idx + 1);
    
    // All jumps set at least this target
    bitmap_set(leaders, ins->jump_target->idx);
    
    return 0;
}

int get_branch_imm_width(instruction_t *ins) // In bytes
{
    switch (ins->req.branch_type) {
    case ZYDIS_BRANCH_TYPE_SHORT:
        return 1;
    case ZYDIS_BRANCH_TYPE_NEAR:
    case ZYDIS_BRANCH_TYPE_FAR:
        if (ins->req.branch_width == ZYDIS_BRANCH_WIDTH_NONE)
            return -1;
        switch (ins->req.branch_width) {
        case ZYDIS_BRANCH_WIDTH_8:
            return 1;
        case ZYDIS_BRANCH_WIDTH_16:
            return 2;
        case ZYDIS_BRANCH_WIDTH_32:
            return 4;
        case ZYDIS_BRANCH_WIDTH_64:
            return 8;
        default:
            return 1;
        }
        break;
    default:
        return -1;
    }
}

static int init_basic_block(snippet_t *snip, instruction_t *ins, void *userdata)
{
    bb_ctx_t *ctx = (bb_ctx_t *)userdata;
    list_node *pos = &ins->node;

    if (!bitmap_get(ctx->leaders, ins->idx))
        return 0;
    
    // If we are currently in a block, terminate it
    if (ctx->bb) {
        ctx->bb->end = container_of(pos->prev, instruction_t, node);
        CHECK_INT(list_insert_after(&ctx->bb->node, &ctx->bb_list), EGENERIC);
    }
    
    // Init the next basic block
    ctx->bb = arena_allocate(snip->basic_blocks);
    ctx->bb->index = ctx->idx;
    ctx->bb->start = ins;
    ctx->bb->end = NULL;
    ctx->block_map[ins->idx] = ctx->bb; // Add a mapping between instruction -> bb (only needed for leaders)

    ctx->idx++;

    // If this is the first basic block, add it to the snippet
    if (list_is_first(pos, &snip->head))
        snip->blocks = ctx->bb;
    
    // This is the last instruction, terminate the final block
    if (list_is_last(pos, &snip->head)) {
        ctx->bb->end = ins;
        CHECK_INT(list_insert_after(&ctx->bb->node, &ctx->bb_list), EGENERIC);
    }
    
    return 0;
}

// Going into this function instructions are expected to have valid targets, and valid index
// 3 * O(N)
int generate_basic_blocks(snippet_t *snip)
{
    bb_ctx_t ctx = {0};
    uint8_t leaders[(snip->count/8) + 1];
    basic_block_t *block_map[snip->count];
    list_node *temp, *pos;
    
    // Make sure leaders is empty
    memset(leaders, 0, (snip->count/8) + 1);

    // Init some context
    ctx.leaders = leaders;
    ctx.block_map = block_map;
    ctx.idx = 0;

    // TODO: Maybe free existing basic blocks instead of erroring later
    if (!snip || snip->basic_blocks)
        return -1;

    CHECK_INT(list_init(&ctx.bb_list), EGENERIC);

    // Mark all leaders (bb start) in a bitmap (The first instruction is always a leader)
    leaders[0] = leaders[0] & 1;
    CHECK_INT(walk_instructions(snip, mark_leaders, (void *)leaders), EGENERIC);
    
    // Create and allocate all of the needed basic block structs
    CHECK_INT(walk_instructions(snip, init_basic_block, (void *)&ctx), EGENERIC);

    // Finally connect the basic blocks
    list_for_each_safe(pos, temp, &ctx.bb_list) {
        ctx.bb = container_of(pos, basic_block_t, node);
        
        if (is_conditional(ctx.bb->end) || is_exception(ctx.bb->end))
            ctx.bb->next[0] = block_map[container_of(ctx.bb->end->node.next, instruction_t, node)->idx];
        
        if (is_jump(ctx.bb->end->req.mnemonic) )
            ctx.bb->next[1] = block_map[ctx.bb->end->jump_target->idx];

        // Other cases like return / exit have no successors
    }

    return 0;
}

int validate_metadata(snippet_t *snip)
{
    ZyanU8 buf[ZYDIS_MAX_INSTRUCTION_LENGTH] = {0};
    uint64_t address = snip->start_address;
    size_t idx = 0;
    ZyanUSize length;
    list_node *temp, *pos;
    instruction_t *ins;
    ZyanStatus status;
    
    list_for_each_safe(pos, temp, &snip->head) {
        ins = container_of(pos, instruction_t, node);

        // Attempt encoding to get the length
        length = ZYDIS_MAX_INSTRUCTION_LENGTH;

        status = ZydisEncoderEncodeInstruction(&ins->req, buf, &length);
        //CHECK_ZYAN_WMSG(status, "Error encoding instruction %ld", ins->idx);

        // Fallback incase an instruction becomes unencodable. This should not happen often.
        if (ZYAN_FAILED(status)) {
            //printf("Failed to encode, silently removing\n");
            CHECK_INT(snippet_remove(snip, ins), EGENERIC);
            continue;
        }
        
        ins->address = address;
        ins->length = length;
        ins->idx = idx;
        
        idx++;
        address += length;
    }

    return 0;
}

int validate_registers(snippet_t *snip, instruction_t *ins, void *userdata)
{
    ZydisEncoderOperand *op;
    ZydisRegisterClassLookupItem item;
    ZydisRegisterClass reg_class;
    ZydisEncoderInstructionMatch match;
    ZydisRegister old_reg, new_reg;
    
    CHECK_ZYAN(ZydisFindMatchingDefinition(&ins->req, &match));
    
    for (int i=0; i < ins->req.operand_count; i++) {
        op = &ins->req.operands[i];
        
        if (op->type != ZYDIS_OPERAND_TYPE_REGISTER)
            continue;
        
        // We are only interested in operands that will be written to
        if (!(match.operands[i].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE))
            continue;
        
        reg_class = ZydisRegisterGetClass(op->reg.value);
        ZydisRegisterGetClassLookupItem(reg_class, &item);
    
        // Only get largest enclosing for gpr type registers
        switch (reg_class) {
        case ZYDIS_REGCLASS_GPR64:
        case ZYDIS_REGCLASS_GPR32:
        case ZYDIS_REGCLASS_GPR16:
        case ZYDIS_REGCLASS_GPR8:
            old_reg = ZydisRegisterGetLargestEnclosing(ins->req.machine_mode, op->reg.value);
            break;
        default:
            old_reg = op->reg.value;
            break;
        }
        
        // Rewrite certain special / reserved registers
        switch (old_reg) {
        case ZYDIS_REGISTER_RSP:
        case ZYDIS_REGISTER_RSI:
        case ZYDIS_REGISTER_SS:
        case ZYDIS_REGISTER_RDI:
            // Replace invalid registers with RAX - RDX to avoid rex encoding issues
            new_reg = ZydisRegisterGetId(rand_exclude(ZYDIS_REGISTER_RAX, ZYDIS_REGISTER_R15, ZYDIS_REGISTER_RSP, ZYDIS_REGISTER_RDI));
            break;
        case ZYDIS_REGISTER_XMM0:
            new_reg = ZydisRegisterGetId(rand_exclude(item.lo, item.hi, snip->index_xreg, snip->index_xreg));
            break;
        case ZYDIS_REGISTER_YMM0:
            new_reg = ZydisRegisterGetId(rand_exclude(item.lo, item.hi, snip->index_yreg, snip->index_yreg));
            break;
        case ZYDIS_REGISTER_ZMM0:
            new_reg = ZydisRegisterGetId(rand_exclude(item.lo, item.hi, snip->index_zreg, snip->index_zreg));
            break;
        default:
            continue; // Register not special go to next ieration
        }

        op->reg.value = ZydisRegisterEncode(reg_class, new_reg);
    }

    return 0;
}

int validate_memory_operands(snippet_t *snip, instruction_t *ins, void *userdata)
{
    ZydisEncoderOperand *op;
    ZydisRegisterClass reg_class;
    ZydisRegister idx_reg;
    ZydisEncoderInstructionMatch match;
    
    CHECK_ZYAN(ZydisFindMatchingDefinition(&ins->req, &match));

    for (int i=0; i < ins->req.operand_count; i++) {
        op = &ins->req.operands[i];
        
        if (op->type != ZYDIS_OPERAND_TYPE_MEMORY)
            continue;
        
        // RIP relative instructions must be handled later, as they are sensitive to position changes ect.
        if (is_rip_relative(ins, op))
            continue;

        // Make sure we are always using valid base
        if (op->mem.base) {
            reg_class = ZydisRegisterGetClass(op->mem.base);
            op->mem.base = ZydisRegisterEncode(reg_class, ZydisRegisterGetId(snip->base_reg));
        }
        
        if (op->mem.displacement && !op->mem.base) {
            op->mem.displacement = snip->mem_start + rand_between(0, snip->mem_sz - 64);
        } else if (op->mem.displacement) {
            // Keep halfing displacement until we reach a reasonable point
            while (llabs(op->mem.displacement) >= snip->mem_sz)
                op->mem.displacement = op->mem.displacement / 2;  
        }

        // Ensure that index is one of our reserved registers
        if (!op->mem.index)
            continue;

        // BND type instructions ignore scale
        if (match.operands[i].type != ZYDIS_SEMANTIC_OPTYPE_MIB && !op->mem.scale)
            return -101;
        
        // Some instructions need special handling for index
        switch (match.operands[i].type) {
        case ZYDIS_SEMANTIC_OPTYPE_MEM_VSIBX:
            idx_reg = snip->index_xreg;
            break;
        case ZYDIS_SEMANTIC_OPTYPE_MEM_VSIBY:
            idx_reg = snip->index_yreg;
            break;
        case ZYDIS_SEMANTIC_OPTYPE_MEM_VSIBZ:
            idx_reg = snip->index_zreg;
            break;
        default:
            idx_reg = snip->index_reg;
            break;
        }
        
        reg_class = ZydisRegisterGetClass(op->mem.index);
        op->mem.index = ZydisRegisterEncode(reg_class, ZydisRegisterGetId(idx_reg));
    }

    return 0;
}

int validate_rip_relative_mem(snippet_t *snip)
{
    bool changed = true;
    list_node *pos, *temp;
    instruction_t *ins;
    ZydisEncoderOperand *op;
    uint64_t target;

    while (changed) {
        changed = false;

        CHECK_INT(validate_metadata(snip), EGENERIC);

        list_for_each_safe(pos, temp, &snip->head) {
            ins = container_of(pos, instruction_t, node);
            for (int i=0; i < ins->req.operand_count; i++) {
                op = &ins->req.operands[i];

                if (op->type != ZYDIS_OPERAND_TYPE_MEMORY || !is_rip_relative(ins, op))
                    continue;
                
                // Check that we have some address (assumed to be valid)
                if (!ins->address)
                    return -25;
                
                target = ins->address + op->mem.displacement;
                
                // If we are not changing displacement, there is no issue
                if (target < snip->mem_start || target >= (snip->mem_start + snip->mem_sz))
                    changed = true;
                else
                    continue;
                
                // Calcuate a new displacement based on the old one
                op->mem.displacement = (snip->mem_start - ins->address) + llabs(op->mem.displacement) % (snip->mem_sz - 128);
            }
        }
    }

    return 0;
}

// Goes through each jump and ensures that they all use immediates, and have valid targets
// O(n^2)
int validate_jump_operands(snippet_t *snip, instruction_t *ins, void *userdata)
{
    if (!is_jump(ins->req.mnemonic))
        return 0;

    int jmp_tgt_idx = get_jmp_op_idx(ins);
    CHECK_INT(jmp_tgt_idx, EGENERIC);
    
    // Invalidate non immediate jumps
    if (ins->req.operands[jmp_tgt_idx].type != ZYDIS_OPERAND_TYPE_IMMEDIATE) {
        memset(&ins->req.operands[jmp_tgt_idx], 0, sizeof(ZydisEncoderOperand));
        ins->req.operands[jmp_tgt_idx].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
        ins->jump_target = NULL;
    }
    
    // Invalidate jumps tat target no longer present instructions
    if (!snippet_contains(snip, ins->jump_target))
        ins->jump_target = NULL;

    // Rewrite short jumps, they are too hard to serialize at the moment
    if (ins->req.branch_type == ZYDIS_BRANCH_TYPE_SHORT)
        ins->req.branch_type = ZYDIS_BRANCH_TYPE_NEAR;

    // Since this generally gets run after mutation / generation, its important to give new targets
    if (ins->jump_target == NULL) {
        // Select a new target
        switch (ins->req.branch_type) {
        case ZYDIS_BRANCH_TYPE_SHORT:   // We rewrite short jumps
            return -1;
        case ZYDIS_BRANCH_TYPE_NEAR:
        case ZYDIS_BRANCH_TYPE_FAR:
        default:
            ins->jump_target = snippet_get(snip, rand_exclude(0, snip->count - 1, ins->idx, ins->idx));
            break;
        }
        
        CHECK_PTR(ins->jump_target, EGENERIC);
        
        // These might no longer be correct, ideally verify again
        ins->target_addrs = ins->jump_target->address;
        ins->req.operands[jmp_tgt_idx].imm.s = offset_between(ins, ins->jump_target);
    }

    return 0;
}

// Instructions are expected to be in a properly sorted order w.r.t. their addresses
// O(N) + O(N log N)
int validate_jump_targets(snippet_t *snip)
{
    list_node *temp, *pos;
    instruction_t *targets[snip->count];
    instruction_t *ins;
    int i = 0;
    
    if (!snip)
        return -1;

    // First generate a (hopefully sorted) list of instructions
    list_for_each_safe(pos, temp, &snip->head) {
        targets[i++] = container_of(pos, instruction_t, node);
    }
    
    // Next iterate through and find the target of each jump
    list_for_each_safe(pos, temp, &snip->head) {
        ins = container_of(pos, instruction_t, node);
        
        if (!is_imm_jmp(ins))
            continue;
        
        if (ins->target_addrs == ADDRESS_INVALID)
            return -66;

        // When a jump is found perform binary search to locate the target
        //printf("Attempting to find jump target for ins %ld: 0x%lx\n", ins->idx, ins->target_addrs);
        ins->jump_target = find_target(targets, 0, snip->count - 1, ins->target_addrs);
        
        // Sometimes (for whatever reason) we can no longer find the target here, its usually no longer exists or is off by 1
        if (!ins->jump_target)
            ins->jump_target = snippet_get(snip, rand_exclude(0, snip->count-1, ins->idx, ins->idx));
        
        CHECK_PTR(ins->jump_target, EGENERIC);
    }

    return 0;
}

// The jump offsets/addresses can not be assumed to be valid here (this is what we are calculating)
// O(n^2) or greater
int validate_jump_offsets(snippet_t *snip)
{
    bool changed = true;
    int64_t new_offset;
    list_node *temp, *pos;
    instruction_t *ins;

    if (!snip)
        return -102;

    while (changed) {
        changed = false;
        // Set the length (and thus address) of all instructions
        // TODO: Maybe restructure this as a pass
        CHECK_INT(validate_metadata(snip), EGENERIC);

        list_for_each_safe(pos, temp, &snip->head) {
            ins = container_of(pos, instruction_t, node);
            
            if (!is_imm_jmp(ins))
                continue;

            if (ins->jump_target == JUMP_INVALID)
                return -56;
            
            // Jump arguments are always signed immediates
            new_offset = offset_between(ins, ins->jump_target);
            //printf("Creating new offset target (%p)->%p\n", (void *) ins->address, (void *)ins->address + new_offset);
            //printf("New offset between %ld and %ld: %ld\n", ins->idx, ins->jump_target->idx, new_offset);
            if (new_offset != ins->req.operands[get_jmp_op_idx(ins)].imm.s)
                changed = true;
            
            ins->req.operands[get_jmp_op_idx(ins)].imm.s = new_offset;
        }
    }

    return 0;
}

// TODO: Extend this to check the current user priv (for kernel fuzzing)
int validate_instructions(snippet_t *snip, instruction_t *ins, void *userdata)
{
    // Just remove all priv instructions
    if (ins->privileged) {
        CHECK_INT(snippet_remove(snip, ins), EGENERIC);
        return 0;
    }

    // This is just here because it doesnt fit elsewhere
    switch (ins->req.mnemonic) {
    case ZYDIS_MNEMONIC_SYSCALL: // Syscalls / kernel stuff cannot be properly serialized from userspace
    case ZYDIS_MNEMONIC_SYSENTER:
    case ZYDIS_MNEMONIC_RDRAND:  // These have randomized output
    case ZYDIS_MNEMONIC_RDSEED:
    case ZYDIS_MNEMONIC_RDTSC:
    case ZYDIS_MNEMONIC_RDTSCP:
    case ZYDIS_MNEMONIC_RDPMC:
    case ZYDIS_MNEMONIC_RDPRU:
    case ZYDIS_MNEMONIC_RDPID:
    case ZYDIS_MNEMONIC_TPAUSE: // Behaves differently?? causes different rflags.cf value
    case ZYDIS_MNEMONIC_LOOP:   // These loop instructions only have 1 byte offset, making them very hard to serialize
    case ZYDIS_MNEMONIC_LOOPNE:
    case ZYDIS_MNEMONIC_LOOPE:
    case ZYDIS_MNEMONIC_POPFQ:  // These can cause the next instruction to sigtrap
    case ZYDIS_MNEMONIC_POPFD:
    case ZYDIS_MNEMONIC_POPF:
    case ZYDIS_MNEMONIC_FNSTENV: // These include eip, fip, or rip in their output
    case ZYDIS_MNEMONIC_FNSAVE:
    case ZYDIS_MNEMONIC_FXSAVE:
    case ZYDIS_MNEMONIC_FXSAVE64:
    case ZYDIS_MNEMONIC_INT1:       // Hardware breakpoints cause mayhem sometimes?
    case ZYDIS_MNEMONIC_LDMXCSR:    // Prevent writing to mxcsr which can change signal handling
    case ZYDIS_MNEMONIC_VLDMXCSR:
    case ZYDIS_MNEMONIC_WRPKRU:
    case ZYDIS_MNEMONIC_INT:        // This is mainly for int 0x80, we dont want syscalls
        CHECK_INT(snippet_remove(snip, ins), EGENERIC);
        break;
    default:
        break;
    }


    // TODO: remove interrupt instructions maybe
    
    return 0;
}

int pipeline_validate(snippet_t *snip)
{
    pipeline_t pipe;

    CHECK_INT(pipeline_init(&pipe), EGENERIC);
    CHECK_INT(pipeline_register(&pipe, validate_instructions, PASS_TYPE_WALKER), EGENERIC);
    CHECK_INT(pipeline_register(&pipe, validate_registers, PASS_TYPE_WALKER), EGENERIC);
    CHECK_INT(pipeline_register(&pipe, validate_memory_operands, PASS_TYPE_WALKER), EGENERIC);
    CHECK_INT(pipeline_register(&pipe, validate_jump_operands, PASS_TYPE_WALKER), EGENERIC);

    // Fallback if target is valid but points to an invalid location
    CHECK_INT(pipeline_register(&pipe, validate_jump_targets, PASS_TYPE_FN), EGENERIC); 
    
    // These two techinically can invalidate the other
    CHECK_INT(pipeline_register(&pipe, validate_rip_relative_mem, PASS_TYPE_FN), EGENERIC);
    CHECK_INT(pipeline_register(&pipe, validate_jump_offsets, PASS_TYPE_FN), EGENERIC);
    
    return pipeline_execute(&pipe, snip);
}

int pipeline_encode(snippet_t *snip)
{
    pipeline_t pipe;

    CHECK_INT(pipeline_init(&pipe), EGENERIC);
    CHECK_INT(pipeline_register(&pipe, validate_jump_offsets, PASS_TYPE_FN), EGENERIC);
    
    return pipeline_execute(&pipe, snip);
}

int pipeline_decode(snippet_t *snip)
{
    pipeline_t pipe;

    CHECK_INT(pipeline_init(&pipe), EGENERIC);
    CHECK_INT(pipeline_register(&pipe, validate_jump_targets, PASS_TYPE_FN), EGENERIC);

    return pipeline_execute(&pipe, snip);
}
