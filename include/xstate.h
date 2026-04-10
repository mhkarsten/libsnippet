#ifndef XSTATE_H
#define XSTATE_H

#include <stdint.h>
#include <sys/user.h>

// NOTE: The structs here are taken from the linux kernel, as there is not really a userspace equivalent

struct user_ymmh_regs {
	/* 16 * 16 bytes for each YMMH-reg */
	uint32_t ymmh_space[64];
};

struct user_xstate_header {
	uint64_t xfeatures;
	uint64_t reserved1[2];
	uint64_t reserved2[5];
};

typedef union xsave_ptrs_ {
    struct fxregs_state             *fxregs;
    struct ymmh_struct              *ymmregs;
    struct mpx_bndcsr_state         *bndcsr;
    struct mpx_bndreg_state         *bndregs;
    struct avx_512_opmask_state     *kregs;
    struct pkru_state               *pkrureg;
    struct avx_512_zmm_uppers_state *zmmregs1;
    struct avx_512_hi16_state       *zmmregs2;
    struct ia32_pasid_state         *pasid;
    struct cet_user_state           *cet_user;
    struct cet_supervisor_state     *cet_super;
    struct arch_lbr_state           *lbr;
    struct xtile_cfg                *xtile_cfg;
    struct xtile_data               *tmmregs;
    struct apx_state                *apxregs;
} xsave_ptrs_t;

/*
 * The structure layout of user_xstateregs, used for exporting the
 * extended register state through ptrace and core-dump (NT_X86_XSTATE note)
 * interfaces will be same as the memory layout of xsave used by the processor
 * (except for the bytes 464..511, which can be used by the software) and hence
 * the size of this structure varies depending on the features supported by the
 * processor and OS. The size of the structure that users need to use can be
 * obtained by doing:
 *     cpuid_count(0xd, 0, &eax, &ptrace_xstateregs_struct_size, &ecx, &edx);
 * i.e., cpuid.(eax=0xd,ecx=0).ebx will be the size that user (debuggers, etc.)
 * need to use.
 *
 * For now, only the first 8 bytes of the software usable bytes[464..471] will
 * be used and will be set to OS enabled xstate mask (which is same as the
 * 64bit mask returned by the xgetbv's xCR0).  Users (analyzing core dump
 * remotely, etc.) can use this mask as well as the mask saved in the
 * xstate_hdr bytes and interpret what states the processor/OS supports
 * and what states are in modified/initialized conditions for the
 * particular process/thread.
 *
 * Also when the user modifies certain state FP/SSE/etc through the
 * ptrace interface, they must ensure that the header.xfeatures
 * bytes[512..519] of the memory layout are updated correspondingly.
 * i.e., for example when FP state is modified to a non-init state,
 * header.xfeatures's bit 0 must be set to '1', when SSE is modified to
 * non-init state, header.xfeatures's bit 1 must to be set to '1', etc.
 */
#define USER_XSTATE_FX_SW_WORDS 6
#define USER_XSTATE_XCR0_WORD	0

struct user_xstateregs {
	struct {
		uint64_t fpx_space[58];
		uint64_t xstate_fx_sw[USER_XSTATE_FX_SW_WORDS];
	} i387;
	struct user_xstate_header header;
	struct user_ymmh_regs ymmh;
	/* further processor state extensions go here */
};

/*
 * The legacy x87 FPU state format, as saved by FSAVE and
 * restored by the FRSTOR instructions:
 */
struct fregs_state {
	uint32_t			cwd;	/* FPU Control Word		*/
	uint32_t			swd;	/* FPU Status Word		*/
	uint32_t			twd;	/* FPU Tag Word			*/
	uint32_t			fip;	/* FPU IP Offset		*/
	uint32_t			fcs;	/* FPU IP Selector		*/
	uint32_t			foo;	/* FPU Operand Pointer Offset	*/
	uint32_t			fos;	/* FPU Operand Pointer Selector	*/

