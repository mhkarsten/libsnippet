#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <immintrin.h>

#include <papi.h>

#include "debug_strings.h"
#include "common.h"
#include "xstate.h"
#include "error.h"
#include "score.h"
#include "snippet.h"

// Data expected to be uint32 little endien
void print_bytes(uint32_t *data, size_t num_bits, bool swap, FILE *out)
{
    for (size_t i=0; i < ((num_bits)/8)/4; i++) {
        if (swap) {
            fprintf(out, "%08x", __builtin_bswap32(data[i]));
        } else {
            fprintf(out, "%08x", data[i]);
        }
    }
}

void print_buffer(uint8_t *buf, size_t num_bytes, FILE *out)
{
    for (size_t i=0; i < num_bytes; i++) {
        fprintf(out, "%02hhx ", buf[i]);
        
        if (((i+1) % 32) == 0)
            fprintf(out, "\n");
    }
    fprintf(out, "\n");
}

void print_supported(void)
{
    uint64_t xcr0 = _xgetbv(0);
    uint32_t eax, ebx, ecx, edx;

    for (enum xfeature feature=0; feature < XFEATURE_MAX; feature++) {
        if (xcr0 & (1ULL << feature))
            printf("%s is enabled\n", xfeature_strings[feature]);

        cpuid_count(0xd, feature, &eax, &ebx, &ecx, &edx);
        if (eax != 0 || ebx != 0)
            printf("%s is supported\n", xfeature_strings[feature]);
    }
}

void print_prstatus(struct user_regs_struct *regs, FILE *out)
{
    fprintf(out, "rax:\t%llx\n", regs->rax);
    fprintf(out, "rbx:\t%llx\n", regs->rbx);
    fprintf(out, "rcx:\t%llx\n", regs->rcx);
    fprintf(out, "rdx:\t%llx\n", regs->rdx);
    fprintf(out, "rdi:\t%llx\n", regs->rdi);
    fprintf(out, "rsi:\t%llx\n", regs->rsi);
    fprintf(out, "rbp:\t%llx\n", regs->rbp);
    fprintf(out, "rsp:\t%llx\n", regs->rsp);
    fprintf(out, "rip:\t%llx\n", regs->rip);
    fprintf(out, "r8:\t%llx\n", regs->r8);
    fprintf(out, "r9:\t%llx\n", regs->r9);
    fprintf(out, "r10:\t%llx\n", regs->r10);
    fprintf(out, "r11:\t%llx\n", regs->r11);
    fprintf(out, "r12:\t%llx\n", regs->r12);
    fprintf(out, "r13:\t%llx\n", regs->r13);
    fprintf(out, "r14:\t%llx\n", regs->r14);
    fprintf(out, "r15:\t%llx\n", regs->r15);
    fprintf(out, "cs:\t%llx\n", regs->cs);
    fprintf(out, "ss:\t%llx\n", regs->ss);
    fprintf(out, "ds:\t%llx\n", regs->ds);
    fprintf(out, "fs:\t%llx\n", regs->fs);
    fprintf(out, "gs:\t%llx\n", regs->gs);
    fprintf(out, "es:\t%llx\n", regs->es);
    fprintf(out, "orig_rax:\t%llx\n", regs->orig_rax);
    fprintf(out, "eflags:\t\t%llx\n", regs->eflags);
    fprintf(out, "gs_base:\t%llx\n", regs->gs_base);
    fprintf(out, "fs_base:\t%llx\n", regs->fs_base);
}

