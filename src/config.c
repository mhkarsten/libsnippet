#include <string.h>
#include <ctype.h>
#include <stdint.h>

#include "common.h"
#include "config.h"
#include "debug_strings.h"

char *cfg_strings[] = {
    [CFG_ITERATIONS]        = "Num Iterations",
    [CFG_MAX_SNIPPET_SZ]    = "Max Snippet Size",
    [CFG_CORE_1]            = "Core 1",
    [CFG_MAX_EXEC_TIME]     = "Max Exec Time",
    [CFG_SNIPPET_MEM_ADDR]  = "Snippet Memory Address",
    [CFG_SNIPPET_MEM_SZ]    = "Snippet Memory Size",
    [CFG_SNIPPET_CODE_ADDR] = "Snippet Code Address",
    [CFG_SNIPPET_CODE_SZ]   = "Snippet Code Size",
    [CFG_SNIPPET_STACK_ADDR] = "Snippet Stack Address",
    [CFG_SNIPPET_STACK_SZ]   = "Snippet Stack Size",
    [CFG_MEM_BASE_REGISTER] = "Memory Base Register",
    [CFG_MEM_INDEX_REGISTER] = "Memory Index Register",
    [CFG_MEM_VSIBX_REGISTER] = "Memory Index xmm register",
    [CFG_MEM_VSIBY_REGISTER] = "Memory Index ymm register",
    [CFG_MEM_VSIBZ_REGISTER] = "Memory Index zmm register",
    [CFG_MEM_PADDING]       = "Snippet Memory Padding",
    [CFG_NUM_EVENTS]        = "Number of Papi Counters",
    [CFG_MACHINE_MODE]      = "Machine Mode",
    [CFG_START_CORPUS_SIZE] = "Starting Afl Corpus Size",
    [CFG_INDEX_THRESHOLD]   = "Index Success Threshold",
    [CFG_INDEX_ITERATIONS]  = "Index Validation Iterations",
    [CFG_IGNORE_SEGMENT]    = "Ignore Segment Registers",
    [CFG_ENCODE_METHOD]     = "Score Encoding Method",
    [CFG_FUZZ_METHOD]       = "Fuzzing Snippet Generation"
};

char *trim(char *str)
{
    size_t start = 0;
    size_t end = strlen(str) - 1;
    
    while (str[start] && isspace(str[start]))
        start++;
    
    while(str[end] && isspace(str[end]))
        end--;
    
    // Null terminate
    str[end+1] = 0;

    // Move to start
    end = strlen(str + start);
    memmove(str, (str + start), end);
    
    // Null terminate again, as the string could have moved
    str[end] = 0;

    return str;
}

int string_to_enum(char *str, char **str_arr, size_t elem_count)
{
    for (size_t i=0; i < elem_count; i++) {
        if (strcmp(str, str_arr[i]) == 0)
            return i;
    }   

    return -1;
}

