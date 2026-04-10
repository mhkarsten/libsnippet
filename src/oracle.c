#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <setjmp.h>
#include <time.h>
#include <poll.h>
#include <semaphore.h>
#include <immintrin.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <linux/elf.h>
#include <linux/perf_event.h>
#include <dlfcn.h>
#include <linux/sched.h>
#include <fcntl.h>

#include <Zydis/Zydis.h>
#include <papi.h>

#include "oracle.h"
#include "config.h"
#include "snippet.h"
#include "common.h"
#include "error.h"
#include "pipeline.h"
#include "generate.h"
#include "score.h"
#include "xstate.h"

#define PAPI_NUM_STATS 10

extern void signal_return(void);
extern void target_return(void);

//ZydisMnemonic nop_instructions[] = {ZYDIS_MNEMONIC_LFENCE, ZYDIS_MNEMONIC_MFENCE, ZYDIS_MNEMONIC_SFENCE, ZYDIS_MNEMONIC_NOP};
ZydisMnemonic nop_instructions[] = {ZYDIS_MNEMONIC_MFENCE};

// This list should be in order of prority, as there are less counters available than stats
int papi_stats[] = {
    PAPI_L1_LDM,
    PAPI_L1_STM,
    PAPI_BR_MSP,
    PAPI_BR_CN,
    PAPI_VEC_DP,
    PAPI_DP_OPS, 
    PAPI_BR_PRC,
    PAPI_BR_UCN,
    PAPI_VEC_SP,
    PAPI_SP_OPS,
    PAPI_L3_LDM,
    PAPI_L2_LDM,
    PAPI_L2_STM,
    PAPI_TLB_DM,
    PAPI_TLB_IM,
    PAPI_LD_INS,
    PAPI_SR_INS,
    PAPI_L1_DCM,
    PAPI_L2_DCM,
    PAPI_BR_TKN,  
    PAPI_BR_NTK,  
    PAPI_L2_TCA,
    PAPI_L3_TCA,
    PAPI_L2_TCR,  
    PAPI_L3_TCR,  
    PAPI_L2_TCW,  
    PAPI_L3_TCW,
    PAPI_L2_TCA,
    PAPI_L3_TCA, 
};

// All of the state needed by the oracle
static oracle_ctx_t oracle_ctx;
static state_t temp_state;      // This is quite a big struct, so its allocated globally

size_t load_code(uint8_t *buf, size_t buf_sz, const char *f_name)
{
    FILE *f;
    struct stat s;
    size_t rd_len;
    
    CHECK_LIBC(stat(f_name, &s));

    f = fopen(f_name, "rb");
    if (!f) {
        fprintf(stderr, "Could not open file\n");
        return -1;
    }
    
    rd_len = fread(buf, 1, s.st_size, f);
    if (rd_len < s.st_size) {
        fprintf(stderr, "Could not read complete file\n");
        return rd_len;
    }
    
    fclose(f);

    return rd_len;
}

void *mmap_with_guard(void *addr, size_t size, int prot, int options)
{
    void *mem;

    if (addr != NULL)
        addr = addr - PAGE_SIZE;

    assert((size % PAGE_SIZE) == 0);

    mem = (uint8_t *) mmap(
                addr, 
                size + (2 * PAGE_SIZE), 
                prot, 
                options, 
                -1, 0);
    CHECK_MMAP_NULL(mem);
    
    // Add offset for the guard pages
    mem += PAGE_SIZE;

    CHECK_LIBC_NULL(mprotect(mem - PAGE_SIZE, PAGE_SIZE, PROT_NONE));
    CHECK_LIBC_NULL(mprotect(mem + size, PAGE_SIZE, PROT_NONE));

    return mem;
}

long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                int cpu, int group_fd, unsigned long flags)
{
    return syscall(__NR_perf_event_open, hw_event, pid, cpu,
                    group_fd, flags);
}

