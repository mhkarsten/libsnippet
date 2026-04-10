#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <string.h>
#include <sys/user.h>

#include <Zydis/Zydis.h>

#include "bloom.h"
#include "common.h"

typedef enum cfg_element_t {
    CFG_ITERATIONS,
    CFG_MAX_SNIPPET_SZ,
    CFG_CORE_1,
    CFG_MAX_EXEC_TIME,
    CFG_SNIPPET_MEM_ADDR,
    CFG_SNIPPET_MEM_SZ,
    CFG_SNIPPET_STACK_ADDR,
    CFG_SNIPPET_STACK_SZ,
    CFG_MEM_PADDING,
    CFG_SNIPPET_CODE_ADDR,
    CFG_SNIPPET_CODE_SZ,
    CFG_MEM_INDEX_REGISTER,
    CFG_MEM_VSIBX_REGISTER,
    CFG_MEM_VSIBY_REGISTER,
    CFG_MEM_VSIBZ_REGISTER,
    CFG_MEM_BASE_REGISTER,
    CFG_NUM_EVENTS,
    CFG_MACHINE_MODE,
    CFG_START_CORPUS_SIZE,
    CFG_INDEX_ITERATIONS,
    CFG_INDEX_THRESHOLD,
    CFG_IGNORE_SEGMENT,
    CFG_FUZZ_METHOD,
    CFG_ENCODE_METHOD,
    CFG_COUNT
} cfg_element;

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

typedef enum fuzz_method_ {
    FUZZ_GENERATIVE,
    FUZZ_MUTATIVE,
    FUZZ_HYBRID,
} fuzz_method_t;

typedef enum encode_method_ {
    NORM_DIRECT,
    NORM_64_BYTE,
    NORM_32_BYTE,
    NORM_16_BYTE,
    NORM_8_BYTE,
} encode_method_t;

typedef struct config_t {
    int iterations;
    int core_1;
    int papi_event_codes[PAPI_MAX_EVENTS];
    int num_papi_events;
    int max_snippet_sz;
    int max_exec_time;
    int start_corpus_size;
    bool ignore_segment;
    fuzz_method_t fuzz_method;
    encode_method_t encode_method;

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
    bloom_t finds;
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

// Oracle / Measurement stuff
#define ITERATIONS          1 // KEEP THIS AT 1, LARGER VALUES DO NOTHING
#define MAX_EXEC_TIME       1000
#define CORE_1              1
#define MAX_FINDS           1000
#define IGNORE_SEGMENT      false

// Experiment stuff
#define START_CORPUS_SIZE   10
#define FUZZ_METHOD         FUZZ_GENERATIVE
#define ENCODE_METHOD       NORM_8_BYTE

// END DEFAULT CONFIG

#define CONFIG_FNAME            "oracle_config.txt"

int export_config(config *cfg);
int default_config(config *cfg);
int import_config(config *cfg, char *config_dir);
int string_to_enum(char *str, char **str_arr, size_t elem_count);

#endif