int export_config(config *cfg)
{
    char *res_env = getenv("AFL_CUSTOM_INFO_OUT");
    
    int fname_len = strlen(res_env) + strlen(CONFIG_FNAME) + 2;
    char *fname = (char *)malloc(fname_len);

    if (!res_env) {
        printf("No res dir specified\n");
        return -1;
    }

    snprintf(fname, fname_len, "%s/%s", res_env, CONFIG_FNAME);

    FILE *f = fopen(fname, "w");
    
    if (!f) {
        free(fname);
        printf("Could not open fuzzer config file");
        return -1;
    }

    fprintf(f, "%-20s:\t%d\n", cfg_strings[CFG_ITERATIONS], cfg->iterations);
    fprintf(f, "%-20s:\t%d\n", cfg_strings[CFG_MAX_SNIPPET_SZ], cfg->max_snippet_sz);
    fprintf(f, "%-20s:\t%d\n", cfg_strings[CFG_CORE_1], cfg->core_1);
    fprintf(f, "%-20s:\t%d\n", cfg_strings[CFG_MAX_EXEC_TIME], cfg->max_exec_time);
    fprintf(f, "%-20s:\t%lx\n", cfg_strings[CFG_SNIPPET_MEM_ADDR], cfg->snippet_memory.address);
    fprintf(f, "%-20s:\t%lu\n", cfg_strings[CFG_SNIPPET_MEM_SZ], cfg->snippet_memory.size);
    fprintf(f, "%-20s:\t%lx\n", cfg_strings[CFG_SNIPPET_CODE_ADDR], cfg->snippet_code.address);
    fprintf(f, "%-20s:\t%lu\n", cfg_strings[CFG_SNIPPET_CODE_SZ], cfg->snippet_code.size);
    fprintf(f, "%-20s:\t%lx\n", cfg_strings[CFG_SNIPPET_STACK_ADDR], cfg->snippet_stack.address);
    fprintf(f, "%-20s:\t%lu\n", cfg_strings[CFG_SNIPPET_STACK_SZ], cfg->snippet_stack.size);
    fprintf(f, "%-20s:\t%ld\n", cfg_strings[CFG_MEM_PADDING], cfg->snippet_mem_padding);
    fprintf(f, "%-20s:\t%u (%s)\n", cfg_strings[CFG_MEM_BASE_REGISTER], cfg->mem_base_register, ZydisRegisterGetString(cfg->mem_base_register));
    fprintf(f, "%-20s:\t%u (%s)\n", cfg_strings[CFG_MEM_INDEX_REGISTER], cfg->mem_index_register, ZydisRegisterGetString(cfg->mem_index_register));
    fprintf(f, "%-20s:\t%u (%s)\n", cfg_strings[CFG_MEM_VSIBX_REGISTER], cfg->mem_vsibx_register, ZydisRegisterGetString(cfg->mem_vsibx_register));
    fprintf(f, "%-20s:\t%u (%s)\n", cfg_strings[CFG_MEM_VSIBY_REGISTER], cfg->mem_vsiby_register, ZydisRegisterGetString(cfg->mem_vsiby_register));
    fprintf(f, "%-20s:\t%u (%s)\n", cfg_strings[CFG_MEM_VSIBZ_REGISTER], cfg->mem_vsibz_register, ZydisRegisterGetString(cfg->mem_vsibz_register));
    fprintf(f, "%-20s:\t%d\n", cfg_strings[CFG_NUM_EVENTS], cfg->num_papi_events);
    fprintf(f, "%-20s:\t%f\n", cfg_strings[CFG_INDEX_THRESHOLD], cfg->index.threshold);
    fprintf(f, "%-20s:\t%ld\n", cfg_strings[CFG_INDEX_ITERATIONS], cfg->index.iterations);
    fprintf(f, "%-20s:\t%u (%s)\n", cfg_strings[CFG_MACHINE_MODE], cfg->mode, zydis_machine_mode_strings[cfg->mode]);
    fprintf(f, "%-20s:\t%d\n", cfg_strings[CFG_IGNORE_SEGMENT], cfg->ignore_segment);
    fprintf(f, "%-20s:\t%u (%s)\n", cfg_strings[CFG_FUZZ_METHOD], cfg->fuzz_method, fuzz_method_strings[cfg->fuzz_method]);
    fprintf(f, "%-20s:\t%u (%s)\n", cfg_strings[CFG_ENCODE_METHOD], cfg->encode_method, encode_method_strings[cfg->encode_method]);

    fclose(f);
    free(fname);

    return 0;
}

int default_config(config *cfg)
{
    memset(cfg, 0, sizeof(config));

    cfg->iterations = ITERATIONS;
    cfg->max_snippet_sz = MAX_SNIPPET_SIZE;
    cfg->core_1 = CORE_1;
    cfg->max_exec_time = MAX_EXEC_TIME;
    cfg->start_corpus_size = START_CORPUS_SIZE;
    cfg->snippet_mem_padding = MEM_PADDING;
    cfg->num_papi_events = 0;
    cfg->ignore_segment = IGNORE_SEGMENT;
    cfg->fuzz_method = FUZZ_METHOD;
    cfg->encode_method = ENCODE_METHOD;
    memset(cfg->papi_event_codes, 0, sizeof(cfg->papi_event_codes));

    cfg->mode = MACHINE_MODE;
    cfg->mem_base_register = MEM_BASE_REGISTER;
    cfg->mem_index_register = MEM_INDEX_REGISTER;
    cfg->mem_vsibx_register = MEM_VSIBX_REGISTER;
    cfg->mem_vsiby_register = MEM_VSIBY_REGISTER;
    cfg->mem_vsibz_register = MEM_VSIBZ_REGISTER;

    cfg->snippet_memory.address = MEM_ADDR;
    cfg->snippet_memory.size = MEM_SIZE;
    cfg->snippet_code.address = CODE_ADDR;
    cfg->snippet_code.size = CODE_SIZE;
    cfg->snippet_stack.address = STACK_ADDR;
    cfg->snippet_stack.size = STACK_SIZE;

    cfg->index.iterations = INDEX_ITERATIONS;
    cfg->index.threshold = INDEX_THRESHOLD;
    cfg->index.count = 0;

    return 0;
}