int perf_mktimer(config *cfg)
{
    int perf_fd;            // used by perf to limit the number of instructions executed
    struct perf_event_attr pe;
    
    // First initialize a perf counter to act at the timer
    memset(&pe, 0, sizeof(struct perf_event_attr));
    pe.type = PERF_TYPE_HARDWARE;
    pe.sample_type = PERF_SAMPLE_IP;
    pe.size = sizeof(struct perf_event_attr);
    pe.config = PERF_COUNT_HW_INSTRUCTIONS;
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;
    pe.pinned = 1;
    pe.precise_ip = 3; // TODO: might be too restrictive, reduce to 2
    //oracle_ctx.pe.sigtrap = 1;    // Not supported?
    pe.sample_period = cfg->max_exec_time;
    
    perf_fd = -1; // Perf must get intialized after the process is created
    
    perf_fd = perf_event_open(&pe, oracle_ctx.fork_pid, -1, -1, 0);
    CHECK_LIBC(perf_fd);
    return -1;
}

void print_wait_status(int status)
{
   if (WIFEXITED(status)) {
       printf("exited, status=%d: %s\n", WEXITSTATUS(status), strsignal(WEXITSTATUS(status)));
   } else if (WIFSIGNALED(status)) {
       printf("killed by signal %s\n", strsignal(WTERMSIG(status)));
   } else if (WIFSTOPPED(status)) {
       printf("stopped by signal %d: %s\n", WSTOPSIG(status), strsignal(WSTOPSIG(status)));
   } else if (WIFCONTINUED(status)) {
       printf("continued\n");
   }
}

// DO NOT USE, FOR DEBUGGING ONLY
oracle_ctx_t *get_ctx(void)
{
    return &oracle_ctx;
}

int reap_snippet(config *cfg)
{
    int status;
    int old_fork_pid;
    long long events[PAPI_MAX_EVENTS]; // Just throw away the results at this point
    
    if (oracle_ctx.fork_pid == -1)
        return 0;
    
    // We want to invalidate fork_pid asap
    old_fork_pid = oracle_ctx.fork_pid;
    oracle_ctx.fork_pid = -1;
    
    // We do want to handle things gracefully when possible
    if (cfg->num_papi_events) {
        CHECK_PAPI(PAPI_state(oracle_ctx.papi_fd, &status));
        
        if (status & PAPI_RUNNING)
            CHECK_PAPI(PAPI_stop(oracle_ctx.papi_fd, events));

        CHECK_PAPI(PAPI_detach(oracle_ctx.papi_fd));
    }

    CHECK_LIBC(ptrace(PTRACE_DETACH, old_fork_pid, NULL, NULL));
    oracle_ctx.papi_fd = -1;

    // Kill the process, we dont need it any more
    kill(old_fork_pid, SIGKILL); // Dont check error here, we dont care if it fails
    
    // Reap the process
    CHECK_LIBC(waitpid(old_fork_pid, &status, WUNTRACED));

#ifdef DEBUG
    print_wait_status(status);
#endif

    return 0;
}

int serialize_snippet(config *cfg, snippet_t *orig, snippet_t *serial)
{
    instruction_t *ins, *tgt;
    ZydisMnemonic nop_type;
    list_node *pos, *temp;
    ZydisEncoderOperand *op;
    size_t target_map[orig->count]; // Maps old_idx -> new_idx So we can fix up jump targets after
    int idx = 0;
    
    list_for_each_safe(pos, temp, &orig->head) {
        // Some bookeeping
        tgt = container_of(pos, instruction_t, node);
        target_map[tgt->idx] = idx;
        
        ins = snippet_allocate(serial);
        CHECK_PTR(ins, EGENERIC);

        // Copy over the old instruction
        *ins = *tgt;
        ins->address = ADDRESS_INVALID;
        ins->target_addrs = ADDRESS_INVALID; // Invalidate addresses
        ins->idx = idx;
        idx++;

        CHECK_INT(snippet_append(serial, ins), EGENERIC);
    
        ins = snippet_allocate(serial);
        CHECK_PTR(ins, EGENERIC);
        
        // Create and insert a new nop instruction
        nop_type = nop_instructions[rand_between(0, LENGTH(nop_instructions))];
        CHECK_INT(create_instruction(cfg, ins, nop_type, NULL), EGENERIC);
        
        ins->idx = idx;
        idx++;

        CHECK_INT(snippet_append(serial, ins), EGENERIC);
    }

    // Fix jumps as our list is not complete
    list_for_each_prev_safe(pos, temp, &orig->head) {
        ins = container_of(pos, instruction_t, node);

        if (ins->jump_target == JUMP_INVALID)
            continue;

        tgt = snippet_get(serial, target_map[ins->idx]); // TODO: Could maybe speed this up with another array
        CHECK_PTR(tgt, EGENERIC);
        
        tgt->jump_target = snippet_get(serial, target_map[ins->jump_target->idx]);
    }

    // Recompute metadata so the addresses are valid in the serialized snippet
    CHECK_INT(validate_metadata(serial), EGENERIC);
    
    list_for_each_prev_safe(pos, temp, &orig->head) {
        ins = container_of(pos, instruction_t, node);
        
        for (size_t i=0; i < ins->req.operand_count; i++) {
            op = &ins->req.operands[i];
            if (op->type != ZYDIS_OPERAND_TYPE_MEMORY || !is_rip_relative(ins, op))
                    continue;

            // Find the new instruction
            tgt = snippet_get(serial, target_map[ins->idx]); // TODO: Could maybe speed this up with another array
            CHECK_PTR(tgt, EGENERIC);
            
            // Correct the offset from rip
            tgt->req.operands[i].mem.displacement = (ins->address + op->mem.displacement) - tgt->address;
        }
    }

    return 0;
}

