#include <stdio.h>
#include <stdbool.h>
#include <sys/user.h>

#include "snippet.h"
#include "config.h"
#include "error.h"
#include "generate.h"

generate_method_t get_method(const char *method_str)
{
    // Valid methods 
    switch (method_str[0]) {
    case 'g':
        return METHOD_GENERATE;
    case 'c':
        return METHOD_CREATE;
    case 'm':
        return METHOD_MIXED;
    default:
        return METHOD_NONE;
    }
}

int create_corpus(config *cfg, generate_method_t method, bool print, bool save_text)
{
    snippet_t snip;
    uint64_t snip_sz;
    char file_name[0xff];
    char text_name[0xff];
    uint8_t encode_buf[PAGE_SIZE * 2]; // TODO: Maybe make this larger?
    FILE *bin_f, *text_f;
    
    CHECK_INT(snippet_init(cfg, &snip), EGENERIC);

    for (int i=0; i < cfg->start_corpus_size; i++) {
        CHECK_INT(snippet_free(&snip), EGENERIC);
        CHECK_INT(create_snippet(cfg, &snip, cfg->max_snippet_sz, method), EGENERIC);

        if (print) {
            printf("Snippet #%d:\n", i);
            CHECK_INT(snippet_print(&snip, stdout, true, true), EGENERIC);
        }

        // Encode the snippet into a buffer
        CHECK_INT(snippet_encode(&snip, encode_buf, PAGE_SIZE * 2), EGENERIC);

        // Give the new entries names
        CHECK_LIBC(snprintf(file_name, 0xff, "corpus/snippet_%d", i));
        CHECK_LIBC(snprintf(text_name, 0xff, "corpus_files/snippet_%d.s", i));
        
        // Save a human readable copy if needed
        if (save_text) {
            text_f = fopen(text_name, "w+");
            CHECK_PTR(text_f, EGENERIC);

            CHECK_LIBC(fprintf(text_f, ".intel_syntax noprefix\n"));
            CHECK_INT(snippet_print(&snip, text_f, true, false), EGENERIC);
            fclose(text_f);
        }
        
        bin_f = fopen(file_name, "w+");
        CHECK_PTR(bin_f, EGENERIC);
        
        // Write the binary into the new file
        instruction_t *last = snippet_get(&snip, snip.count - 1);
        snip_sz = (last->address + last->length) - snip.start_address;
        CHECK_LIBC(fwrite(encode_buf, snip_sz, 1, bin_f));

        fclose(bin_f);
    }

    CHECK_INT(snippet_destroy(&snip), EGENERIC);
    return 0;
}

int main(int argc, char **argv)
{
    bool print = false;
    bool save_text = false;
    generate_method_t method = METHOD_GENERATE;
    char *config_dir = NULL;
    uint32_t seed = 420;
    int ret;
    config cfg;

    while ((ret = getopt(argc, argv, "ptc:m:s:")) != -1) {
        switch(ret) {
        case 'p':
            print = true;
            break;
        case 'c':
            config_dir = optarg;
            break;
        case 'm':
           method = get_method(optarg);
           break;
        case 's':
           seed = strtoul(optarg, NULL, 10);
           break;
        case 't':
            save_text = true;
            break;
        default:
            fprintf(stderr, "Usage: %s [-s rng_seed] [-p] [-t] [-c config_dir] [-m generate|create]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }
    
    if (method == METHOD_NONE) {
        fprintf(stderr, "Expected valid instruction generation method\n");
        exit(EXIT_FAILURE);
    }

    if (config_dir)
        import_config(&cfg, config_dir);
    else
        default_config(&cfg);
    
    CHECK_INT(create_index(&cfg), EGENERIC);
    
    if (print)
        printf("Using seed %d\n", seed);

    srandom(seed);

    CHECK_INT(create_corpus(&cfg, method, print, save_text), EGENERIC);

    return EXIT_SUCCESS;
}