	/* 8*10 bytes for each FP-reg = 80 bytes:			*/
	uint32_t			st_space[20];

	/* Software status information [not touched by FSAVE]:		*/
	uint32_t			status;
};

/*
 * The legacy fx SSE/MMX FPU state format, as saved by FXSAVE and
 * restored by the FXRSTOR instructions. It's similar to the FSAVE
 * format, but differs in some areas, plus has extensions at
 * the end for the XMM registers.
 */
struct fxregs_state {
	uint16_t			cwd; /* Control Word			*/
	uint16_t			swd; /* Status Word			*/
	uint16_t			twd; /* Tag Word			*/
	uint16_t			fop; /* Last Instruction Opcode		*/
	union {
		struct {
			uint64_t	rip; /* Instruction Pointer		*/
			uint64_t	rdp; /* Data Pointer			*/
		};
		struct {
			uint32_t	fip; /* FPU IP Offset			*/
			uint32_t	fcs; /* FPU IP Selector			*/
			uint32_t	foo; /* FPU Operand Offset		*/
			uint32_t	fos; /* FPU Operand Selector		*/
		};
	};
	uint32_t			mxcsr;		/* MXCSR Register State */
	uint32_t			mxcsr_mask;	/* MXCSR Mask		*/

	/* 8*16 bytes for each FP-reg = 128 bytes:			*/
	uint32_t			st_space[32];

	/* 16*16 bytes for each XMM-reg = 256 bytes:			*/
	uint32_t			xmm_space[64];

	uint32_t			padding[12];

	union {
		uint32_t		padding1[12];
		uint32_t		sw_reserved[12];
	};

} __attribute__((aligned(16)));

/* Default value for fxregs_state.mxcsr: */
#define MXCSR_DEFAULT		0x1f80

/* Copy both mxcsr & mxcsr_flags with a single uint64_t memcpy: */
#define MXCSR_AND_FLAGS_SIZE sizeof(uint64_t)

/*
 * Software based FPU emulation state. This is arbitrary really,
 * it matches the x87 format to make it easier to understand:
 */
struct swregs_state {
	uint32_t			cwd;
	uint32_t			swd;
	uint32_t			twd;
	uint32_t			fip;
	uint32_t			fcs;
	uint32_t			foo;
	uint32_t			fos;
	/* 8*10 bytes for each FP-reg = 80 bytes: */
	uint32_t			st_space[20];
	uint8_t			ftop;
	uint8_t			changed;
	uint8_t			lookahead;
	uint8_t			no_update;
	uint8_t			rm;
	uint8_t			alimit;
	struct math_emu_info	*info;
	uint32_t			entry_eip;
};

/*
 * List of XSAVE features Linux knows about:
 */
enum xfeature {
	XFEATURE_FP,
	XFEATURE_SSE,
	/*
	 * Values above here are "legacy states".
	 * Those below are "extended states".
	 */
	XFEATURE_YMM,
	XFEATURE_BNDREGS,
	XFEATURE_BNDCSR,
	XFEATURE_OPMASK,
	XFEATURE_ZMM_Hi256,
	XFEATURE_Hi16_ZMM,
	XFEATURE_PT_UNIMPLEMENTED_SO_FAR,
	XFEATURE_PKRU,
	XFEATURE_PASID,
	XFEATURE_CET_USER,
	XFEATURE_CET_KERNEL,
	XFEATURE_RSRVD_COMP_13,
	XFEATURE_RSRVD_COMP_14,
	XFEATURE_LBR,
	XFEATURE_RSRVD_COMP_16,
	XFEATURE_XTILE_CFG,
	XFEATURE_XTILE_DATA,
	XFEATURE_APX,

	XFEATURE_MAX,
};

