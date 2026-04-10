#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

#include "config.h"
#include "oracle.h"
#include "error.h"
#include "score.h"

config cfg;
uint8_t coverage_buf[PAGE_SIZE * 64];
size_t coverage_size = PAGE_SIZE * 64;
states_t states;

int validate(config *cfg, char *code_file, bool print_snip, size_t attempts)
{
    uint8_t buf[PAGE_SIZE * 2];
    size_t len;
    int ret;
    oracle_ctx_t *oracle;

    CHECK_INT(init_oracle(cfg), EGENERIC);
    oracle = get_ctx();

    cfg->iterations = 1;
        
    len = load_code(buf, PAGE_SIZE * 2, code_file);
    if (len < 0)
        return -1;
    
    for (size_t i=0; i < attempts; i++) {
        ret = measure_snippet(cfg, buf, len, &states);
        if (ret < 0) {
            printf("There was an error executing the snippet_t\n");
            return -1;
        }

        ret = encode_score(cfg, &states, coverage_buf, coverage_size, buf, len, false);
        CHECK_INT(ret, EGENERIC);
        
        print_score_diff(&states, false, false);
        
        if (!ret)
            continue;

        if (print_snip) {
            printf("Difference on run %ld\n", i);

            snippet_print(&oracle->original, stdout, true, true);
            print_wait_status(states.original.final_sig);
            printf("Final rip @ %p\n", (void *)states.original.final_rip);

            snippet_print(&oracle->serialized, stdout, true, true);
            print_wait_status(states.serial.final_sig);
            printf("Final rip @ %p\n", (void *)states.serial.final_rip);

            // print_events(states.original.perf, cfg->papi_event_codes, cfg->num_papi_events, stdout);
            print_prstatus(&states.original.gpr_state, stdout);
            print_xstate((struct xregs_state *)&states.original.x_state, stdout);
            
            // print_events(states.serial.perf, cfg->papi_event_codes, cfg->num_papi_events, stdout);
            print_prstatus(&states.serial.gpr_state, stdout);
            print_xstate((struct xregs_state *)&states.serial.x_state, stdout);
        }
        
        break;
    }

    if (destroy_oracle(cfg) < 0)
        return -1;

    return 0;
}

int main(int argc, char *argv[])
{
    bool print_snip = false;
    char *config_file = NULL;
    char *code_file = NULL;
    size_t attempts = 1;
    config cfg;
    int ret;

    while ((ret = getopt(argc, argv, "pc:a:")) != -1) {
        switch(ret) {
        case 'p':
            print_snip = true;
            break;
        case 'c':
            config_file = optarg;
            break;
        case 'a':
            attempts = strtoul(optarg, NULL, 10);
	    break;
        default:
            fprintf(stderr, "Usage: %s [-p] [-c config_dir] [-a num attempts] snippet_binary\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Expected binary file after arguments\n");
        exit(EXIT_FAILURE);
    }

    code_file = argv[optind];
    
    // This also sets all parameters that might not be set in the desired config
    default_config(&cfg);
    
    if (config_file != NULL 
        && import_config(&cfg, config_file) < 0) {

        fprintf(stderr, "The provided config file was not valid\n");
        exit(EXIT_FAILURE);
    }

    // Configured for 1 Mil entries
    if (bloom_init(&cfg.finds, BLOOM_SZ, BLOOM_HSH) < 0)
        return EXIT_FAILURE;

    if (set_affinity(cfg.core_1 + 1) < 0)
        return EXIT_FAILURE;

    if (validate(&cfg, code_file, print_snip, attempts) < 0)
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
