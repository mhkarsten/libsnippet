#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <Zydis/Zydis.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "config.h"
#include "error.h"
#include "list.h"
#include "debug_strings.h"
#include "snippet.h"
#include "arena.h"

void test_list(snippet_t *snip)
{
    list_node *node = snip->head.next;
    instruction_t *ins, *next, *prev;

    while (node != &snip->head) {
        ins = container_of(node, instruction_t, node);

        if (!list_is_first(node, &snip->head))
            prev = container_of(node->prev, instruction_t, node);
        else
            prev = NULL;

        if (!list_is_last(node, &snip->head))
            next = container_of(node->next, instruction_t, node);
        else
            next = NULL;
        
        if (prev)
            printf("(%ld) <- ", prev->idx);

        printf("(%ld)", ins->idx);

        if (next)
            printf(" -> (%ld)", next->idx);

        printf("\n");

        node = node->next;
    }
}

int snippet_init(config *cfg, snippet_t *snip)
{
    memset(snip, 0, sizeof(snippet_t));

    snip->instructions = arena_init(sizeof(instruction_t), MAX_SNIPPET_SIZE);
    CHECK_PTR(snip->instructions, EGENERIC);

    snip->basic_blocks = arena_init(sizeof(basic_block_t), MAX_SNIPPET_SIZE);
    
    CHECK_INT(list_init(&snip->head), EGENERIC);
    
    snip->count = 0;
    snip->start_address = cfg->snippet_code.address;
    snip->mem_start = cfg->snippet_memory.address;
    snip->mem_sz = cfg->snippet_memory.size;
    snip->index_reg = cfg->mem_index_register;
    snip->index_xreg = cfg->mem_vsibx_register;
    snip->index_yreg = cfg->mem_vsiby_register;
    snip->index_zreg = cfg->mem_vsibz_register;
    snip->base_reg = cfg->mem_base_register;
    return 0;
}

// Frees all instructions in a snippet so they can be used again
int snippet_free(snippet_t *snip)
{
    list_node *pos, *temp;
    instruction_t *ins;
    basic_block_t *block;
    basic_block_t *queue[snip->count]; // Essentially hope this does not get too large
    size_t queue_pos;

    if (!snip)
        return -1;

    // Go through and free all instructions, adding them to the free list
    list_for_each_safe(pos, temp, &snip->head) {
        ins = container_of(pos, instruction_t, node);
        CHECK_INT(arena_free(snip->instructions, ins), EGENERIC);
    }

    snip->count = 0;
    
    // Reset the list, all instructions will be wiped before allocation anyways
    CHECK_INT(list_init(&snip->head), EGENERIC);
    
    if (!snip->basic_blocks || !snip->blocks)
        return 0;
    
    queue[0] = snip->blocks;
    queue_pos = 1;

    while (queue_pos != 0) {
        block = queue[queue_pos-1];
        
        // If there are no valid next blocks, free the current one and continue
        if (!block->next[0] && !block->next[1]) {
            CHECK_INT(arena_free(snip->basic_blocks, block), EGENERIC);
            queue_pos--;
            continue;
        }

        // Add the next blocks to the queue
        for (int i=0; i < 2; i++) {
            if (block->next[i]) {
                queue[queue_pos] = block->next[i];
                queue_pos++;
            }
        }
    }
    
    // Reset blocks pointer as well
    snip->blocks = NULL;
    
    return 0;
}

// Clears all the state from a snippet so it can be used again
int snippet_destroy(snippet_t *snip)
{
    if (snip->instructions)
        CHECK_INT(arena_destroy(snip->instructions), EGENERIC);
    
    if (snip->basic_blocks)
        CHECK_INT(arena_destroy(snip->basic_blocks), EGENERIC);
    
    // Since the memory for these was unmapped, we can simply invalidate the pointers
    snip->blocks = NULL;
    CHECK_INT(list_init(&snip->head), EGENERIC);
    
    snip->count = 0;
    snip->start_address = 0;

    return 0;
}

int get_jmp_op_idx(instruction_t *ins)
{
    switch (ins->req.operand_count) {
    case 1:
        return 0;
    case 2:
        return 1;
    default:
        return -1;
    }
}

// TODO: finish these functions
bool is_jump(ZydisMnemonic ins)
{
    return  (ins >= ZYDIS_MNEMONIC_JB && ins <= ZYDIS_MNEMONIC_JZ) || (ins >= ZYDIS_MNEMONIC_LOOP && ins <= ZYDIS_MNEMONIC_LOOPNE);
}