#define XFEATURE_MASK_FP		    (1 << XFEATURE_FP)
#define XFEATURE_MASK_SSE		    (1 << XFEATURE_SSE)
#define XFEATURE_MASK_YMM		    (1 << XFEATURE_YMM)
#define XFEATURE_MASK_BNDREGS		(1 << XFEATURE_BNDREGS)
#define XFEATURE_MASK_BNDCSR		(1 << XFEATURE_BNDCSR)
#define XFEATURE_MASK_OPMASK		(1 << XFEATURE_OPMASK)
#define XFEATURE_MASK_ZMM_Hi256		(1 << XFEATURE_ZMM_Hi256)
#define XFEATURE_MASK_Hi16_ZMM		(1 << XFEATURE_Hi16_ZMM)
#define XFEATURE_MASK_PT		    (1 << XFEATURE_PT_UNIMPLEMENTED_SO_FAR)
#define XFEATURE_MASK_PKRU		    (1 << XFEATURE_PKRU)
#define XFEATURE_MASK_PASID		    (1 << XFEATURE_PASID)
#define XFEATURE_MASK_CET_USER		(1 << XFEATURE_CET_USER)
#define XFEATURE_MASK_CET_KERNEL	(1 << XFEATURE_CET_KERNEL)
#define XFEATURE_MASK_LBR		    (1 << XFEATURE_LBR)
#define XFEATURE_MASK_XTILE_CFG		(1 << XFEATURE_XTILE_CFG)
#define XFEATURE_MASK_XTILE_DATA	(1 << XFEATURE_XTILE_DATA)
#define XFEATURE_MASK_APX		    (1 << XFEATURE_APX)

#define XFEATURE_MASK_FPSSE		    (XFEATURE_MASK_FP | XFEATURE_MASK_SSE)
#define XFEATURE_MASK_AVX512		(XFEATURE_MASK_OPMASK | XFEATURE_MASK_ZMM_Hi256 | XFEATURE_MASK_Hi16_ZMM)

// Assumed to be running in an x86_64 context
#define XFEATURE_MASK_XTILE		(XFEATURE_MASK_XTILE_DATA | XFEATURE_MASK_XTILE_CFG)


#define FIRST_EXTENDED_XFEATURE	XFEATURE_YMM

struct reg_128_bit {
	uint8_t      regbytes[128/8];
};
struct reg_256_bit {
	uint8_t	regbytes[256/8];
};
struct reg_512_bit {
	uint8_t	regbytes[512/8];
};
struct reg_1024_byte {
	uint8_t	regbytes[1024];
};

/*
 * State component 2:
 *
 * There are 16x 256-bit AVX registers named YMM0-YMM15.
 * The low 128 bits are aliased to the 16 SSE registers (XMM0-XMM15)
 * and are stored in 'struct fxregs_state::xmm_space[]' in the
 * "legacy" area.
 *
 * The high 128 bits are stored here.
 */
struct ymmh_struct {
	struct reg_128_bit              hi_ymm[16];
} __attribute__((packed));

/* Intel MPX support: */

struct mpx_bndreg {
	uint64_t				lower_bound;
	uint64_t				upper_bound;
} __attribute__((packed));
/*
 * State component 3 is used for the 4 128-bit bounds registers
 */
struct mpx_bndreg_state {
	struct mpx_bndreg		bndreg[4];
} __attribute__((packed));

/*
 * State component 4 is used for the 64-bit user-mode MPX
 * configuration register BNDCFGU and the 64-bit MPX status
 * register BNDSTATUS.  We call the pair "BNDCSR".
 */
struct mpx_bndcsr {
	uint64_t				bndcfgu;
	uint64_t				bndstatus;
} __attribute__((packed));

/*
 * The BNDCSR state is padded out to be 64-bytes in size.
 */
struct mpx_bndcsr_state {
	union {
		struct mpx_bndcsr		bndcsr;
		uint8_t				pad_to_64_bytes[64];
	};
} __attribute__((packed));

/* AVX-512 Components: */

/*
 * State component 5 is used for the 8 64-bit opmask registers
 * k0-k7 (opmask state).
 */
struct avx_512_opmask_state {
	uint64_t				opmask_reg[8];
} __attribute__((packed));