int init_papi(config *cfg)
{
    int ret;
    // Initialize papi in the parent thread which will perform the measurements
    ret = PAPI_library_init(PAPI_VER_CURRENT);
    CHECK_INT(ret, EGENERIC);

    if (ret != PAPI_VER_CURRENT)
        return -1;
    oracle_ctx.papi_fd = PAPI_NULL;
    cfg->num_papi_events = 0; // Just to be sure
    CHECK_PAPI(PAPI_create_eventset(&oracle_ctx.papi_fd));
    CHECK_PAPI(PAPI_assign_eventset_component(oracle_ctx.papi_fd, 0));
    CHECK_PAPI(PAPI_set_domain(PAPI_DOM_USER));

    //CHECK_PAPI(PAPI_set_granularity(PAPI_GRN_PROC));
    //PAPI_add_event(oracle_ctx.papi_fd, PAPI_TOT_INS);

    // Add as many events as we can
    for (int i=0; i < LENGTH(papi_stats); i++) {
        if (PAPI_query_event(papi_stats[i]) != PAPI_OK)
            continue;

        if (PAPI_add_event(oracle_ctx.papi_fd, papi_stats[i]) != PAPI_OK)
            continue;

        cfg->papi_event_codes[cfg->num_papi_events++] = papi_stats[i];
    }
    
    return 0;
}

int init_oracle(config *cfg)
{
    if (!cfg)
        return -1;
    // TODO: Check if state already exists here

    // This is shared as the parent needs to write & child executes
    oracle_ctx.target = (void (*)(void)) mmap(
                (void *) cfg->snippet_code.address, 
                cfg->snippet_code.size, 
                MMAP_PROT, 
                (MMAP_OPT | MAP_FIXED | MAP_SHARED) & ~MAP_PRIVATE, 
                -1, 0);
    CHECK_MMAP(oracle_ctx.target);
    
    oracle_ctx.target_mem = mmap_with_guard(
            (void *) cfg->snippet_memory.address,
            cfg->snippet_memory.size,
            MMAP_PROT,
            MMAP_OPT | MAP_FIXED);
    CHECK_PTR(oracle_ctx.target_mem, EGENERIC);
    
    oracle_ctx.target_stack = mmap_with_guard(
            (void *) cfg->snippet_stack.address,
            cfg->snippet_stack.size, 
            MMAP_PROT, 
            MMAP_OPT | MAP_FIXED | MAP_STACK | MAP_GROWSDOWN);
    CHECK_PTR(oracle_ctx.target_stack, EGENERIC);
    
    // Stack needs to point to the top of the region (highest addr)
    oracle_ctx.target_stack += cfg->snippet_stack.size;
    
    // Initialize the snippets
    CHECK_INT(snippet_init(cfg, &oracle_ctx.original), EGENERIC);
    CHECK_INT(snippet_init(cfg, &oracle_ctx.serialized), EGENERIC);
    
    // Initialize remaining variables in oracle_ctx
    oracle_ctx.papi_fd = -1;
    oracle_ctx.fork_pid = -1;

    return 0;
}

