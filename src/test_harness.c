#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <fcntl.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <dirent.h>

#include <Zydis/Zydis.h>

#include "oracle.h"
#include "config.h"
#include "snippet.h"
#include "error.h"
#include "generate.h"

config cfg;
uint8_t coverage_buf[PAGE_SIZE * 64];
size_t coverage_size = PAGE_SIZE * 64;

int test_distribution(config *cfg, bool to_csv)
{
    size_t total_ins = 5000000;
    clock_t start = clock();
    FILE *f;
    ZyanStatus status;
    size_t size;
    instruction_t ins;
    ZydisDisassembledInstruction dis;
    snippet_t snip;
    uint8_t buf[PAGE_SIZE];

    if (to_csv) {
        f = fopen("random_instructions.csv", "w+");
        if (!f)
            return -1;

        fprintf(f, "instruction length|instruction text\n");
    }

    start = clock();
    
    for (int i=0; i < 1000; i++) {
        CHECK_INT(create_random_instruction(cfg, &ins), EGENERIC); 
        print_instruction(&snip, &ins, NULL);

        size = ZYDIS_MAX_INSTRUCTION_LENGTH; 
        status = ZydisEncoderEncodeInstruction(&ins.req, buf, &size);
        CHECK_ZYAN(status);

        status = ZydisDisassembleIntel(cfg->mode, 0, buf, ZYDIS_MAX_INSTRUCTION_LENGTH, &dis);
        CHECK_ZYAN(status);
        fprintf(f, "%d|%s\n", dis.info.length, dis.text);
    }

    printf("Total time taken to generate %ld instructions: %f s\n", total_ins, ((double)(clock() - start)) / CLOCKS_PER_SEC);

    if (to_csv)
        fclose(f);

    return 0;
}

int test_generation(config *cfg)
{
    //walk_instruction_defs(ZYDIS_MACHINE_MODE_LONG_64);
    
    test_generate_all(cfg, ZYDIS_MACHINE_MODE_LONG_64, true, 0);
    
    // while (1)
    //     generate_template(ZYDIS_MNEMONIC_SFENCE);
    
    return 0;
}

int test_index(config *cfg)
{
    cfg->index.iterations = 10000;
    if (create_index(cfg) < 0)
        return -1;

    printf("Created index with %ld / %d entries\n", cfg->index.count-1, ZYDIS_MNEMONIC_MAX_VALUE);
    return 0;
}

int test_measurement(config *cfg)
{
    uint8_t buf[PAGE_SIZE * 2];
    size_t len;
    double start, local_start;
    int64_t runs = 2;
    states_t states;
    const char dir[] = "corpus_extra";
    struct dirent **f_names;
    char name[1024];

    if (init_oracle(cfg) < 0)
        return -1;

    cfg->iterations = 1;
    start = clock();
    
    runs = scandir(dir, &f_names, NULL, NULL);
    CHECK_LIBC(runs);
    
    for (int i=0; i < runs; i++) {
        if (strcmp(f_names[i]->d_name, ".") == 0 ||
            strcmp(f_names[i]->d_name, "..") == 0)
            continue;

        sprintf(name, "%s/%s", dir, f_names[i]->d_name);
        len = load_code(buf, PAGE_SIZE * 2, name);
        if (len < 0)
            goto error;

        printf("Measuring: %s\n", f_names[i]->d_name);
        local_start = clock();
    
        int results = measure_snippet(cfg, buf, len, &states);
        if (results < 0) {
            printf("There was an error executing the snippet_t\n");
            return -1;
        }
        
        print_prstatus(&states.original.gpr_state, stdout);
        print_xstate((struct xregs_state *)&states.original.x_state, stdout);
        //print_prstatus(&states.serial.gpr_state, stdout);
        //print_xstate((struct xregs_state *)&states.serial.x_state, stdout);
        //printf("[%s] Time registered: %f\n", name,  (clock() - local_start) / CLOCKS_PER_SEC);

        CHECK_INT(encode_score(cfg, &states, coverage_buf, coverage_size, buf, len, false), EGENERIC);
        print_score_diff(&states, false, false);
    }
    
    if (destroy_oracle(cfg) < 0)
        return -1;

    printf("Ran %ld times, in %f sec\n", runs * cfg->iterations, (clock() - start) / CLOCKS_PER_SEC);
    return 0;
error:
    printf("Failed measurement test\n");
    return -1;
}