bool is_imm_jmp(instruction_t *ins)
{
    return is_jump(ins->req.mnemonic) && ins->req.operands[get_jmp_op_idx(ins)].type == ZYDIS_OPERAND_TYPE_IMMEDIATE;
}


bool is_terminator(instruction_t *ins)
{
    return false;
}

bool is_conditional(instruction_t *ins) 
{
    return false;
}

bool is_exception(instruction_t *ins)
{
    return false;
}

// Maybe add more conditions to this later for register operands ect.
bool is_rip_relative(instruction_t *ins, ZydisEncoderOperand *op)
{
    return ZydisRegisterGetLargestEnclosing(ins->req.machine_mode, op->mem.base) == ZYDIS_REGISTER_RIP;
}

ZydisInstructionCategory get_category(instruction_t *ins) 
{
    ZydisEncoderInstructionMatch match;

    CHECK_ZYAN(ZydisFindMatchingDefinition(&ins->req, &match));
    return match.base_definition->category;
}

int print_instruction(snippet_t *snip, instruction_t *ins, void *userdata)
{
    ZydisEncoderOperand *operand;
    printf("%ld\t[%ld] %s ", ins->idx, ins->length, ZydisMnemonicGetString(ins->req.mnemonic));
        
    for (int op = 0; op < ins->req.operand_count; op++) {
        operand = &ins->req.operands[op];
        printf("%s ", zydis_operand_type_strings[operand->type]);
        
        switch (operand->type) {
        case ZYDIS_OPERAND_TYPE_MEMORY:
            printf("[");
            if (operand->mem.base)
                printf("%s", ZydisRegisterGetString(operand->mem.base));
            if (operand->mem.index) {
                if (operand->mem.base)
                    printf(" + ");

                printf("(%s * %d)", ZydisRegisterGetString(operand->mem.index), operand->mem.scale);
            }
            if (operand->mem.displacement) {
                if (operand->mem.base || operand->mem.index)
                    printf(" + ");

                printf("0x%lx", operand->mem.displacement);
            }
            printf("] (%hu)", operand->mem.size);
            break;
        case ZYDIS_OPERAND_TYPE_IMMEDIATE:
            if (is_jump(ins->req.mnemonic) && ins->jump_target)
                printf("0x%lx (%ld)", operand->imm.s,  ins->jump_target->idx);
            else
                printf("0x%lx", operand->imm.u);
            break;
        case ZYDIS_OPERAND_TYPE_REGISTER:
                printf("%s (is4 %d)", ZydisRegisterGetString(operand->reg.value), operand->reg.is4);
            break;
        default:
            printf("UNSUPPORTED");
            break;
        }

        if (ins->req.operand_count > 1 
            && op != (ins->req.operand_count - 1))
            printf(", ");
    }

    printf("\n");
    return 0;
}

int snippet_print(snippet_t *snip, FILE *dst, bool use_zydis, bool with_address)
{
    if (!use_zydis)
        return walk_instructions(snip, print_instruction, NULL);

    ZyanU8 instructions[PRINT_BUF_SZ] = {0};
    ZyanUSize offset = 0;
    ZyanUSize address = snip->start_address;
    ZydisDisassembledInstruction ins;
    size_t idx = 0;
    
    CHECK_INT(snippet_encode(snip, instructions, PRINT_BUF_SZ), EGENERIC);

    while (ZYAN_SUCCESS(ZydisDisassembleIntel(
                    ZYDIS_MACHINE_MODE_LONG_64, 
                    address, 
                    instructions + offset, 
                    PRINT_BUF_SZ - offset, 
                    &ins
    ))) {        
        if (with_address)
            CHECK_LIBC(fprintf(dst, "%016" PRIX64 "  %s\n", address, ins.text));
        else
            CHECK_LIBC(fprintf(dst, "%s\n", ins.text));

        offset += ins.info.length;    
        address += ins.info.length;
        idx++;
        
        if (idx >= snip->count)
            break;
    }

    return 0;
}

instruction_t *snippet_allocate(snippet_t *snip)
{
    instruction_t *ins;

    ins = arena_allocate(snip->instructions);
    CHECK_PTR_NULL(ins, EGENERIC);

    return ins;
}