/*
 * State component 6 is used for the upper 256 bits of the
 * registers ZMM0-ZMM15. These 16 256-bit values are denoted
 * ZMM0_H-ZMM15_H (ZMM_Hi256 state).
 */
struct avx_512_zmm_uppers_state {
	struct reg_256_bit		zmm_upper[16];
} __attribute__((packed));

/*
 * State component 7 is used for the 16 512-bit registers
 * ZMM16-ZMM31 (Hi16_ZMM state).
 */
struct avx_512_hi16_state {
	struct reg_512_bit		hi16_zmm[16];
} __attribute__((packed));

/*
 * State component 9: 32-bit PKRU register.  The state is
 * 8 bytes long but only 4 bytes is used currently.
 */
struct pkru_state {
	uint32_t				pkru;
	uint32_t				pad;
} __attribute__((packed));

/*
 * State component 11 is Control-flow Enforcement user states
 */
struct cet_user_state {
	/* user control-flow settings */
	uint64_t user_cet;
	/* user shadow stack pointer */
	uint64_t user_ssp;
};

/*
 * State component 12 is Control-flow Enforcement supervisor states.
 * This state includes SSP pointers for privilege levels 0 through 2.
 */
struct cet_supervisor_state {
	uint64_t pl0_ssp;
	uint64_t pl1_ssp;
	uint64_t pl2_ssp;
} __attribute__((packed));

/*
 * State component 15: Architectural LBR configuration state.
 * The size of Arch LBR state depends on the number of LBRs (lbr_depth).
 */

struct lbr_entry {
	uint64_t from;
	uint64_t to;
	uint64_t info;
};

struct arch_lbr_state {
	uint64_t lbr_ctl;
	uint64_t lbr_depth;
	uint64_t ler_from;
	uint64_t ler_to;
	uint64_t ler_info;
	struct lbr_entry		entries[];
};

/*
 * State component 17: 64-byte tile configuration register.
 */
struct xtile_cfg {
	uint64_t				tcfg[8];
} __attribute__((packed));

/*
 * State component 18: 1KB tile data register.
 * Each register represents 16 64-byte rows of the matrix
 * data. But the number of registers depends on the actual
 * implementation.
 */
struct xtile_data {
	struct reg_1024_byte		tmm;
} __attribute__((packed));

/*
 * State component 19: 8B extended general purpose register.
 */
struct apx_state {
	uint64_t				egpr[16];
} __attribute__((packed));

/*
 * State component 10 is supervisor state used for context-switching the
 * PASID state.
 */
struct ia32_pasid_state {
	uint64_t pasid;
} __attribute__((packed));

struct xstate_header {
	uint64_t				xfeatures;
	uint64_t				xcomp_bv;
	uint64_t				reserved[6];
} __attribute__((packed));

/*
 * xstate_header.xcomp_bv[63] indicates that the extended_state_area
 * is in compacted format.
 */
#define XCOMP_BV_COMPACTED_FORMAT ((uint64_t)1 << 63)

/*
 * This is our most modern FPU state format, as saved by the XSAVE
 * and restored by the XRSTOR instructions.
 *
 * It consists of a legacy fxregs portion, an xstate header and
 * subsequent areas as defined by the xstate header.  Not all CPUs
 * support all the extensions, so the size of the extended area
 * can vary quite a bit between CPUs.
 */
struct xregs_state {
	struct fxregs_state		i387;
	struct xstate_header	header;
	uint8_t				    extended_state_area[];
} __attribute__ ((packed, aligned (64)));

/*
 * This is a union of all the possible FPU state formats
 * put together, so that we can pick the right one runtime.
 *
 * The size of the structure is determined by the largest
 * member - which is the xsave area.  The padding is there
 * to ensure that statically-allocated task_structs (just
 * the init_task today) have enough space.
 */
union fpregs_state {
	struct fregs_state		fsave;
	struct fxregs_state		fxsave;
	struct swregs_state		soft;
	struct xregs_state		xsave;
	uint8_t __padding[PAGE_SIZE];
};
#endif