void print_xstate(struct xregs_state *regs, FILE *out)
{
    uint32_t eax, ecx, edx;
    uint32_t feature_offset = 0;
    uint32_t last_struct_size = 0;
    xsave_ptrs_t xstate = {0};
    struct ymmh_struct *ymmregs = NULL; // Need a copy of this to parse zmm
    struct xtile_cfg *xtile_cfg = NULL; // Need a copy to parse xtile_data when compressed
    
    //print_buffer((uint32_t *)regs, 1088);

    for (enum xfeature feature=0; feature < XFEATURE_MAX; feature++) {
        // Feature is not supported by this cpu
        if (!(regs->header.xfeatures & (1ULL << feature)))
            continue;
        
        // Get the offset of a feature within the xsave struct
        if (regs->header.xcomp_bv & XCOMP_BV_COMPACTED_FORMAT)
            feature_offset += last_struct_size;    
        else
            cpuid_count(0xd, feature, &eax, &feature_offset, &ecx, &edx); // The offsets returned from here are always fixed
        
        //printf("%s is at offset %d with size %d\n", xfeature_strings[feature], feature_offset, eax);
        switch (feature) {
        case XFEATURE_SSE:
            break; // Skip this as FP and SSE are always enabled and share the same space
        case XFEATURE_FP: 
            xstate.fxregs = (struct fxregs_state *) &regs->i387; // Fixed offset, we always know where this is (cpuid also returns nonsense)
            fprintf(out, "mxcsr:\t\t%x\n", xstate.fxregs->mxcsr);
            fprintf(out, "mxcsr_mask:\t%x\n", xstate.fxregs->mxcsr_mask);
            
            for (int i=0; i < 8; i++) {
                fprintf(out, "st%d:\t", i);
                print_bytes(&xstate.fxregs->st_space[i*4], 128, true, out);
                fprintf(out, "\n");
            }

            for (int i=0; i < 16; i++) {
                fprintf(out, "xmm%d:\t", i);
                print_bytes(&xstate.fxregs->xmm_space[i*4], 128, true, out);
                fprintf(out, "\n");
            }
            last_struct_size = sizeof(struct fxregs_state);
            break;
        case XFEATURE_YMM:
            xstate.ymmregs = (struct ymmh_struct *)(((void *)regs) + feature_offset);
            ymmregs = xstate.ymmregs;

            for (int i=0; i < 16; i++) {
                fprintf(out, "ymm%d:\t", i);
                print_bytes((uint32_t *)xstate.ymmregs->hi_ymm[i].regbytes, 128, false, out);
                print_bytes((uint32_t *)&regs->i387.xmm_space[i*4], 128, true, out);
                fprintf(out, "\n");
            }
            last_struct_size = sizeof(struct ymmh_struct);
            break;
        case XFEATURE_BNDREGS:
            xstate.bndregs = (struct mpx_bndreg_state *)(((void *)regs) + feature_offset);
            for (int i=0; i < 4; i++) {
                fprintf(out,"bnd%d:\t", i);
                fprintf(out, "%16lx%16lx\n", xstate.bndregs->bndreg[i].upper_bound, xstate.bndregs->bndreg[i].lower_bound);
            }
            last_struct_size = sizeof(struct mpx_bndreg_state);
            break;
        case XFEATURE_BNDCSR:
            xstate.bndcsr = (struct mpx_bndcsr_state *)(((void *)regs) + feature_offset);
            fprintf(out, "bndcfgu:\t%lx\n", xstate.bndcsr->bndcsr.bndcfgu);
            fprintf(out, "bndstatus:\t%lx\n", xstate.bndcsr->bndcsr.bndstatus);
            last_struct_size = sizeof(struct mpx_bndcsr_state);
            break;
        case XFEATURE_OPMASK:
            xstate.kregs = (struct avx_512_opmask_state *)(((void *)regs) + feature_offset);
            for (int i=0; i < 8; i++)
                fprintf(out, "k%d:\t%16lx\n", i, xstate.kregs->opmask_reg[i]);
            last_struct_size = sizeof(struct avx_512_opmask_state);
            break;
        case XFEATURE_ZMM_Hi256:
            xstate.zmmregs1 = (struct avx_512_zmm_uppers_state *)(((void *)regs) + feature_offset);
            if (!ymmregs) {
                fprintf(out, "Missing avx registers needed to parse avx512\n");
                exit(EXIT_FAILURE);
            }

            for (int i=0; i < 16; i++) {
                fprintf(out, "zmm%d:\t", i);
                print_bytes((uint32_t *)xstate.zmmregs1->zmm_upper[i].regbytes, 256, false, out);
                print_bytes((uint32_t *)ymmregs->hi_ymm[i].regbytes, 128, false, out);
                print_bytes((uint32_t *)&regs->i387.xmm_space[i*4], 128, true, out);
                fprintf(out, "\n");
            }
            last_struct_size = sizeof(struct avx_512_zmm_uppers_state);
            break;
        case XFEATURE_Hi16_ZMM:
            xstate.zmmregs2 = (struct avx_512_hi16_state *)(((void *)regs) + feature_offset);

            for (int i=0; i < 16; i++) {
                fprintf(out, "zmm%d:\t", i+16);
                print_bytes((uint32_t *)xstate.zmmregs2->hi16_zmm[i].regbytes, 512, false, out);
                fprintf(out, "\n");
            }
            last_struct_size = sizeof(struct avx_512_hi16_state);
            break;
        case XFEATURE_PT_UNIMPLEMENTED_SO_FAR:
            break; // Reserved bits
        case XFEATURE_PKRU: // struct pkru_state
            xstate.pkrureg = (struct pkru_state *)(((void *)regs) + feature_offset);
            fprintf(out, "pkru:\t%x\n", xstate.pkrureg->pkru);
            last_struct_size = sizeof(struct pkru_state);
            break;
        case XFEATURE_PASID:
            // struct ia32_pasid_state
            xstate.pasid = (struct ia32_pasid_state *)(((void *)regs) + feature_offset);
            fprintf(out, "pasid:\t%lx\n", xstate.pasid->pasid);
            last_struct_size = sizeof(struct ia32_pasid_state);
            break;
        case XFEATURE_CET_USER:
            xstate.cet_user = (struct cet_user_state *)(((void *)regs) + feature_offset);
            fprintf(out, "user_cet:\t%lx\n", xstate.cet_user->user_cet);
            fprintf(out, "user_ssp:\t%lx\n", xstate.cet_user->user_ssp);
            last_struct_size = sizeof(struct cet_user_state);
            break;
        case XFEATURE_CET_KERNEL:
            xstate.cet_super = (struct cet_supervisor_state *)(((void *)regs) + feature_offset);
            fprintf(out, "pl0_ssp:\t%lx\n", xstate.cet_super->pl0_ssp);
            fprintf(out, "pl1_ssp:\t%lx\n", xstate.cet_super->pl1_ssp);
            fprintf(out, "pl2_ssp:\t%lx\n", xstate.cet_super->pl2_ssp);
            last_struct_size = sizeof(struct cet_supervisor_state);
            break;
        case XFEATURE_RSRVD_COMP_13:
        case XFEATURE_RSRVD_COMP_14:
            break; // More reserved bits
        case XFEATURE_LBR:
            xstate.lbr = (struct arch_lbr_state *)(((void *)regs) + feature_offset);
            fprintf(out, "lbr_ctl:\t%lx\n", xstate.lbr->lbr_ctl);
            fprintf(out, "lbr_depth:\t%lx\n", xstate.lbr->lbr_depth);
            fprintf(out, "ler_to:\t%lx\n", xstate.lbr->ler_to);
            fprintf(out, "ler_from:\t%lx\n", xstate.lbr->ler_from);
            fprintf(out, "ler_info:\t%lx\n", xstate.lbr->ler_info);

            for (size_t i=0; i < xstate.lbr->lbr_depth; i++)
                fprintf(out, "lbr%ld:\tto %lx, from %lx, info %lx\n", i, 
                        xstate.lbr->entries[i].to, 
                        xstate.lbr->entries[i].from, 
                        xstate.lbr->entries[i].info);
            last_struct_size = sizeof(struct arch_lbr_state) + (xstate.lbr->lbr_depth * sizeof(struct lbr_entry));
            break;
        case XFEATURE_RSRVD_COMP_16:
            break; // More reserved bits
        case XFEATURE_XTILE_CFG:
            xstate.xtile_cfg = (struct xtile_cfg *)(((void *)regs) + feature_offset);
            xtile_cfg = xstate.xtile_cfg;
            for (int i=0; i < 8; i++) {
                fprintf(out, "tcfg%d:\t%lx\n", i,  xstate.xtile_cfg->tcfg[i]);
            }
            last_struct_size += sizeof(struct xtile_cfg);
            break;
        case XFEATURE_XTILE_DATA:
            xstate.tmmregs = (struct xtile_data *)(((void *)regs) + feature_offset);
            if (!xtile_cfg) {
                fprintf(out, "xtile_cfg needed to properly parse xtile_data\n");
                exit(EXIT_FAILURE);
            }

            for (int i=0; i < 8; i++) { // Should be <= 8
                if (!xtile_cfg->tcfg[i])
                    continue;
                fprintf(out, "tmm%d:\t", i);
                print_bytes((uint32_t *)xstate.tmmregs[i].tmm.regbytes, 1024*8, false, out);
                fprintf(out, "\n");
                last_struct_size += sizeof(struct xtile_data);
            }
            break;
        case XFEATURE_APX:
            xstate.apxregs = (struct apx_state *)(((void *)regs) + feature_offset);
            for (int i=0; i < 8; i++) // The other >= 8 are not implemennted
                fprintf(out, "r%d:\t%lx\n", i+8, xstate.apxregs->egpr[i]);

            last_struct_size += sizeof(struct apx_state);
            break;
        default:
            fprintf(out, "xfeature unknown\n");
        }
    }
}