int destroy_oracle(config *cfg)
{
    if (oracle_ctx.fork_pid != -1)
        CHECK_INT(reap_snippet(cfg), EGENERIC);
    
    CHECK_INT(snippet_destroy(&oracle_ctx.original), EGENERIC);
    CHECK_INT(snippet_destroy(&oracle_ctx.serialized), EGENERIC);
    
    CHECK_LIBC(munmap(oracle_ctx.target, cfg->snippet_code.size));
    CHECK_LIBC(munmap(oracle_ctx.target_mem, cfg->snippet_memory.size));
    CHECK_LIBC(munmap(oracle_ctx.target_stack, cfg->snippet_stack.size));
    
    // Close the papi library
    PAPI_shutdown();
    oracle_ctx.papi_fd = -1;

    // Wipe out all other state
    memset(&oracle_ctx, 0, sizeof(oracle_ctx_t));
    
    return 0;
}

int load_snippet(config *cfg, snippet_t *snip)
{
    uint8_t *snippet_buf = (uint8_t *)oracle_ctx.target;
    size_t len;
    instruction_t *ins;

    if (!oracle_ctx.target || !oracle_ctx.target_mem)
        return -1;

    // Dynamically add a jump instruction back into safe code
    ins = snippet_allocate(snip);
    ins->req.mnemonic = ZYDIS_MNEMONIC_JMP;
    ins->req.operand_count = 1;
    ins->req.machine_mode = cfg->mode;
    ins->req.operands[0].type = ZYDIS_OPERAND_TYPE_MEMORY;
    ins->req.operands[0].mem.base = ZYDIS_REGISTER_RIP;
    ins->req.operands[0].mem.displacement = 0;
    ins->req.operands[0].mem.size = 8;
    
    CHECK_INT(snippet_append(snip, ins), EGENERIC);

    // Change permissions on the buffer, so it can be written to
    CHECK_LIBC(mprotect(snippet_buf, cfg->snippet_code.size, PROT_READ | PROT_WRITE));

    // Clear the old code and memory, and write in the new code
    memset(snippet_buf, 0, cfg->snippet_code.size);
    memset(oracle_ctx.target_mem, 0, cfg->snippet_memory.size); // TODO: This can be done once in the parent (cow)
    
    // Ensure that all of the jump offsets are correct
    CHECK_INT(pipeline_encode(snip), EGENERIC);

    // Encode the new snippet_t directly into the buffer
    len = snippet_encode(snip, snippet_buf, cfg->snippet_code.size);    
    CHECK_INT(len, ESNIPENCODE);
    
    //snippet_print(snip, stdout, true, true);
    assert(len < cfg->snippet_code.size);
    
    // Write the return target into the code buffer, so the jump back can read it
    *((uint64_t *) &snippet_buf[len]) = (uint64_t) target_return;
    
    // Set the protections back to execute
    CHECK_LIBC(mprotect(snippet_buf, cfg->snippet_code.size, PROT_READ | PROT_EXEC));
    
    return 0;
}

int gather_state(config *cfg, state_t *state)
{
    struct iovec iov;
    
    // Collect papi counters
    if (cfg->num_papi_events)
        CHECK_PAPI(PAPI_stop(oracle_ctx.papi_fd, state->perf));

    state->perf_count = cfg->num_papi_events;
    
    iov.iov_base = state->x_state.__padding;
    iov.iov_len = sizeof(xsave_buf_t);
    CHECK_LIBC(ptrace(PTRACE_GETREGSET, oracle_ctx.fork_pid, NT_X86_XSTATE, &iov));
    
    // Save the size, since its variable
    state->xstate_sz = iov.iov_len;

    iov.iov_base = &state->gpr_state;
    iov.iov_len = sizeof(struct user_regs_struct);
    CHECK_LIBC(ptrace(PTRACE_GETREGSET, oracle_ctx.fork_pid, NT_PRSTATUS, &iov));
    
    state->final_rip = state->gpr_state.rip;

    return 0;
}

