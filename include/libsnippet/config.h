#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <string.h>
#include <sys/user.h>

#include <Zydis/Zydis.h>

#include "libsnippet/common.h"

typedef struct mem_ {
    uint64_t address;
    size_t size;
} mem_t;

typedef struct generation_index_ {
    size_t iterations;
    size_t count;
    double threshold;
    ZydisMnemonic instructions[ZYDIS_MNEMONIC_MAX_VALUE+1];
} generation_index_t;

typedef struct config_t {
    int max_snippet_sz;
    
    mem_t snippet_code;
    mem_t snippet_memory;
    mem_t snippet_stack;
    size_t snippet_mem_padding;
    
    ZydisMachineMode mode;
    ZydisRegister mem_base_register;
    ZydisRegister mem_index_register;
    ZydisRegister mem_vsibx_register;
    ZydisRegister mem_vsiby_register;
    ZydisRegister mem_vsibz_register;

    generation_index_t index; // This should be at the bottom, as its quite a large allocation
} config;

// START DEFAULT CONFIG
// Snippet Execution stuff
#define MEM_ADDR            0x000000000d00000
#define MEM_SIZE            PAGE_SIZE * 2
#define MEM_PADDING         64                  // Gap in bytes to the end of the memory area, avoid OOB writes
#define CODE_ADDR           0x0000000050000000  // This sets where the code is dynamically loaded
#define CODE_SIZE           PAGE_SIZE * 2
#define STACK_SIZE          PAGE_SIZE * 4
#define STACK_ADDR          0x0000000060000000  // This sets where the stack used by the snippet is located 
// Generations stuff
#define INDEX_ITERATIONS    100     // How many generation attempts per instruction
#define INDEX_THRESHOLD     1.0     // What % of attempted generation must be correct to include an instruction
#define MEMORY_MNEMONIC     "rsi"
#define MAX_SNIPPET_SIZE    20      // (About) how large snippets should be before serialization
// Zydis stuff
#define MACHINE_MODE        ZYDIS_MACHINE_MODE_LONG_64
#define MEM_BASE_REGISTER   ZYDIS_REGISTER_RSI
#define MEM_INDEX_REGISTER  ZYDIS_REGISTER_RDI
#define MEM_VSIBX_REGISTER  ZYDIS_REGISTER_XMM0
#define MEM_VSIBY_REGISTER  ZYDIS_REGISTER_YMM0
#define MEM_VSIBZ_REGISTER  ZYDIS_REGISTER_ZMM0

#endif