void print_events(long long *papi_stats, int *events, size_t num_events, FILE *out)
{
    char event_name[PAPI_MAX_STR_LEN];

    for (size_t i=0; i < num_events; i++) {
        if (PAPI_event_code_to_name(events[i], event_name) < 0) {
            fprintf(out, "Could not get event name for event %d\n", events[i]);
            exit(EXIT_FAILURE);
        }

        fprintf(out, "%s: %lld\n", event_name, papi_stats[i]);    
    }
}

// The caller is responsible for ensuring the offset in dst is valid
// Returns: Returns new pointer to dst or NULL on error
uint8_t *normalize_bytes(uint8_t *bytes, size_t sz, size_t stride, size_t factor, uint8_t *dst)
{
    size_t written = 0;
    bool has_value;

    if (stride > sz || !dst)
        return NULL;
    
    assert((sz % stride) == 0);
    
    for (size_t i=0; i < sz; i+=stride) {
        has_value = false;
        for (size_t j=i; j < i+stride; j++) {
            if (!bytes[j])
                continue;

            has_value = true;
            break;
        }
        
        // State is normalized 1 to 1 basically
        memset(dst + written, has_value ? 0xff : 0x00, stride * factor);
        written += stride * factor;
    }

    return dst + written;
}

int normalize_xstate(struct xregs_state *regs, uint8_t *cov, size_t buf_size)
{
    uint32_t eax, ecx, edx;
    uint32_t feature_offset = 0;
    uint32_t last_struct_size = 0;
    xsave_ptrs_t xstate = {0};
    struct ymmh_struct *ymmregs = NULL; // Need a copy of this to parse zmm
    struct xtile_cfg *xtile_cfg = NULL; // Need a copy to parse xtile_data when compressed
    uint8_t *cov_pos = cov;

    // This is a conservative check to assuming everything gets encoded at 2x factor
    if ((sizeof(struct xregs_state) + 3 * PAGE_SIZE) * 2 > buf_size)
        return -1;
	
    for (enum xfeature feature=0; feature < XFEATURE_MAX; feature++) {
	    // Feature is not supported by this cpu
        if (!(regs->header.xfeatures & (1ULL << feature)))
            continue;
        
        // Get the offset of a feature within the xsave struct
        if (regs->header.xcomp_bv & XCOMP_BV_COMPACTED_FORMAT)
            feature_offset += last_struct_size;    
        else
            cpuid_count(0xd, feature, &eax, &feature_offset, &ecx, &edx); // The offsets returned from here are always fixed

        // NOTE: Modern compilers/assemblers do not support intel MPX anymore, support was dropped in 2018 afaik.
        //       However the parsing, is still present. I assume the regisers are also still writable
        // NOTE: The APX will likely never be enabled, these are the rx gpr registers, they are already saved
        //       somewhere else so there is no need.
        switch (feature) {
        case XFEATURE_SSE:
            break; // Skip this as FP and SSE are always enabled and share the same space
        case XFEATURE_FP:
            xstate.fxregs = (struct fxregs_state *) &regs->i387; // Fixed offset, we always know where this is (cpuid also returns nonsense)
            // mxcsr & mxcsr mask
            cov_pos = NORMALIZE_32(&xstate.fxregs->mxcsr, 2, 1, cov_pos);
            CHECK_PTR(cov_pos, EGENERIC);
            // st and xmm registers
            cov_pos = NORMALIZE_128(xstate.fxregs->st_space, 24, 1, cov_pos);

            last_struct_size = sizeof(struct fxregs_state);
            break;
        case XFEATURE_YMM:
            xstate.ymmregs = (struct ymmh_struct *)(((void *)regs) + feature_offset);
            ymmregs = xstate.ymmregs;
            // Since the low ymm registers are the xmm registers, we record things twice here a bit
            for (int i=0; i < 16; i++) {
                cov_pos = NORMALIZE_128(xstate.ymmregs->hi_ymm[i].regbytes, 1, 1, cov_pos);
                CHECK_PTR(cov_pos, EGENERIC);
                cov_pos = NORMALIZE_128(&regs->i387.xmm_space[i*4], 1, 1, cov_pos);
                CHECK_PTR(cov_pos, EGENERIC);
            }

            last_struct_size = sizeof(struct ymmh_struct);
            break;
        case XFEATURE_BNDREGS:
            xstate.bndregs = (struct mpx_bndreg_state *)(((void *)regs) + feature_offset);
            cov_pos = NORMALIZE_64(xstate.bndregs->bndreg, 8, 1, cov_pos);

            last_struct_size = sizeof(struct mpx_bndreg_state);
            break;
        case XFEATURE_BNDCSR:
            xstate.bndcsr = (struct mpx_bndcsr_state *)(((void *)regs) + feature_offset);
            cov_pos = NORMALIZE_64(&xstate.bndcsr->bndcsr, 2, 1, cov_pos);

            last_struct_size = sizeof(struct mpx_bndcsr_state);
            break;        
        case XFEATURE_OPMASK:
            xstate.kregs = (struct avx_512_opmask_state *)(((void *)regs) + feature_offset);
            cov_pos = NORMALIZE_64(xstate.kregs->opmask_reg, 8, 1, cov_pos);

            last_struct_size = sizeof(struct avx_512_opmask_state);
            break;
        case XFEATURE_ZMM_Hi256:
            xstate.zmmregs1 = (struct avx_512_zmm_uppers_state *)(((void *)regs) + feature_offset);
            CHECK_PTR(ymmregs, EGENERIC);

            for (int i=0; i < 16; i++) {
                cov_pos = NORMALIZE_256(xstate.zmmregs1->zmm_upper[i].regbytes, 1, 1, cov_pos); 
                CHECK_PTR(cov_pos, EGENERIC);
                cov_pos = NORMALIZE_128(ymmregs->hi_ymm[i].regbytes, 1, 1, cov_pos);
                CHECK_PTR(cov_pos, EGENERIC);
                cov_pos = NORMALIZE_128(&regs->i387.xmm_space[i*4], 1, 1, cov_pos);
                CHECK_PTR(cov_pos, EGENERIC);
            }
            last_struct_size = sizeof(struct avx_512_zmm_uppers_state);
            break;void print_events(long long *papi_stats, int *events, size_t num_events, FILE *out);
        case XFEATURE_Hi16_ZMM:
            xstate.zmmregs2 = (struct avx_512_hi16_state *)(((void *)regs) + feature_offset);
            cov_pos = NORMALIZE_512(xstate.zmmregs2->hi16_zmm, 16, 1, cov_pos);

            last_struct_size = sizeof(struct avx_512_hi16_state);
            break;
        case XFEATURE_PT_UNIMPLEMENTED_SO_FAR:
            break; // Reserved bits
        case XFEATURE_PKRU: // struct pkru_state
            xstate.pkrureg = (struct pkru_state *)(((void *)regs) + feature_offset);
            cov_pos = NORMALIZE_32(&xstate.pkrureg->pkru, 1, 1, cov_pos);

            last_struct_size = sizeof(struct pkru_state);
            break;
        case XFEATURE_PASID:
            // struct ia32_pasid_state
            xstate.pasid = (struct ia32_pasid_state *)(((void *)regs) + feature_offset);
            cov_pos = NORMALIZE_64(&xstate.pasid->pasid, 1, 1, cov_pos);

            last_struct_size = sizeof(struct ia32_pasid_state);
            break;
        case XFEATURE_CET_USER:
            xstate.cet_user = (struct cet_user_state *)(((void *)regs) + feature_offset);
            cov_pos = NORMALIZE_64(&xstate.cet_user, 2, 1, cov_pos);

            last_struct_size = sizeof(struct cet_user_state);
            break;
        case XFEATURE_CET_KERNEL:
            xstate.cet_super = (struct cet_supervisor_state *)(((void *)regs) + feature_offset);
            cov_pos = NORMALIZE_64(&xstate.cet_super, 3, 1, cov_pos);

            last_struct_size = sizeof(struct cet_supervisor_state);
            break;
        case XFEATURE_RSRVD_COMP_13:
        case XFEATURE_RSRVD_COMP_14:
            break; // More reserved bits
        case XFEATURE_LBR:
            xstate.lbr = (struct arch_lbr_state *)(((void *)regs) + feature_offset);
            cov_pos = NORMALIZE_64(xstate.lbr, 5, 1, cov_pos);
            CHECK_PTR(cov_pos, EGENERIC);
            
            cov_pos = NORMALIZE_64(xstate.lbr->entries, xstate.lbr->lbr_depth * 3, 1, cov_pos);

            last_struct_size = sizeof(struct arch_lbr_state) + (xstate.lbr->lbr_depth * sizeof(struct lbr_entry));
            break;
        case XFEATURE_RSRVD_COMP_16:
            break; // More reserved bits
        case XFEATURE_XTILE_CFG:
            xstate.xtile_cfg = (struct xtile_cfg *)(((void *)regs) + feature_offset);
            xtile_cfg = xstate.xtile_cfg;
            cov_pos = NORMALIZE_64(xstate.xtile_cfg->tcfg, 8, 1, cov_pos);
            
            last_struct_size += sizeof(struct xtile_cfg);
            break;
        case XFEATURE_XTILE_DATA:
            xstate.tmmregs = (struct xtile_data *)(((void *)regs) + feature_offset);
            CHECK_PTR(xtile_cfg, EGENERIC);
            
            // NOTE: A bit unclear, but I think if compressed mode is enabled, only configured xtiles are saved
            //       otherwise they are all saved? We only ever deal with valid ones incase thats not the case
            for (int i=0; i < 8; i++) {
                if (!xtile_cfg->tcfg[i])
                    continue;

                // tmmregs can have more than one struct inside
                cov_pos = NORMALIZE_1024_BYTE(xstate.tmmregs[i].tmm.regbytes, 1, 1, cov_pos);
                CHECK_PTR(cov_pos, EGENERIC);

                last_struct_size += sizeof(struct xtile_data);
            }
            break;
        case XFEATURE_APX:
            xstate.apxregs = (struct apx_state *)(((void *)regs) + feature_offset);
            cov_pos = NORMALIZE_64(xstate.apxregs, 8, 1, cov_pos);

            last_struct_size += sizeof(struct apx_state);
            break;
        default:
            printf("xfeature unknown\n");
        }
        
        // Check to make sure we didnt invalidate the coverage pointer
        CHECK_PTR(cov_pos, EGENERIC);
    }

    return cov_pos - cov;
}