int clear_extra_registers(config *cfg) 
{
    struct iovec iov;
    long long int temp_reg = 0;

    iov.iov_base = temp_state.x_state.__padding;
    iov.iov_len = sizeof(xsave_buf_t);
    CHECK_LIBC(ptrace(PTRACE_GETREGSET, oracle_ctx.fork_pid, NT_X86_XSTATE, &iov));
    
    // Wipe the xsave buffer
    memset(temp_state.x_state.__padding, 0, iov.iov_len);
    
    // Put in some sensible defaults
    ((struct xregs_state *)temp_state.x_state.__padding)->header.xfeatures = _xgetbv(0); // All CPU supported features enabled
    ((struct xregs_state *)temp_state.x_state.__padding)->header.xcomp_bv = 0;
    ((struct xregs_state *)temp_state.x_state.__padding)->i387.mxcsr = 0x1F80;
    ((struct xregs_state *)temp_state.x_state.__padding)->i387.mxcsr_mask = 0x0000FFFF;
    ((struct xregs_state *)temp_state.x_state.__padding)->i387.cwd = 0x037F;

    CHECK_LIBC(ptrace(PTRACE_SETREGSET, oracle_ctx.fork_pid, NT_X86_XSTATE, &iov));
    
    // Clear the debug registers we can
    for (int i=0; i < 4; i++)
        CHECK_LIBC(ptrace(PTRACE_POKEUSER, oracle_ctx.fork_pid, offset_of(struct user, u_debugreg[i]), &temp_reg));
    
    // DB6 & 7 need special handling
    temp_reg = ptrace(PTRACE_PEEKUSER, oracle_ctx.fork_pid, offset_of(struct user, u_debugreg[6]), NULL);
    temp_reg &= ~(1 << 13) & ~(1 << 14) & ~(1 << 15);
    CHECK_LIBC(ptrace(PTRACE_POKEUSER, oracle_ctx.fork_pid, offset_of(struct user, u_debugreg[6]), temp_reg));
    
    temp_reg = ptrace(PTRACE_PEEKUSER, oracle_ctx.fork_pid, offset_of(struct user, u_debugreg[7]), NULL);
    temp_reg &= ~(1 << 13);
    CHECK_LIBC(ptrace(PTRACE_POKEUSER, oracle_ctx.fork_pid, offset_of(struct user, u_debugreg[7]), 1 << 13));
    
    return 0;
}

// Clear out any registers and prepare state for new execution
int clear_registers(config *cfg)
{
    CHECK_LIBC(ptrace(PTRACE_GETREGS, oracle_ctx.fork_pid, NULL, &temp_state.gpr_state));
    
    // Dont clear out everything from this set
    temp_state.gpr_state.rsi = (unsigned long long) oracle_ctx.target_mem; 
    temp_state.gpr_state.fs_base = (unsigned long long) oracle_ctx.target_mem;
    temp_state.gpr_state.rsp = (unsigned long long) oracle_ctx.target_stack;
    temp_state.gpr_state.rbp = (unsigned long long) oracle_ctx.target_stack;
    temp_state.gpr_state.rdi = 0x1; // Maybe set this to something else
    temp_state.gpr_state.rax = 0;
    temp_state.gpr_state.rbx = 0;
    temp_state.gpr_state.rcx = 0;
    temp_state.gpr_state.rdx = 0;
    temp_state.gpr_state.r8 = 0;
    temp_state.gpr_state.r9 = 0;
    temp_state.gpr_state.r10 = 0;
    temp_state.gpr_state.r11 = 0;
    temp_state.gpr_state.r12 = 0;
    temp_state.gpr_state.r13 = 0;
    temp_state.gpr_state.r14 = 0;
    temp_state.gpr_state.r15 = 0;
    temp_state.gpr_state.es = 0;
    temp_state.gpr_state.ds = 0;
    temp_state.gpr_state.fs = 0;
    temp_state.gpr_state.eflags = 0;
    // temp_state.gpr_state.cs = 0; // These cannot be cleared without crashing
    // temp_state.gpr_state.ss = 0;


    CHECK_LIBC(ptrace(PTRACE_SETREGS, oracle_ctx.fork_pid, NULL, &temp_state.gpr_state));
    
    CHECK_INT(clear_extra_registers(cfg), EGENERIC);
    return 0;
}

