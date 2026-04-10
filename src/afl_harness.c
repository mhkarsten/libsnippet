#include <stdio.h>
#include <sys/user.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <signal.h>
#include <sched.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/mman.h>

#include "common.h"
#include "generate.h"
#include "config.h"
#include "oracle.h"
#include "error.h"
#include "score.h"

// Defines needed for AFL
#define FS_VERSION              0x41464c00 + 1
#define FS_OPT_MAX_MAPSIZE      ((0x00fffffeU >> 1) + 1)
#define FS_OPT_ERROR            0xf800008f
#define FS_OPT_GET_ERROR(x)     ((x & 0x00ffff00) >> 8)
#define FS_OPT_SET_ERROR(x)     ((x & 0x0000ffff) << 8)
#define FS_ERROR_MAP_SIZE       1
#define FS_ERROR_SHMAT          8
#define FS_NEW_OPT_MAPSIZE      0x00000001      // parameter: 32 bit value
#define FS_NEW_OPT_SHDMEM_FUZZ  0x00000002  // parameter: none
#define FORKSRV_FD              198
#define MAP_SIZE                65536
#define SHM_ENV_VAR             "__AFL_SHM_ID"            // Shared memory needed for returning score (coverage)
#define SHM_FUZZ_ENV_VAR        "__AFL_SHM_FUZZ_ID"    // Shared memory needed for returning fuzzing cases

// AFL book keeping
// This is more for when there is actual instrumentation
uint8_t __afl_dummy_area[MAP_SIZE];

// shm for code coverage, used here for encoding the score
uint8_t *__afl_area_ptr = __afl_dummy_area;
uint8_t *__afl_backup_ptr = __afl_dummy_area;
__thread uint32_t __afl_map_size = MAP_SIZE;

// shm for receiveing test cases, could maybe minimize further by sending directly
uint8_t *__afl_fuzz_ptr;
__thread uint32_t *__afl_fuzz_len;
static pid_t child_pid = -1;

// Non AFL book keeping
static config cfg = {0};
static size_t *total_finds;
static states_t states = {0};       // This is where the score is kept

static void at_exit(int signal)
{
    if (child_pid > 0) {
        kill(child_pid, SIGTERM);
        waitpid(child_pid, NULL, 0);
        
        child_pid = -1;
    }
    
    exit(0);
}

// Any other threads must be stopped, otherwise rouge processes
static void child_exit(int signal)
{
    if (reap_snippet(&cfg) < 0)
        exit(EXIT_FAILURE);
    
    destroy_oracle(&cfg);
    exit(EXIT_SUCCESS);
}

/* Error reporting to forkserver controller */
static void send_forkserver_error(int error) 
{
    uint32_t status;
    if (!error || error > 0xffff) return;
    status = (FS_OPT_ERROR | FS_OPT_SET_ERROR(error));
    if (write(FORKSRV_FD + 1, (char *)&status, 4) != 4) return;
}

static void __afl_map_shm(void)
{
    char *id_str_cov = getenv(SHM_ENV_VAR);
    char *id_str_fuz = getenv(SHM_FUZZ_ENV_VAR);
    uint32_t shm_id;

    if (id_str_cov) {
        shm_id = atoi(id_str_cov);
        __afl_area_ptr = shmat(shm_id, NULL, 0);
        __afl_backup_ptr = __afl_area_ptr;

        if (__afl_area_ptr == (void *)-1) {
            send_forkserver_error(FS_ERROR_SHMAT);
            exit(1);
        }
        
        /* Write something into the bitmap so that the parent doesn't give up */
        __afl_area_ptr[0] = 1;
    }

    if (id_str_fuz) {
        shm_id = atoi(id_str_fuz);
        __afl_fuzz_ptr = shmat(shm_id, NULL, 0);

        // The length is stored right at the start of this area
        __afl_fuzz_len = (uint32_t *)__afl_fuzz_ptr;
        __afl_fuzz_ptr = __afl_fuzz_ptr + sizeof(uint32_t);

        if (__afl_fuzz_ptr == (void *)-1) {
            send_forkserver_error(FS_ERROR_SHMAT);
            exit(1);
        }   
    }
}