int normalize_gpr(struct user_regs_struct *regs, uint8_t *cov, size_t buf_size)
{
    uint8_t *cov_pos;

    if (!regs || !cov)
        return -1;
    
    if (sizeof(unsigned long long int) != sizeof(uint64_t))
        return -99;
    
    // This assumes a maximum factor of 2 is used
    if ((sizeof(uint64_t) * 26) * 2 > buf_size)
        return -1;

    cov_pos = NORMALIZE_64(regs, 26, 1, cov);
    CHECK_PTR(cov_pos, EGENERIC);
    
    return cov_pos - cov;
}

int normalize_perf(long long *stats, size_t num_stats, uint8_t *cov, size_t buf_size)
{
    uint8_t *cov_pos;

    if (!stats || !cov)
        return -1;

    if (sizeof(long long) != sizeof(uint64_t))
        return -99;
    
    // Assumes a maximum factor of 2 is used
    if ((sizeof(uint64_t) * num_stats) * 2 > buf_size)
        return -1;
    
    if (num_stats == 0) // Nothing to write
        return 0;

    cov_pos = NORMALIZE_64(stats, num_stats, 1, cov);
    CHECK_PTR(cov_pos, EGENERIC);

    return cov_pos - cov;
}

int print_score_diff(states_t *states, bool binary, bool log)
{
    char serial_fname[] = "/tmp/serial.XXXXXX";
    char orig_fname[] = "/tmp/original.XXXXXX";
    int fd1, fd2;
    FILE *f1, *f2;

    fd1 = mkstemp(serial_fname);
    CHECK_LIBC(fd1);
    fd2 = mkstemp(orig_fname);
    CHECK_LIBC(fd2);

    f1 = fdopen(fd1, "w+");
    f2 = fdopen(fd2, "w+");

    if (binary) {
        CHECK_LIBC(fwrite(&states->serial, sizeof(state_t), 1, f1));
        CHECK_LIBC(fwrite(&states->original, sizeof(state_t), 1, f2));
    } else {
        print_prstatus(&states->serial.gpr_state, f1);
        print_xstate((struct xregs_state *)states->serial.x_state.__padding, f1);
        print_prstatus(&states->original.gpr_state, f2);
        print_xstate((struct xregs_state *)states->original.x_state.__padding, f2);
    }
    
    fclose(f1);
    fclose(f2);

    char cmd[PAGE_SIZE];

    if (binary)
        snprintf(cmd, sizeof(cmd), "diff --color -u <(xxd %s) <(xxd %s)", serial_fname, orig_fname);
    else
        snprintf(cmd, sizeof(cmd), "diff --color -u %s %s", serial_fname, orig_fname);

    if (log)
        sprintf(cmd + strlen(cmd),  " >> results/default/oracle_finds/finds.log");

    CHECK_LIBC(system(cmd));

    unlink(serial_fname);
    unlink(orig_fname);

    return 0;
}