// Recover a few key registers to child process can resume
int recover_registers(config *cfg, int sig)
{
    // A clean copy of the register state was saved earlier

    // Reset rip regardless, signal might have been generated by the snippet
    oracle_ctx.reg_state.rip = (unsigned long long)&signal_return;

    CHECK_LIBC(ptrace(PTRACE_SETREGS, oracle_ctx.fork_pid, NULL, &oracle_ctx.reg_state));
    
    CHECK_INT(clear_extra_registers(cfg), EGENERIC);
    return 0;
}

int fork_snippet(config *cfg)
{
    // Ignore signals because we handle these in the parent
    signal(SIGSEGV, SIG_IGN);
    signal(SIGALRM, SIG_IGN);
    signal(SIGILL, SIG_IGN);
    signal(SIGFPE, SIG_IGN);
    signal(SIGBUS, SIG_IGN);
    signal(SIGABRT, SIG_IGN);
    signal(SIGTRAP, SIG_IGN);
    signal(SIGVTALRM, SIG_IGN);
    
    CHECK_INT(set_affinity(cfg->core_1), EGENERIC);

    // Save current state for restoring
    sigsetjmp(oracle_ctx.fork_env, 1);
    
    // Clear out memory and stack
    memset((oracle_ctx.target_stack - cfg->snippet_stack.size), 0, cfg->snippet_stack.size);
    memset(oracle_ctx.target_mem, 0, cfg->snippet_memory.size);

    // Wait until the parent is ready
    setitimer(ITIMER_VIRTUAL, &(struct itimerval){{0, 0}, {0, cfg->max_exec_time}}, 0);
    interrupt(); // The parent also clears out the registers here

    // Jump to untrused code
    asm volatile("jmp *oracle_ctx(%%rip)\n":::); // Target is first element of oracle_ctx

    // Where the target code returns to
    asm volatile(".global target_return\n"
                 "target_return:\n");

	// Make sure that state is committed best we can before stopping
    asm volatile("mfence\n"
                 "wait\n"); 
    // Stop the process so we can read the register contents ect.
    interrupt();
    //*(volatile int*)0 = 0; // Test segfault

    // Where (ptrace handled) signals should return from
    asm volatile(".global signal_return\n"
                 "signal_return:\n");
    
    // Disable timer at the first possible moment, state should hopefully be restored enough 
    setitimer(ITIMER_VIRTUAL, &(struct itimerval){{0, 0}, {0, 0}}, 0);
    // Restore whatever additional state possible
    siglongjmp(oracle_ctx.fork_env, 1);

    _exit(EXIT_FAILURE);
}