// TODO: Refactor this to be less ugly
static void __afl_start_forkserver(void)
{
    uint32_t was_killed = 0;
    uint32_t child_stopped = 0;
    uint32_t status, status2;
    uint8_t *msg = (uint8_t *) &status;
    uint8_t *reply = (uint8_t *) &status2;
    int wait_status;
    
    // Save the original sigterm handling
    struct sigaction orig_action;
    sigaction(SIGTERM, NULL, &orig_action);

    signal(SIGTERM, at_exit);

    // Share initial version info with afl
    status = FS_VERSION;
    if (write(FORKSRV_FD + 1, msg, 4) != 4) { exit(-1); }

    // Read and check reply from afl
    if (read(FORKSRV_FD, reply, 4) != 4) { exit(1); }
    if (status2 != (FS_VERSION ^ 0xffffffff)) { exit(1); }

    // Share what things we will be using, namely map size and shared memory
    status = FS_NEW_OPT_MAPSIZE | FS_NEW_OPT_SHDMEM_FUZZ;
    if (write(FORKSRV_FD + 1, msg, 4) != 4) { exit(-2); }

    // Share the map size we are using, could define less as well
    status = __afl_map_size;
    if (write(FORKSRV_FD + 1, msg, 4) != 4) { exit(-3); }
    
    // Share version again to end the handshake
    status = 0x41464c00 + 1;
    if (write(FORKSRV_FD + 1, msg, 4) != 4) { exit(-1); }
    
    // HANDSHAKE COMPLETE
    
    // Map in all the shared memory
    __afl_map_shm();

    // Map in our own shared memory
    total_finds = mmap(NULL, 
                      sizeof(size_t), 
                      MMAP_PROT,
                      MAP_SHARED | MAP_ANONYMOUS | MAP_POPULATE,
                      -1,
                      0);
    
    *total_finds = 0;
    
    // This is the main forkserver implementation
    while (1) {
        // Wait until parent is ready
        if (read(FORKSRV_FD, &was_killed, 4) != 4) { exit(1); }
        
        // Handle edge case where child was killed and it was stopped
        if (child_stopped && was_killed) {
            child_stopped = 0;
            if (waitpid(child_pid, &wait_status, 0) < 0) { exit(1); }
        }
        
        if (!child_stopped) {
            child_pid = fork();
            if (child_pid < 0) { exit(1); }
            
            // Here we are the child. Perform persistent looping
            if (child_pid == 0) {
                // Restore signals here
                signal(SIGTERM, orig_action.sa_handler);

                close(FORKSRV_FD);
                close(FORKSRV_FD + 1);
                return; // Go back to child code
            }
        } else {
            kill(child_pid, SIGCONT);
            child_stopped = 0;
        }
        
        // Inform afl of the newly created child
        if (write(FORKSRV_FD + 1, &child_pid, 4) != 4) { exit(1); }
        
        // Wait for the child to terminate or stop
        if (waitpid(child_pid, &wait_status, WUNTRACED) < 0) { exit(1); }

        if (WIFSTOPPED(wait_status)) { child_stopped = 1; }
        
        // Initiate graceful shutdown by signaling SIGABRT
        if (*total_finds >= MAX_FINDS)
            wait_status = 0x80 | SIGABRT;

        // Give afl the status of the child
        if (write(FORKSRV_FD + 1, &wait_status, 4) != 4) { exit(1); }
    }
}

static int __afl_persistent_loop(uint32_t max_cnt)
{
    static uint8_t first_pass = 1;
    static uint32_t cur_cnt;

    if (first_pass) {
        first_pass = 0;
        memset(__afl_area_ptr, 0, __afl_map_size);
        __afl_area_ptr[0] = 1;
        
        cur_cnt = max_cnt;
        __afl_area_ptr = __afl_backup_ptr;
        return 1;
    } else if (cur_cnt--) {
        raise(SIGSTOP);
            
        // Reset some memory things, then move to the next run
        memset(__afl_area_ptr, 0, __afl_map_size);
        __afl_area_ptr[0] = 1;
        return 1;
    } else {
        __afl_area_ptr = __afl_dummy_area;
        return 0;
    }
}

// Implement a forkserver here
int main(int argc, char** argv, char **envp) 
{
    int new_find;
    default_config(&cfg);
    
    if (create_index(&cfg) < 0)
        return 77;
    
    // Configured for 1 Mil entries
    if (bloom_init(&cfg.finds, BLOOM_SZ, BLOOM_HSH) < 0)
        return 79;

    // Intialize the oracle (memory regions ect.)
    if (init_oracle(&cfg) < 0)
        return 75;

    //srandom(1); // TODO: Handle seeds
    export_config(&cfg);

    // Register child signal handler for graceful shutdown
    signal(SIGTERM, &child_exit);

    // Start the forkserver here
    __afl_start_forkserver();
    
    uint8_t *buf = __afl_fuzz_ptr;
    uint32_t *len = __afl_fuzz_len;

    CHECK_INT(set_affinity(cfg.core_1 + 1), EGENERIC);
    
    // Persistent afl loop, run 100000 times before forking again
    while (__afl_persistent_loop(100000)) {
        /* Reset coverage map state */
        memset(__afl_area_ptr+1, 0, __afl_map_size-1);

        CHECK_INT(measure_snippet(&cfg, buf, *len, &states), ESNIPEXEC);

        new_find = encode_score(&cfg, &states, __afl_area_ptr+1, __afl_map_size-1, buf, *len, true);
        CHECK_INT(new_find, EGENERIC);
        if (!new_find)
            continue;

        *total_finds += new_find; // new find can only be 1 in this implementation
        
        if (*total_finds > MAX_FINDS)
            break;
    }

    if (reap_snippet(&cfg) < 0)
        return 76;

    // This only exits the child process, which goes back to the main loop
    exit(EXIT_SUCCESS);
}