bool states_equal(state_t *state1, state_t *state2)
{
    // Here we need to explicitly ignore the xsave header, as sometimes fp/sse can differ between (known) equivalent snippets
    return memcmp(state1->x_state.__padding, state2->x_state.__padding, sizeof(xsave_buf_t)) == 0
        && memcmp(&state1->gpr_state, &state2->gpr_state, sizeof(struct user_regs_struct)) == 0
        && state1->final_sig == state2->final_sig 
        && state1->perf_count == state2->perf_count;
}

int64_t normalize_direct(config *cfg, states_t *states, uint8_t *coverage_buf, size_t buf_size)
{
    size_t buf_pos = 0;
    int new_bytes = 0;
    
    if (states->original.perf_count) {
        new_bytes = normalize_perf(states->original.perf, states->original.perf_count, coverage_buf + buf_pos, buf_size - buf_pos);
        CHECK_INT(new_bytes, EGENERIC);
        buf_pos += new_bytes;
    }

    new_bytes = normalize_gpr(&states->original.gpr_state, coverage_buf + buf_pos, buf_size - buf_pos);
    CHECK_INT(new_bytes, EGENERIC);
    buf_pos += new_bytes;

    new_bytes = normalize_xstate((struct xregs_state *)states->original.x_state.__padding, coverage_buf + buf_pos, buf_size - buf_pos);
    CHECK_INT(new_bytes, EGENERIC);
    buf_pos += new_bytes;

    return new_bytes;
}