int test_measure_single(config *cfg)
{
    uint8_t buf[PAGE_SIZE * 2];
    size_t len;
    double start, local_start;
    int64_t runs = 2;
    states_t states;

    if (init_oracle(cfg) < 0)
        return -1;

    cfg->iterations = 1;
    start = clock();
    
    len = load_code(buf, PAGE_SIZE * 2, "extra_results/amd_hybrid_run1/oracle_finds/find_bin.NPC7PM");
    if (len < 0)
        goto error;

    local_start = clock();

    int results = measure_snippet(cfg, buf, len, &states);
    if (results < 0) {
        printf("There was an error executing the snippet_t\n");
        return -1;
    }

    oracle_ctx_t *ctx = get_ctx();
    
    printf("ORIGINAL:\n");
    snippet_print(&ctx->original, stdout, true, true);
    
    printf("SERIAL:\n");
    snippet_print(&ctx->serialized, stdout, true, true);
    print_prstatus(&states.original.gpr_state, stdout);
    print_xstate((struct xregs_state *)&states.original.x_state, stdout);
    //print_prstatus(&states.serial.gpr_state, stdout);
    //print_xstate((struct xregs_state *)&states.serial.x_state, stdout);
    //printf("[%s] Time registered: %f\n", name,  (clock() - local_start) / CLOCKS_PER_SEC);

    CHECK_INT(encode_score(cfg, &states, coverage_buf, coverage_size, buf, len, false), EGENERIC);
    print_score_diff(&states, false, false);
    
    if (destroy_oracle(cfg) < 0)
        return -1;

    printf("Ran %ld times, in %f sec\n", runs * cfg->iterations, (clock() - start) / CLOCKS_PER_SEC);
    return 0;
error:
    printf("Failed measurement test\n");
    return -1;
}

// This is essentially meant to be run using gdb
int test_execution(config *cfg)
{
    uint8_t buf[PAGE_SIZE * 2];
    size_t len;
    int status;

    if (init_oracle(cfg) < 0)
        return -1;

    oracle_ctx_t *ctx = get_ctx();
    // Clear out both snippet buffers
    CHECK_INT(snippet_free(&ctx->original), EGENERIC);
    CHECK_INT(snippet_free(&ctx->serialized), EGENERIC);
    len = load_code(buf, PAGE_SIZE * 2, "results/test_5/oracle_finds/find_bin.5SH90Y");
    if (len < 0)
        goto error;
    
    // Decode the original snippet
    CHECK_INT(snippet_decode(&ctx->original, cfg->snippet_code.address, buf, len), EGENERIC);
    CHECK_INT(pipeline_decode(&ctx->original), EGENERIC);
    
    // Insert nops inbetween operations
    CHECK_INT(serialize_snippet(cfg, &ctx->original, &ctx->serialized), EGENERIC);

    CHECK_INT(load_snippet(cfg, &ctx->serialized), ESNIPLOAD);

    CHECK_INT(fork_snippet(cfg), EGENERIC);
    
    // Should wait here for the process to die, it wont ever technically
    CHECK_LIBC(waitpid(ctx->fork_pid, &status, __WALL));
    
    print_wait_status(status);

    CHECK_INT(destroy_oracle(cfg), EGENERIC);

    return 0;
error:
    printf("Failed measurement test\n");
    return -1;
}

int test_processing(config *cfg)
{
    uint8_t buf[PAGE_SIZE * 2];
    size_t len;

    if (init_oracle(cfg) < 0)
        return -1;

    oracle_ctx_t *ctx = get_ctx();
    // Clear out both snippet buffers
    CHECK_INT(snippet_free(&ctx->original), EGENERIC);
    CHECK_INT(snippet_free(&ctx->serialized), EGENERIC);

    len = load_code(buf, PAGE_SIZE * 2, "extra_results/amd_hybrid_run1/oracle_finds/find_bin.NPC7PM");
    if (len < 0)
        goto error;
    
    // Decode the original snippet
    CHECK_INT(snippet_decode(&ctx->original, cfg->snippet_code.address, buf, len), EGENERIC);
    CHECK_INT(pipeline_decode(&ctx->original), EGENERIC);
    CHECK_INT(pipeline_encode(&ctx->original), EGENERIC);

    CHECK_INT(serialize_snippet(cfg, &ctx->original, &ctx->serialized), EGENERIC);
    CHECK_INT(pipeline_encode(&ctx->serialized), EGENERIC);

    //CHECK_INT(pipeline_validate(&ctx->original), EGENERIC);
    printf("ORIGINAL:\n");
    snippet_print(&ctx->original, stdout, true, true);
    
    printf("SERIAL:\n");
    snippet_print(&ctx->serialized, stdout, true, true);

    return 0;
error:
    printf("Failed measurement test\n");
    return -1;
}

int walk_defs(config *cfg)
{
    FILE *f = fopen("analysis_data/all_defs.csv", "w+");
    
    fprintf(f, "encoding,mnemonic,category\n");

    walk_instruction_defs(ZYDIS_MACHINE_MODE_LONG_64, f);

    fclose(f);

    return 0;
}

int main(int argc, char **argv) 
{
    default_config(&cfg);
    
    if(set_affinity(cfg.core_1) < 0)
        return EXIT_FAILURE;
    
    srandom(1);

    // if (test_distribution(&cfg, true) != 0)
    //     goto error;
    //
    if (test_measurement(&cfg) != 0)
        goto error;

    // if (test_execution(&cfg) != 0)
    //     goto error;

    // if (test_generation(&cfg) != 0)
    //     goto error;
    //
    // if (test_index(&cfg) != 0)
    //     goto error;
    //
    // if (walk_defs(&cfg) != 0)
    //     goto error;
    //
    // if (test_processing(&cfg) != 0)
    //     goto error;
    //
    // if (test_measure_single(&cfg) != 0)
    //     goto error;

    return 0;

error:
    printf("Failed test run\n");
    return -1;
}