instruction_t *snippet_get(snippet_t *snip, size_t idx)
{
    list_node *node;

    if (!snip || snip->count == 0 || idx >= snip->count)
        return NULL;

    node = list_get(&snip->head, idx);
    CHECK_PTR_NULL(node, EGENERIC);

    return container_of(node, instruction_t, node);
}

// Compares only using pointer values, used to detect invalid jump_targets
bool snippet_contains(snippet_t *snip, instruction_t *ins)
{
    list_node *temp, *pos;

    list_for_each_safe(pos, temp, &snip->head) {
        if (container_of(pos, instruction_t, node) == ins)
            return true;
    }

    return false;
}

int snippet_append(snippet_t *snip, instruction_t *ins)
{
    if (!snip || !ins)
        return -1;
    
    CHECK_INT(list_insert_before(&ins->node, &snip->head), EGENERIC);
    
    snip->count++;
    return 0;
}

// NOTE: This is also very likely to invalidate idx
int snippet_insert_at(snippet_t *snip, instruction_t *ins, size_t idx)
{
    list_node *pos;

    if (!snip || !ins || idx > snip->count)
        return -1;

    if (idx == snip->count)
        return snippet_append(snip, ins);
    
    if (idx == 0)
        pos = &snip->head;
    else
        pos = list_get(&snip->head, idx);
    
    CHECK_PTR(pos, EGENERIC);
    CHECK_INT(list_insert_before(&ins->node, pos), EGENERIC);

    snip->count++;
    return 0;
}

int snippet_insert_before(snippet_t *snip, instruction_t *ins, instruction_t *position)
{
    if (!snip || !ins || !position)
        return -1;
    
    CHECK_INT(list_insert_before(&ins->node, &position->node), EGENERIC);

    snip->count++;
    return 0;
}

// NOTE: This will invalidate the idx field of (most) instructions
int snippet_remove(snippet_t *snip, instruction_t *ins)
{
    if (!snip || snip->count == 0 || !ins)
        return -1;

    // Remove it from the instruction_t list and free it
    CHECK_INT(list_remove(&ins->node), EGENERIC);
    CHECK_INT(arena_free(snip->instructions, ins), EGENERIC);

    snip->count--;
    return 0;
}

int snippet_swap(snippet_t *snip, size_t idx1, size_t idx2)
{
    list_node *pos1, *pos2;

    if (!snip || idx1 >= snip->count || idx2 >= snip->count)
        return -123;
    
    // Same index is a null op
    if (idx1 == idx2)
        return 0;
    
    pos1 = list_get(&snip->head, idx1);
    CHECK_PTR(pos1, EGENERIC);

    pos2 = list_get(&snip->head, idx2);
    CHECK_PTR(pos2, EGENERIC);
    
    CHECK_INT(list_swap(pos1, pos2), EGENERIC);
    
    // Update their indexes
    container_of(pos1, instruction_t, node)->idx = idx2;
    container_of(pos2, instruction_t, node)->idx = idx1;

    return 0;
}

static int walk_basic_blocks_rec(
        snippet_t *snip, basic_block_t *cur_bb, 
        int (*fn)(snippet_t *, basic_block_t *, void *), 
        void *userdata)
{
    if (!cur_bb)
        return 0;

    CHECK_INT(fn(snip, cur_bb, userdata), EGENERIC);
    
    CHECK_INT(walk_basic_blocks_rec(snip, cur_bb->next[0], fn, userdata), EGENERIC);

    return walk_basic_blocks_rec(snip, cur_bb->next[1], fn, userdata);
}

int walk_basic_blocks(
        snippet_t *snip, 
        int (*fn)(snippet_t *, basic_block_t *, void *), 
        void *userdata)
{
    if (!snip->blocks)
        return -1;

    return walk_basic_blocks_rec(snip, &snip->blocks[0], fn, userdata);
}

int walk_instructions(
        snippet_t *snip, 
        int (*fn)(snippet_t *, instruction_t *, void *), 
        void *userdata) 
{
    list_node *temp, *pos;
    instruction_t *ins;

    list_for_each_safe(pos, temp, &snip->head) {
        ins = container_of(pos, instruction_t, node);
        CHECK_INT(fn(snip, ins, userdata), EGENERIC);
    }
    
    return 0;
}