int64_t normalize_bytewise(config *cfg, states_t *states, uint8_t *coverage_buf, size_t buf_size, size_t num_bytes)
{
    uint8_t *buf_pos = coverage_buf;
    
    // Is power of 2
    assert(((num_bytes != 0) && !(num_bytes & (num_bytes - 1))));
    
    // Results can be divided without remainder
    assert(sizeof(struct user_regs_struct) % num_bytes == 0);
    assert(states->original.xstate_sz % num_bytes == 0);

    if (states->original.perf_count) {
        buf_pos = normalize_bytes((uint8_t *)states->original.perf, states->original.perf_count * num_bytes, num_bytes, 1, buf_pos);
        CHECK_PTR(buf_pos, EGENERIC);
    }

    buf_pos = normalize_bytes((uint8_t *)&states->original.gpr_state, sizeof(struct user_regs_struct), num_bytes, 1, buf_pos);
    CHECK_PTR(buf_pos, EGENERIC);
    
    buf_pos = normalize_bytes((uint8_t *)&states->original.x_state.__padding, states->original.xstate_sz, num_bytes, 1, buf_pos);
    CHECK_PTR(buf_pos, EGENERIC);

    return buf_pos - coverage_buf;
}

// This normalizes score based on registers and perf counters, and encodes booleans into the coverage buffer
int encode_score(config *cfg, states_t *states, uint8_t *coverage_buf, size_t buf_size, uint8_t *test_case, size_t case_len, bool save)
{
    assert(sizeof(state_t) < buf_size);
    int64_t written;
    uint64_t header;
    
    // Double check, if one hit timer ignore the sample
    if (states->original.final_sig == SIGVTALRM || states->serial.final_sig == SIGVTALRM)
        return 0;
    
    //printf("Original rip at %p\n", (void *)states->original.gpr_state.rip);
    //printf("Serialized rip at %p\n", (void *)states->serial.gpr_state.rip);
    
    switch (cfg->encode_method) {
    case NORM_DIRECT:
        written = normalize_direct(cfg, states, coverage_buf, buf_size);
        break;
    case NORM_64_BYTE: // These can include some different stuff (e.g. metadata headers)
        written = normalize_bytewise(cfg, states, coverage_buf, buf_size, 8);  
        break;
    case NORM_32_BYTE:
        written = normalize_bytewise(cfg, states, coverage_buf, buf_size, 4); 
        break;
    case NORM_16_BYTE:
        written = normalize_bytewise(cfg, states, coverage_buf, buf_size, 2); 
        break;
    case NORM_8_BYTE:
        written = normalize_bytewise(cfg, states, coverage_buf, buf_size, 1); 
        break;
    default:
        return -1;
    }

    CHECK_INT(written, EGENERIC);
    
    states->original.gpr_state.rip = 0;
    states->serial.gpr_state.rip = 0;
    ((struct xregs_state *)states->original.x_state.__padding)->i387.rip = 0;
    ((struct xregs_state *)states->serial.x_state.__padding)->i387.rip = 0;
    
    // Always set these bits as we know them to always be included in the buffer
    // NOTE: On some systems FP/SSE activation is different between exectuons, this gets around that
    header = ((struct xregs_state *)states->serial.x_state.__padding)->header.xfeatures;
    ((struct xregs_state *)states->serial.x_state.__padding)->header.xfeatures = header | XFEATURE_MASK_FPSSE;
    
    header = ((struct xregs_state *)states->original.x_state.__padding)->header.xfeatures;
    ((struct xregs_state *)states->original.x_state.__padding)->header.xfeatures = header | XFEATURE_MASK_FPSSE;
    
    // NOTE: So for whatever reason segment register values are not stable between runs, 
    //       I want to include them in execution for possible effects but exclude them 
    //       from scoring because they generate too many false positives to allow long runs. 
    //       I have enough samples already :)
    if (cfg->ignore_segment) {
        states->original.gpr_state.fs = 0;
        states->original.gpr_state.ds = 0;
        states->original.gpr_state.es = 0;
        states->original.gpr_state.gs = 0;

        states->serial.gpr_state.fs = 0;
        states->serial.gpr_state.ds = 0;
        states->serial.gpr_state.es = 0;
        states->serial.gpr_state.gs = 0;
    }
    
    //print_buffer(coverage_buf, written, stdout);
    
    // Now we compare all the bytes to see if this is worthy of being saved
    if (!states_equal(&states->original, &states->serial)) {
        if (bloom_check_add(&cfg->finds, test_case, case_len))
            return 0;

        if (save)
            CHECK_INT(dump_snippet(NULL, test_case, case_len, "oracle_finds", false, false, 0, 0), EGENERIC);
        
        // Some logging
        print_score_diff(states, false, true);
        return 1; // Return one when a new find was saved
    }

    return 0;
}