int import_config(config *cfg, char *cfg_path)
{
    FILE *f = fopen(cfg_path, "r");
    if (f == NULL)
        return -1;

    char *line = malloc(PAGE_SIZE);

    while (fgets(line, PAGE_SIZE, f) != NULL) {
        char *name = trim(strtok(line, ":"));
        char *value = trim(strtok(NULL, ":"));

        cfg_element elm = (cfg_element) string_to_enum(line, cfg_strings, CFG_COUNT);
        
        //printf("Found: %s, %s\n", cfg_strings[elm], value);
        
        // TODO: Parse PAPI saved event codes

        switch (elm) {
        case CFG_ITERATIONS:
            cfg->iterations = atoi(value);
            break;
        case CFG_MAX_SNIPPET_SZ:
            cfg->max_snippet_sz = atoi(value);
            break;
        case CFG_CORE_1:
            cfg->core_1 = atoi(value);
            break;
        case CFG_MAX_EXEC_TIME:
            cfg->max_exec_time = atoi(value);
            break;
        case CFG_SNIPPET_MEM_ADDR:
            cfg->snippet_memory.address = strtoul(value, NULL, 16);
            break;
        case CFG_SNIPPET_MEM_SZ:
            cfg->snippet_memory.size = strtoul(value, NULL, 10);
            break;
        case CFG_SNIPPET_CODE_ADDR:
            cfg->snippet_code.address = strtoul(value, NULL, 16);
            break;
        case CFG_SNIPPET_CODE_SZ:
            cfg->snippet_code.size = strtoul(value, NULL, 10);
            break;
        case CFG_SNIPPET_STACK_ADDR:
            cfg->snippet_stack.address = strtoul(value, NULL, 16);
            break;
        case CFG_SNIPPET_STACK_SZ:
            cfg->snippet_stack.size = strtoul(value, NULL, 10);
            break;
        case CFG_MEM_BASE_REGISTER:
            strchr(value, ' ')[0] = 0;
            cfg->mem_base_register = (ZydisRegister) atoi(value);
            break;
        case CFG_MEM_INDEX_REGISTER:
            strchr(value, ' ')[0] = 0;
            cfg->mem_index_register = (ZydisRegister) atoi(value);
            break;
        case CFG_MEM_VSIBX_REGISTER:
            strchr(value, ' ')[0] = 0;
            cfg->mem_vsibx_register = (ZydisRegister) atoi(value);
            break;
        case CFG_MEM_VSIBY_REGISTER:
            strchr(value, ' ')[0] = 0;
            cfg->mem_vsiby_register = (ZydisRegister) atoi(value);
            break;
        case CFG_MEM_VSIBZ_REGISTER:
            strchr(value, ' ')[0] = 0;
            cfg->mem_vsibz_register = (ZydisRegister) atoi(value);
            break;
        case CFG_MEM_PADDING:
            cfg->snippet_mem_padding = strtoul(value, NULL, 10);
            break;
        case CFG_NUM_EVENTS:
            cfg->num_papi_events = strtoul(value, NULL, 10);
            break;
        case CFG_MACHINE_MODE:
            strchr(value, ' ')[0] = 0;
            cfg->mode = (ZydisMachineMode) atoi(value);
            break;
        case CFG_START_CORPUS_SIZE:
            cfg->start_corpus_size = atoi(value);
            break;
        case CFG_INDEX_THRESHOLD:
            cfg->index.threshold = strtod(value, NULL);
            break;
        case CFG_INDEX_ITERATIONS:
            cfg->index.iterations = strtoul(value, NULL, 10);
            break;
        case CFG_IGNORE_SEGMENT:
            cfg->ignore_segment = (bool)atoi(value);
            break;
        case CFG_ENCODE_METHOD:
            strchr(value, ' ')[0] = 0;
            cfg->encode_method = (encode_method_t) atoi(value);
            break;
        case CFG_FUZZ_METHOD:
            strchr(value, ' ')[0] = 0;
            cfg->fuzz_method = (fuzz_method_t) atoi(value);
            break;
        case CFG_COUNT:
        default:
            printf("Unknown config paramter %s : %s\n", name, value);
            free(line);
            return -1;
        }
    }
    
    free(line);
    return 0;
}