int snippet_encode(snippet_t *snip, ZyanU8 *buf, ZyanUSize buf_sz)
{
    ZyanStatus status;
    ZyanUSize ins_size;
    ZyanUSize buf_pos = 0;
    list_node *temp, *pos;
    instruction_t *ins;
    
    list_for_each_safe(pos, temp, &snip->head) {
        ins = container_of(pos, instruction_t, node);
        ins_size = buf_sz - buf_pos;

        status = ZydisEncoderEncodeInstruction(&ins->req, buf + buf_pos, &ins_size);
        CHECK_ZYAN_WMSG(status, "Error encoding instruction_t %s @ %ld \n", ZydisMnemonicGetString(ins->req.mnemonic), ins->idx);

        if (buf_pos + ins_size > buf_sz)
            return -ESNIPNOMEM;

        buf_pos += ins_size;
     }

    return buf_pos;
}

// The snippet is expected to be already initialized and empty here
int snippet_decode(snippet_t *snip, ZyanU64 rt_address, const ZyanU8 *buf, ZyanUSize buf_sz)
{
    ZyanUSize offset = 0;
    size_t idx = 0;
    instruction_t *ins;
    ZydisDisassembledInstruction dis_ins = {0};
    
    // Disassemble the binary blob, and fill out the instructions
    while (ZYAN_SUCCESS(ZydisDisassembleIntel(
                    ZYDIS_MACHINE_MODE_LONG_64, 
                    rt_address, 
                    buf + offset, 
                    buf_sz - offset, 
                    &dis_ins
    ))) {
        // Grab new memory for the request
        ins = snippet_allocate(snip);
        CHECK_PTR(ins, ESNIPNOMEM);

        // Have Zydis transform back into an encoder request (easier to work with)
        CHECK_ZYAN(ZydisEncoderDecodedInstructionToEncoderRequest(
                    &dis_ins.info, 
                    dis_ins.operands, 
                    dis_ins.info.operand_count_visible, 
                    &ins->req));

        // Set values for known metadata
        ins->length = dis_ins.info.length;
        ins->jump_target = JUMP_INVALID;
        ins->target_addrs = ADDRESS_INVALID;
        ins->address = rt_address;
        ins->idx = idx;
        idx++;

        //print_instruction(snip, ins, NULL);

        // Also remember the target address for jumps
        if (is_imm_jmp(ins))
            CHECK_ZYAN(ZydisCalcAbsoluteAddress(&dis_ins.info, &dis_ins.operands[get_jmp_op_idx(ins)], rt_address, &ins->target_addrs));
        
        // Add the new instruction_t to the snippet_t
        CHECK_INT(snippet_append(snip, ins), EGENERIC);
        
        // Move to the next instruction
        offset += dis_ins.info.length;    
        rt_address += dis_ins.info.length;

    }
    
    return 0;
}

int dump_snippet(snippet_t *snip, uint8_t *bin, size_t bin_sz, char *dir_name, bool with_text, bool is_error, int signal, uint64_t rip)
{
    char *res_env = getenv("AFL_CUSTOM_INFO_OUT");
    char template[1024];
    uint8_t buf[PAGE_SIZE*2];
    int fd, buf_len, ret;
    FILE *f;

    if (with_text && !snip)
        return -1;

    if ((!bin || !bin_sz) && !snip)
        return -1;
    
    sprintf(template, "%s/%s", res_env, dir_name);
    
    ret = mkdir(template, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (ret == -1 && errno != EEXIST)
        CHECK_LIBC(ret);
    
    if (is_error)
        sprintf(template, "%s/%s/find-%p-%s.XXXXXX", res_env, dir_name, (void *)rip, strsignal(signal));
    else
        sprintf(template, "%s/%s/find_bin.XXXXXX", res_env, dir_name);
    
    fd = mkstemp(template);
    CHECK_LIBC(fd);

    if (bin && bin_sz) {
        CHECK_LIBC(write(fd, bin, bin_sz));
    } else {
        buf_len = snippet_encode(snip, buf, PAGE_SIZE*2);
        CHECK_INT(buf_len, EGENERIC);

        CHECK_LIBC(write(fd, buf, buf_len));
    }
    
    close(fd);

    if (with_text) {
        sprintf(template, "%s/%s/find_text.XXXXXX", res_env, dir_name);

        f = fdopen(mkstemp(template), "w+");
        CHECK_PTR(f, EGENERIC);

        CHECK_LIBC(fprintf(f, ".intel_syntax noprefix\n"));
        CHECK_INT(snippet_print(snip, f, true, false), EGENERIC);

        fclose(f);
    }
    return 0;
}