int execute_snippet(config *cfg, state_t *state)
{
    int status;
    int sig;

    if (oracle_ctx.fork_pid < 0) {
        oracle_ctx.fork_pid = fork();
        CHECK_LIBC(oracle_ctx.fork_pid);

        if (oracle_ctx.fork_pid == 0)
            fork_snippet(cfg); // NO RETURN

        CHECK_LIBC(ptrace(PTRACE_SEIZE, oracle_ctx.fork_pid, 0, PTRACE_O_EXITKILL));
        
        if (cfg->num_papi_events)
            CHECK_PAPI(PAPI_attach(oracle_ctx.papi_fd, oracle_ctx.fork_pid));
    } else {
        // Resume the paused process
        CHECK_LIBC(ptrace(PTRACE_CONT, oracle_ctx.fork_pid, NULL, NULL));
    }
    
    // Retry spesefically if we unexpectedly got a timer, they are oneshots anyways
    do {
        CHECK_LIBC(waitpid(oracle_ctx.fork_pid, &status, __WALL));
        //sig = WIFSTOPPED(status) ? WSTOPSIG(status) : WTERMSIG(status);
        sig = WIFSTOPPED(status) ? WSTOPSIG(status) : 
              WIFSIGNALED(status) ? WTERMSIG(status) : -1;

        if (sig == SIGVTALRM)
            CHECK_LIBC(ptrace(PTRACE_CONT, oracle_ctx.fork_pid, NULL, NULL));
    } while (sig == SIGVTALRM);

    // Something has gone pretty seriously wrong here, we are desynced or have an urecoverable error 
    if (sig != SIGTRAP) {
        cfg->num_papi_events = 0;
        gather_state(cfg, state);
        CHECK_INT(reap_snippet(cfg), EGENERIC); // Kill the process to be extra sure
        CHECK_INT(dump_snippet(&oracle_ctx.original, NULL, 0, "oracle_errors", false, true, sig, state->gpr_state.rip), EGENERIC);
        return -95; // This will eventually cause a complete reset
    }
    
    // Start PAPI recording, if we have events
    if (cfg->num_papi_events) {
        CHECK_PAPI(PAPI_reset(oracle_ctx.papi_fd));
        CHECK_PAPI(PAPI_start(oracle_ctx.papi_fd));
    }
    // Save register state so we can restore it later
    // TODO: Do this once, save runtime and use cow 
    CHECK_LIBC(ptrace(PTRACE_GETREGS, oracle_ctx.fork_pid, NULL, &oracle_ctx.reg_state));

    // Clear all of the registers
    CHECK_INT(clear_registers(cfg), EGENERIC);
    CHECK_LIBC(ptrace(PTRACE_CONT, oracle_ctx.fork_pid, NULL, NULL));
    
    CHECK_LIBC(waitpid(oracle_ctx.fork_pid, &status, __WALL));
    
    //print_wait_status(status);

    sig = WIFSTOPPED(status) ? WSTOPSIG(status) : WTERMSIG(status);
    state->final_sig = sig; // Record the terminating / stopping sig

    switch (sig) {
    case SIGALRM:
    case SIGVTALRM:
        // Timer hit, discard result        
        break; 
    case SIGTRAP: // Success, reached the end. Or the snippet had an intx instruction
    case SIGSTOP:
    case SIGABRT:
    case SIGSEGV:
    case SIGILL:
    case SIGFPE:
    case SIGBUS:
        CHECK_INT(gather_state(cfg, state), EGENERIC);
        break;
    default:
        // Technically we could just ignore all signals, but its nice to know whats generated
        print_wait_status(status); 
        return -29; // Found a new signal
    }

    // Possibly reap the process if it exited
    if (WIFSIGNALED(status) || WIFEXITED(status))
        CHECK_INT(reap_snippet(cfg), EGENERIC);
    else
        CHECK_INT(recover_registers(cfg, sig), EGENERIC);

    // We can just keep reusing the same process
    return 0;
}

int measure_snippet(config *cfg, const uint8_t *buf, size_t buf_sz, states_t *states)
{
    // Clear out both snippet buffers
    CHECK_INT(snippet_free(&oracle_ctx.original), EGENERIC);
    CHECK_INT(snippet_free(&oracle_ctx.serialized), EGENERIC);
    
    // Decode the original snippet
    CHECK_INT(snippet_decode(&oracle_ctx.original, cfg->snippet_code.address, buf, buf_sz), EGENERIC);
    CHECK_INT(pipeline_decode(&oracle_ctx.original), EGENERIC);
    //CHECK_INT(pipeline_validate(&oracle_ctx.original), EGENERIC);
    
    // Insert nops inbetween operations
    CHECK_INT(serialize_snippet(cfg, &oracle_ctx.original, &oracle_ctx.serialized), EGENERIC);
    
    if (oracle_ctx.papi_fd < 0)
        CHECK_INT(init_papi(cfg), EGENERIC);
    
    // We only really ever iterate once
    for (int i=0; i < cfg->iterations; i++) {
        // Clear state for new results
        memset(states, 0, sizeof(states_t));
        
        // Run the original
        CHECK_INT(load_snippet(cfg, &oracle_ctx.original), ESNIPLOAD);
        CHECK_INT(execute_snippet(cfg, &states->original), ESNIPEXEC);
        
        // Run the serialized version
        CHECK_INT(load_snippet(cfg, &oracle_ctx.serialized), ESNIPLOAD);
        CHECK_INT(execute_snippet(cfg, &states->serial), ESNIPEXEC);
    }
    
    return 0;
}

