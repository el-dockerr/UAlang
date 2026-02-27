/*
 * =============================================================================
 *  UA - Unified Assembler
 *  ARM Back-End (Code Generation)
 *
 *  File:    backend_arm.c
 *  Purpose: Translate UA IR into raw ARM (ARMv7-A, 32-bit) machine code.
 *
 *  ┌──────────────────────────────────────────────────────────────────────┐
 *  │  ARM Instruction Encoding Reference (ARMv7-A, ARM mode)           │
 *  │                                                                    │
 *  │  All instructions are 32 bits, little-endian.                      │
 *  │  Condition field: bits [31:28] = 0xE (AL = always execute)         │
 *  │                                                                    │
 *  │  Data Processing (register):                                       │
 *  │    cond 00 I opcode S Rn Rd operand2                               │
 *  │    bits: [31:28] [27:26] [25] [24:21] [20] [19:16] [15:12] [11:0] │
 *  │                                                                    │
 *  │  Opcodes (bits [24:21]):                                           │
 *  │    AND=0x0  EOR=0x1  SUB=0x2  RSB=0x3  ADD=0x4  ADC=0x5           │
 *  │    SBC=0x6  RSC=0x7  TST=0x8  TEQ=0x9  CMP=0xA  CMN=0xB           │
 *  │    ORR=0xC  MOV=0xD  BIC=0xE  MVN=0xF                             │
 *  │                                                                    │
 *  │  LDR Rd, [Rn]:       cond 01 I P U B W L Rn Rd offset12           │
 *  │  STR Rd, [Rn]:       cond 01 I P U B W L Rn Rd offset12           │
 *  │                                                                    │
 *  │  Branch:              cond 101 L offset24 (signed, <<2, PC-rel)    │
 *  │    B   = L=0          BL = L=1                                     │
 *  │    BEQ: cond=0x0      BNE: cond=0x1                               │
 *  │                                                                    │
 *  │  BX Rm:              cond 0001 0010 1111 1111 1111 0001 Rm          │
 *  │  MUL Rd, Rm, Rs:     cond 000 0000 S Rd 0000 Rs 1001 Rm            │
 *  │  SDIV Rd, Rn, Rm:    cond 0111 0001 Rd 1111 Rm 0001 Rn (ARMv7VE) │
 *  │  MOVW Rd, #imm16:    cond 0011 0000 imm4 Rd imm12                 │
 *  │  MOVT Rd, #imm16:    cond 0011 0100 imm4 Rd imm12                 │
 *  │  SVC #imm24:         cond 1111 imm24                               │
 *  │  PUSH {Rd}:          cond 0101 0010 1101 Rd 0000 0000 0100         │
 *  │  POP  {Rd}:          cond 0100 1001 1101 Rd 0000 0000 0100         │
 *  │  NOP:                cond 0011 0010 0000 1111 0000 0000 0000       │
 *  │                                                                    │
 *  │  UA registers R0-R7 map directly to ARM r0-r7.                     │
 *  │  R8-R15 are rejected (r13=SP, r14=LR, r15=PC are special).        │
 *  └──────────────────────────────────────────────────────────────────────┘
 *
 *  License: MIT
 * =============================================================================
 */

#include "backend_arm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 *  ARM register encoding table
 * =========================================================================
 *  Index = UA register number,  Value = ARM register encoding.
 *  R0-R7 map directly.  R8-R15 rejected (R13=SP, R14=LR, R15=PC).
 * ========================================================================= */
#define ARM_MAX_REG  8
#define ARM_REG_FP   11    /* r11 — used as scratch for SET imm  */
#define ARM_REG_IP   12    /* r12 — intra-procedure scratch register */
#define ARM_REG_SP   13
#define ARM_REG_LR   14

static const uint8_t ARM_REG_ENC[ARM_MAX_REG] = {
    0, 1, 2, 3, 4, 5, 6, 7
};

static const char* ARM_REG_NAME[ARM_MAX_REG] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7"
};

/* =========================================================================
 *  ARM condition codes  (bits [31:28])
 * ========================================================================= */
#define ARM_COND_EQ  0x0   /* Equal (Z set)           */
#define ARM_COND_NE  0x1   /* Not equal (Z clear)     */
#define ARM_COND_AL  0xE   /* Always                  */
#define ARM_COND_LT  0xB   /* Signed less than (N!=V) */
#define ARM_COND_GT  0xC   /* Signed greater than (Z==0 && N==V) */

/* =========================================================================
 *  Error helpers
 * ========================================================================= */
static void arm_error(const Instruction *inst, const char *msg)
{
    fprintf(stderr,
            "\n"
            "  UA ARM Backend Error\n"
            "  ---------------------\n"
            "  Line %d, Column %d: %s\n\n",
            inst->line, inst->column, msg);
    exit(1);
}

static void arm_validate_register(const Instruction *inst, int reg)
{
    if (reg < 0 || reg >= ARM_MAX_REG) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "register R%d is not mapped in the ARM backend "
                 "(supports R0-R7: r0-r7)",
                 reg);
        arm_error(inst, msg);
    }
}

/* =========================================================================
 *  Emit helpers  —  emit a 32-bit ARM instruction (little-endian)
 * ========================================================================= */
static void emit_arm32(CodeBuffer *buf, uint32_t word)
{
    emit_byte(buf, (uint8_t)( word        & 0xFF));
    emit_byte(buf, (uint8_t)((word >>  8) & 0xFF));
    emit_byte(buf, (uint8_t)((word >> 16) & 0xFF));
    emit_byte(buf, (uint8_t)((word >> 24) & 0xFF));
}

/* =========================================================================
 *  ARM Data Processing instruction builders
 *
 *  Format:  cond | 00 | I | opcode | S | Rn | Rd | operand2
 *           [31:28] [27:26] [25] [24:21] [20] [19:16] [15:12] [11:0]
 * ========================================================================= */

/* Data processing: register operand (I=0, operand2 = Rm) */
static uint32_t arm_dp_reg(uint8_t cond, uint8_t opcode, uint8_t s,
                            uint8_t rn, uint8_t rd, uint8_t rm)
{
    return ((uint32_t)cond << 28)
         | (0u << 26)           /* 00 */
         | (0u << 25)           /* I=0 (register) */
         | ((uint32_t)opcode << 21)
         | ((uint32_t)s << 20)
         | ((uint32_t)rn << 16)
         | ((uint32_t)rd << 12)
         | (uint32_t)rm;
}

/* Data processing: immediate operand (I=1, operand2 = rotate_imm:imm8) */
static uint32_t arm_dp_imm(uint8_t cond, uint8_t opcode, uint8_t s,
                            uint8_t rn, uint8_t rd,
                            uint8_t rotate, uint8_t imm8)
{
    return ((uint32_t)cond << 28)
         | (0u << 26)
         | (1u << 25)           /* I=1 (immediate) */
         | ((uint32_t)opcode << 21)
         | ((uint32_t)s << 20)
         | ((uint32_t)rn << 16)
         | ((uint32_t)rd << 12)
         | ((uint32_t)(rotate & 0xF) << 8)
         | (uint32_t)imm8;
}

/* Data processing: register with shift (I=0, operand2 = shift|Rm) */
static uint32_t arm_dp_reg_shift_imm(uint8_t cond, uint8_t opcode, uint8_t s,
                                      uint8_t rn, uint8_t rd, uint8_t rm,
                                      uint8_t shift_type, uint8_t shift_imm)
{
    /* operand2: shift_imm[11:7] | shift_type[6:5] | 0[4] | Rm[3:0] */
    uint32_t operand2 = ((uint32_t)(shift_imm & 0x1F) << 7)
                       | ((uint32_t)(shift_type & 0x3) << 5)
                       | (uint32_t)rm;
    return ((uint32_t)cond << 28)
         | (0u << 26)
         | (0u << 25)
         | ((uint32_t)opcode << 21)
         | ((uint32_t)s << 20)
         | ((uint32_t)rn << 16)
         | ((uint32_t)rd << 12)
         | operand2;
}

/* Data processing: register-shifted register (I=0) */
static uint32_t arm_dp_reg_shift_reg(uint8_t cond, uint8_t opcode, uint8_t s,
                                      uint8_t rn, uint8_t rd, uint8_t rm,
                                      uint8_t shift_type, uint8_t rs)
{
    /* operand2: Rs[11:8] | 0[7] | shift_type[6:5] | 1[4] | Rm[3:0] */
    uint32_t operand2 = ((uint32_t)rs << 8)
                       | ((uint32_t)(shift_type & 0x3) << 5)
                       | (1u << 4)
                       | (uint32_t)rm;
    return ((uint32_t)cond << 28)
         | (0u << 26)
         | (0u << 25)
         | ((uint32_t)opcode << 21)
         | ((uint32_t)s << 20)
         | ((uint32_t)rn << 16)
         | ((uint32_t)rd << 12)
         | operand2;
}

/* Shift types */
#define ARM_SHIFT_LSL  0
#define ARM_SHIFT_LSR  1
#define ARM_SHIFT_ASR  2
#define ARM_SHIFT_ROR  3

/* Data processing opcode constants */
#define ARM_DP_AND  0x0
#define ARM_DP_EOR  0x1
#define ARM_DP_SUB  0x2
#define ARM_DP_ADD  0x4
#define ARM_DP_CMP  0xA
#define ARM_DP_ORR  0xC
#define ARM_DP_MOV  0xD
#define ARM_DP_MVN  0xF

/* =========================================================================
 *  High-level emit functions
 * ========================================================================= */

/* --- MOV Rd, Rm -------------------------------------------------------- */
static void emit_arm_mov_reg(CodeBuffer *buf, uint8_t rd, uint8_t rm)
{
    emit_arm32(buf, arm_dp_reg(ARM_COND_AL, ARM_DP_MOV, 0, 0, rd, rm));
}

/* --- MOVW Rd, #imm16  (ARMv6T2/ARMv7) -------------------------------- */
static void emit_arm_movw(CodeBuffer *buf, uint8_t rd, uint16_t imm16)
{
    /* Encoding: cond 0011 0000 imm4 Rd imm12 */
    uint32_t imm4  = (imm16 >> 12) & 0xF;
    uint32_t imm12 = imm16 & 0xFFF;
    uint32_t word = ((uint32_t)ARM_COND_AL << 28)
                  | (0x03u << 24)       /* 0011 */
                  | (0x00u << 20)       /* 0000 */
                  | (imm4 << 16)
                  | ((uint32_t)rd << 12)
                  | imm12;
    emit_arm32(buf, word);
}

/* --- MOVT Rd, #imm16  (ARMv6T2/ARMv7) -------------------------------- */
static void emit_arm_movt(CodeBuffer *buf, uint8_t rd, uint16_t imm16)
{
    /* Encoding: cond 0011 0100 imm4 Rd imm12 */
    uint32_t imm4  = (imm16 >> 12) & 0xF;
    uint32_t imm12 = imm16 & 0xFFF;
    uint32_t word = ((uint32_t)ARM_COND_AL << 28)
                  | (0x03u << 24)
                  | (0x04u << 20)       /* 0100 */
                  | (imm4 << 16)
                  | ((uint32_t)rd << 12)
                  | imm12;
    emit_arm32(buf, word);
}

/* --- Load a full 32-bit immediate into Rd ------------------------------ */
/*     Uses MOVW for low 16 bits, MOVT for high 16 bits if needed.        */
static void emit_arm_load_imm32(CodeBuffer *buf, uint8_t rd, int32_t imm)
{
    uint32_t val = (uint32_t)imm;
    emit_arm_movw(buf, rd, (uint16_t)(val & 0xFFFF));
    if ((val >> 16) != 0) {
        emit_arm_movt(buf, rd, (uint16_t)((val >> 16) & 0xFFFF));
    }
}

/* Always emit both MOVW + MOVT (8 bytes) — used for variable addresses
 * where a fixed instruction size is needed for pass-1 sizing. */
static void emit_arm_load_imm32_full(CodeBuffer *buf, uint8_t rd, int32_t imm)
{
    uint32_t val = (uint32_t)imm;
    emit_arm_movw(buf, rd, (uint16_t)(val & 0xFFFF));
    emit_arm_movt(buf, rd, (uint16_t)((val >> 16) & 0xFFFF));
}

/* --- ADD Rd, Rn, Rm ---------------------------------------------------- */
static void emit_arm_add_reg(CodeBuffer *buf, uint8_t rd, uint8_t rn,
                              uint8_t rm)
{
    emit_arm32(buf, arm_dp_reg(ARM_COND_AL, ARM_DP_ADD, 0, rn, rd, rm));
}

/* --- ADD Rd, Rn, #imm8 ------------------------------------------------ */
static void emit_arm_add_imm(CodeBuffer *buf, uint8_t rd, uint8_t rn,
                              uint8_t imm8)
{
    emit_arm32(buf, arm_dp_imm(ARM_COND_AL, ARM_DP_ADD, 0, rn, rd, 0, imm8));
}

/* --- SUB Rd, Rn, Rm ---------------------------------------------------- */
static void emit_arm_sub_reg(CodeBuffer *buf, uint8_t rd, uint8_t rn,
                              uint8_t rm)
{
    emit_arm32(buf, arm_dp_reg(ARM_COND_AL, ARM_DP_SUB, 0, rn, rd, rm));
}

/* --- SUB Rd, Rn, #imm8 ------------------------------------------------ */
static void emit_arm_sub_imm(CodeBuffer *buf, uint8_t rd, uint8_t rn,
                              uint8_t imm8)
{
    emit_arm32(buf, arm_dp_imm(ARM_COND_AL, ARM_DP_SUB, 0, rn, rd, 0, imm8));
}

/* --- AND Rd, Rn, Rm ---------------------------------------------------- */
static void emit_arm_and_reg(CodeBuffer *buf, uint8_t rd, uint8_t rn,
                              uint8_t rm)
{
    emit_arm32(buf, arm_dp_reg(ARM_COND_AL, ARM_DP_AND, 0, rn, rd, rm));
}

/* --- ORR Rd, Rn, Rm ---------------------------------------------------- */
static void emit_arm_orr_reg(CodeBuffer *buf, uint8_t rd, uint8_t rn,
                              uint8_t rm)
{
    emit_arm32(buf, arm_dp_reg(ARM_COND_AL, ARM_DP_ORR, 0, rn, rd, rm));
}

/* --- EOR Rd, Rn, Rm ---------------------------------------------------- */
static void emit_arm_eor_reg(CodeBuffer *buf, uint8_t rd, uint8_t rn,
                              uint8_t rm)
{
    emit_arm32(buf, arm_dp_reg(ARM_COND_AL, ARM_DP_EOR, 0, rn, rd, rm));
}

/* --- MVN Rd, Rm  (bitwise NOT) ----------------------------------------- */
static void emit_arm_mvn_reg(CodeBuffer *buf, uint8_t rd, uint8_t rm)
{
    emit_arm32(buf, arm_dp_reg(ARM_COND_AL, ARM_DP_MVN, 0, 0, rd, rm));
}

/* --- CMP Rn, Rm  (sets flags, S=1, Rd=0) ------------------------------ */
static void emit_arm_cmp_reg(CodeBuffer *buf, uint8_t rn, uint8_t rm)
{
    emit_arm32(buf, arm_dp_reg(ARM_COND_AL, ARM_DP_CMP, 1, rn, 0, rm));
}

/* --- CMP Rn, #imm8  (sets flags, S=1, Rd=0) --------------------------- */
static void emit_arm_cmp_imm(CodeBuffer *buf, uint8_t rn,
                              uint8_t rotate, uint8_t imm8)
{
    emit_arm32(buf, arm_dp_imm(ARM_COND_AL, ARM_DP_CMP, 1, rn, 0,
                                rotate, imm8));
}

/* --- LSL Rd, Rm, #shift_imm (MOV with barrel shift) -------------------- */
static void emit_arm_lsl_imm(CodeBuffer *buf, uint8_t rd, uint8_t rm,
                              uint8_t shift_imm)
{
    emit_arm32(buf, arm_dp_reg_shift_imm(ARM_COND_AL, ARM_DP_MOV, 0,
                                          0, rd, rm, ARM_SHIFT_LSL, shift_imm));
}

/* --- LSR Rd, Rm, #shift_imm ------------------------------------------- */
static void emit_arm_lsr_imm(CodeBuffer *buf, uint8_t rd, uint8_t rm,
                              uint8_t shift_imm)
{
    emit_arm32(buf, arm_dp_reg_shift_imm(ARM_COND_AL, ARM_DP_MOV, 0,
                                          0, rd, rm, ARM_SHIFT_LSR, shift_imm));
}

/* --- LSL Rd, Rm, Rs  (register-shifted register) ----------------------- */
static void emit_arm_lsl_reg(CodeBuffer *buf, uint8_t rd, uint8_t rm,
                              uint8_t rs)
{
    emit_arm32(buf, arm_dp_reg_shift_reg(ARM_COND_AL, ARM_DP_MOV, 0,
                                          0, rd, rm, ARM_SHIFT_LSL, rs));
}

/* --- LSR Rd, Rm, Rs  (register-shifted register) ----------------------- */
static void emit_arm_lsr_reg(CodeBuffer *buf, uint8_t rd, uint8_t rm,
                              uint8_t rs)
{
    emit_arm32(buf, arm_dp_reg_shift_reg(ARM_COND_AL, ARM_DP_MOV, 0,
                                          0, rd, rm, ARM_SHIFT_LSR, rs));
}

/* --- MUL Rd, Rm, Rs ---------------------------------------------------- */
/*     cond 000 0000 S Rd 0000 Rs 1001 Rm                                  */
static void emit_arm_mul(CodeBuffer *buf, uint8_t rd, uint8_t rm,
                          uint8_t rs)
{
    uint32_t word = ((uint32_t)ARM_COND_AL << 28)
                  | ((uint32_t)rd << 16)
                  | ((uint32_t)rs << 8)
                  | (0x9u << 4)     /* 1001 */
                  | (uint32_t)rm;
    emit_arm32(buf, word);
}

/* --- SDIV Rd, Rn, Rm  (ARMv7VE / -A with IDIV extension) -------------- */
/*     cond 0111 0001 Rd 1111 Rm 0001 Rn                                    */
static void emit_arm_sdiv(CodeBuffer *buf, uint8_t rd, uint8_t rn,
                           uint8_t rm)
{
    uint32_t word = ((uint32_t)ARM_COND_AL << 28)
                  | (0x71u << 20)       /* 0111 0001 */
                  | ((uint32_t)rd << 16)
                  | (0xFu << 12)        /* 1111 */
                  | ((uint32_t)rm << 8)
                  | (0x1u << 4)         /* 0001 */
                  | (uint32_t)rn;
    emit_arm32(buf, word);
}

/* --- LDR Rd, [Rn]  (offset=0, P=1, U=1, B=0, W=0, L=1) --------------- */
/*     cond 01 0 P U B W L Rn Rd offset12                                  */
static void emit_arm_ldr(CodeBuffer *buf, uint8_t rd, uint8_t rn)
{
    uint32_t word = ((uint32_t)ARM_COND_AL << 28)
                  | (0x01u << 26)       /* 01 */
                  | (1u << 24)          /* P=1 (pre-indexed) */
                  | (1u << 23)          /* U=1 (add offset) */
                  | (0u << 22)          /* B=0 (word) */
                  | (0u << 21)          /* W=0 (no writeback) */
                  | (1u << 20)          /* L=1 (load) */
                  | ((uint32_t)rn << 16)
                  | ((uint32_t)rd << 12)
                  | 0;                  /* offset12 = 0 */
    emit_arm32(buf, word);
}

/* --- STR Rd, [Rn]  (offset=0, P=1, U=1, B=0, W=0, L=0) --------------- */
static void emit_arm_str(CodeBuffer *buf, uint8_t rd, uint8_t rn)
{
    uint32_t word = ((uint32_t)ARM_COND_AL << 28)
                  | (0x01u << 26)
                  | (1u << 24)          /* P=1 */
                  | (1u << 23)          /* U=1 */
                  | (0u << 22)          /* B=0 */
                  | (0u << 21)          /* W=0 */
                  | (0u << 20)          /* L=0 (store) */
                  | ((uint32_t)rn << 16)
                  | ((uint32_t)rd << 12)
                  | 0;
    emit_arm32(buf, word);
}

/* --- BX Rm  (branch and exchange, used for RET via BX LR) ------------- */
/*     cond 0001 0010 1111 1111 1111 0001 Rm                                */
static void emit_arm_bx(CodeBuffer *buf, uint8_t rm)
{
    uint32_t word = ((uint32_t)ARM_COND_AL << 28)
                  | (0x012FFF10u)
                  | (uint32_t)rm;
    emit_arm32(buf, word);
}

/* --- PUSH {Rd}  (STR Rd, [SP, #-4]!) ---------------------------------- */
/*     cond 0101 0010 1101 Rd 0000 0000 0100                                */
static void emit_arm_push(CodeBuffer *buf, uint8_t rd)
{
    uint32_t word = ((uint32_t)ARM_COND_AL << 28)
                  | (0x052Du << 12)     /* 0101 0010 1101 */
                  | ((uint32_t)rd << 12) /* wait - rethink this */
                  | 0x004;              /* #4 */
    /* Re-encode correctly: STR Rd, [SP, #-4]!
     * = cond 01 0 1 0 0 1 0 Rn(SP=13) Rd offset12(4)
     * = cond 0101 0010 1101 Rd 0000 0000 0100 */
    word = ((uint32_t)ARM_COND_AL << 28)
         | (0x01u << 26)       /* 01 */
         | (1u << 24)          /* P=1 pre-indexed */
         | (0u << 23)          /* U=0 subtract */
         | (0u << 22)          /* B=0 word */
         | (1u << 21)          /* W=1 writeback */
         | (0u << 20)          /* L=0 store */
         | ((uint32_t)ARM_REG_SP << 16)
         | ((uint32_t)rd << 12)
         | 4;                  /* offset12 = 4 */
    emit_arm32(buf, word);
}

/* --- POP {Rd}  (LDR Rd, [SP], #4) ------------------------------------- */
/*     Post-indexed load: cond 01 0 0 1 0 0 1 Rn(SP) Rd offset12(4)       */
static void emit_arm_pop(CodeBuffer *buf, uint8_t rd)
{
    uint32_t word = ((uint32_t)ARM_COND_AL << 28)
                  | (0x01u << 26)       /* 01 */
                  | (0u << 24)          /* P=0 post-indexed */
                  | (1u << 23)          /* U=1 add */
                  | (0u << 22)          /* B=0 word */
                  | (0u << 21)          /* W=0 (post-index implies writeback) */
                  | (1u << 20)          /* L=1 load */
                  | ((uint32_t)ARM_REG_SP << 16)
                  | ((uint32_t)rd << 12)
                  | 4;                  /* offset12 = 4 */
    emit_arm32(buf, word);
}

/* --- NOP  (MOV R0, R0) ------------------------------------------------ */
static void emit_arm_nop(CodeBuffer *buf)
{
    /* ARM encoding for NOP: E1A00000 = MOV r0, r0 */
    emit_arm32(buf, 0xE1A00000u);
}

/* --- SVC #imm24  (software interrupt / supervisor call) ---------------- */
static void emit_arm_svc(CodeBuffer *buf, uint32_t imm24)
{
    uint32_t word = ((uint32_t)ARM_COND_AL << 28)
                  | (0xFu << 24)        /* 1111 */
                  | (imm24 & 0x00FFFFFF);
    emit_arm32(buf, word);
}

/* --- Branch placeholder (4 bytes, will be patched) --------------------- */
static void emit_arm_branch_placeholder(CodeBuffer *buf)
{
    emit_byte(buf, 0x00);
    emit_byte(buf, 0x00);
    emit_byte(buf, 0x00);
    emit_byte(buf, 0x00);
}

static void patch_arm_branch(CodeBuffer *buf, int offset, uint32_t word)
{
    buf->bytes[offset    ] = (uint8_t)( word        & 0xFF);
    buf->bytes[offset + 1] = (uint8_t)((word >>  8) & 0xFF);
    buf->bytes[offset + 2] = (uint8_t)((word >> 16) & 0xFF);
    buf->bytes[offset + 3] = (uint8_t)((word >> 24) & 0xFF);
}

/* =========================================================================
 *  Symbol table for labels
 * ========================================================================= */
#define ARM_MAX_SYMBOLS  256
#define ARM_MAX_FIXUPS   256

typedef struct {
    char name[UA_MAX_LABEL_LEN];
    int  address;
} ARMSymbol;

typedef struct {
    char  label[UA_MAX_LABEL_LEN];
    int   patch_offset;     /* offset into CodeBuffer where the instr lives */
    int   instr_addr;       /* byte address of the branch instruction       */
    int   line;
    int   is_link;          /* 1 = BL (CALL), 0 = B (JMP) */
    int   cond;             /* condition code for branch */
} ARMFixup;

typedef struct {
    ARMSymbol symbols[ARM_MAX_SYMBOLS];
    int       sym_count;
    ARMFixup  fixups[ARM_MAX_FIXUPS];
    int       fix_count;
} ARMSymTab;

static void arm_symtab_init(ARMSymTab *st)
{
    st->sym_count = 0;
    st->fix_count = 0;
}

static void arm_symtab_add(ARMSymTab *st, const char *name, int address)
{
    if (st->sym_count >= ARM_MAX_SYMBOLS) {
        fprintf(stderr, "ARM: symbol table overflow\n");
        exit(1);
    }
    strncpy(st->symbols[st->sym_count].name, name, UA_MAX_LABEL_LEN - 1);
    st->symbols[st->sym_count].name[UA_MAX_LABEL_LEN - 1] = '\0';
    st->symbols[st->sym_count].address = address;
    st->sym_count++;
}

static int arm_symtab_lookup(const ARMSymTab *st, const char *name)
{
    for (int i = 0; i < st->sym_count; i++) {
        if (strcmp(st->symbols[i].name, name) == 0)
            return st->symbols[i].address;
    }
    return -1;
}

static void arm_add_fixup(ARMSymTab *st, const char *label,
                           int patch_offset, int instr_addr, int line,
                           int is_link, int cond)
{
    if (st->fix_count >= ARM_MAX_FIXUPS) {
        fprintf(stderr, "ARM: fixup table overflow\n");
        exit(1);
    }
    ARMFixup *f = &st->fixups[st->fix_count++];
    strncpy(f->label, label, UA_MAX_LABEL_LEN - 1);
    f->label[UA_MAX_LABEL_LEN - 1] = '\0';
    f->patch_offset = patch_offset;
    f->instr_addr   = instr_addr;
    f->line         = line;
    f->is_link      = is_link;
    f->cond         = cond;
}

/* =========================================================================
 *  Try to encode a 32-bit immediate as ARM rotated imm8
 *
 *  ARM data-processing immediates use an 8-bit value rotated right by
 *  an even number (0, 2, 4, ..., 30).  Returns 1 if encodable,
 *  fills *rotate and *imm8.
 * ========================================================================= */
static int arm_encode_imm(uint32_t val, uint8_t *rotate, uint8_t *imm8)
{
    for (int rot = 0; rot < 16; rot++) {
        uint32_t rotated = (val >> (rot * 2)) | (val << (32 - rot * 2));
        if (rot * 2 == 0) rotated = val;
        if ((rotated & 0xFF) == rotated) {
            *rotate = (uint8_t)rot;
            *imm8   = (uint8_t)(rotated & 0xFF);
            return 1;
        }
    }
    return 0;
}

/* =========================================================================
 *  instruction_size_arm()  —  compute byte size of each instruction
 *
 *  Most ARM instructions are 4 bytes.
 *  LDI with large immediates needs 8 bytes (MOVW + MOVT).
 *  ALU with immediate operand: if fits in rotated imm8 -> 4 bytes,
 *    otherwise load imm into scratch (4-8 bytes) + ALU reg (4 bytes).
 * ========================================================================= */
static int instruction_size_arm(const Instruction *inst)
{
    if (inst->is_label) return 0;

    switch (inst->opcode) {
        case OP_LDI: {
            int32_t imm = (int32_t)inst->operands[1].data.imm;
            uint32_t val = (uint32_t)imm;
            /* If fits in MOVW only (upper 16 bits zero), it's 4 bytes
             * Otherwise MOVW + MOVT = 8 bytes */
            if ((val >> 16) == 0) return 4;
            return 8;
        }
        case OP_MOV:    return 4;
        case OP_LOAD:   return 4;
        case OP_STORE:  return 4;
        case OP_ADD:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 4;
            else {
                uint8_t rot, imm8;
                if (arm_encode_imm((uint32_t)(int32_t)inst->operands[1].data.imm,
                                   &rot, &imm8))
                    return 4;   /* fits in ARM immediate */
                /* Need to load into scratch then add */
                uint32_t val = (uint32_t)(int32_t)inst->operands[1].data.imm;
                return ((val >> 16) == 0 ? 4 : 8) + 4;
            }
        case OP_SUB:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 4;
            else {
                uint8_t rot, imm8;
                if (arm_encode_imm((uint32_t)(int32_t)inst->operands[1].data.imm,
                                   &rot, &imm8))
                    return 4;
                uint32_t val = (uint32_t)(int32_t)inst->operands[1].data.imm;
                return ((val >> 16) == 0 ? 4 : 8) + 4;
            }
        case OP_AND:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 4;
            else {
                uint8_t rot, imm8;
                if (arm_encode_imm((uint32_t)(int32_t)inst->operands[1].data.imm,
                                   &rot, &imm8))
                    return 4;
                uint32_t val = (uint32_t)(int32_t)inst->operands[1].data.imm;
                return ((val >> 16) == 0 ? 4 : 8) + 4;
            }
        case OP_OR:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 4;
            else {
                uint8_t rot, imm8;
                if (arm_encode_imm((uint32_t)(int32_t)inst->operands[1].data.imm,
                                   &rot, &imm8))
                    return 4;
                uint32_t val = (uint32_t)(int32_t)inst->operands[1].data.imm;
                return ((val >> 16) == 0 ? 4 : 8) + 4;
            }
        case OP_XOR:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 4;
            else {
                uint8_t rot, imm8;
                if (arm_encode_imm((uint32_t)(int32_t)inst->operands[1].data.imm,
                                   &rot, &imm8))
                    return 4;
                uint32_t val = (uint32_t)(int32_t)inst->operands[1].data.imm;
                return ((val >> 16) == 0 ? 4 : 8) + 4;
            }
        case OP_NOT:    return 4;
        case OP_INC:    return 4;
        case OP_DEC:    return 4;
        case OP_MUL:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 4;
            else {
                /* Load imm into scratch + MUL */
                uint32_t val = (uint32_t)(int32_t)inst->operands[1].data.imm;
                return ((val >> 16) == 0 ? 4 : 8) + 4;
            }
        case OP_DIV:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 4;
            else {
                uint32_t val = (uint32_t)(int32_t)inst->operands[1].data.imm;
                return ((val >> 16) == 0 ? 4 : 8) + 4;
            }
        case OP_SHL:
            return 4;   /* Both imm and reg forms are 4 bytes */
        case OP_SHR:
            return 4;
        case OP_CMP:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 4;
            else {
                uint8_t rot, imm8;
                if (arm_encode_imm((uint32_t)(int32_t)inst->operands[1].data.imm,
                                   &rot, &imm8))
                    return 4;
                /* Load into scratch + CMP reg */
                uint32_t val = (uint32_t)(int32_t)inst->operands[1].data.imm;
                return ((val >> 16) == 0 ? 4 : 8) + 4;
            }
        case OP_JMP:    return 4;   /* B rel24 */
        case OP_JZ:     return 4;   /* BEQ rel24 */
        case OP_JNZ:    return 4;   /* BNE rel24 */
        case OP_JL:     return 4;   /* BLT rel24 */
        case OP_JG:     return 4;   /* BGT rel24 */
        case OP_CALL:   return 4;   /* BL rel24 */
        case OP_RET:    return 4;   /* BX LR */
        case OP_PUSH:   return 4;
        case OP_POP:    return 4;
        case OP_NOP:    return 4;
        case OP_HLT:    return 4;   /* BX LR */
        case OP_INT:    return 4;   /* SVC */

        /* ---- Variable pseudo-instructions ----------------------------- */
        case OP_VAR:    return 0;   /* declaration only */
        case OP_BUFFER: return 0;   /* declaration only */
        case OP_SET:
            /* SET name, Rs  -> MOVW+MOVT r12,addr + STR Rs,[r12] (12) */
            if (inst->operands[1].type == OPERAND_REGISTER) return 12;
            /* SET name, imm -> MOVW+MOVT r11,imm + MOVW+MOVT r12,addr
             *                + STR r11,[r12]  (20) */
            return 20;
        case OP_GET:
            /* GET Rd, name  -> MOVW+MOVT r12,addr + LDR Rd,[r12]  (12) */
            return 12;

        /* ---- New Phase-8 instructions --------------------------------- */
        case OP_LDS:    return 8;   /* MOVW+MOVT Rd, addr  (load string ptr) */
        case OP_LOADB:  return 4;   /* LDRB Rd, [Rs]  */
        case OP_STOREB: return 4;   /* STRB Rs, [Rd]  */
        case OP_SYS:    return 4;   /* SVC #0         */

        /* ---- Architecture-specific opcodes (ARM) ----------------------- */
        case OP_WFI:    return 4;   /* WFI  (cond=AL): E320F003 */
        case OP_DMB:    return 4;   /* DMB SY:         F57FF05F */

        default:        return 0;
    }
}

/* =========================================================================
 *  Helper: emit ALU with immediate that may not fit in ARM imm8
 *
 *  If the immediate fits in the ARM rotated-imm8 encoding, emit a single
 *  data-processing-immediate instruction.  Otherwise, load it into a
 *  scratch register (IP=r12) and use the register form.
 * ========================================================================= */

/* Choose a scratch register that doesn't conflict with rd */
static uint8_t arm_scratch_reg(uint8_t rd)
{
    /* Use r12 (IP) as scratch — it's the intra-procedure-call scratch reg */
    (void)rd;
    return 12;
}

/* =========================================================================
 *  Variable table for ARM
 * ========================================================================= */
#define ARM_MAX_VARS   256
#define ARM_VAR_SIZE   4      /* bytes per variable (word) */

typedef struct {
    char    name[UA_MAX_LABEL_LEN];
    int32_t init_value;
    int     has_init;
} ARMVarEntry;

typedef struct {
    ARMVarEntry vars[ARM_MAX_VARS];
    int         count;
} ARMVarTable;

static void arm_vartab_init(ARMVarTable *vt) { vt->count = 0; }

/* =========================================================================
 *  String table for ARM  —  collects LDS string literals
 *  String data is appended after variable data in the output.
 * ========================================================================= */
#define ARM_MAX_STRINGS 256

typedef struct {
    const char *text;
    int         offset;
    int         length;
} ARMStringEntry;

typedef struct {
    ARMStringEntry strings[ARM_MAX_STRINGS];
    int            count;
    int            total_size;
} ARMStringTable;

static void arm_strtab_init(ARMStringTable *st) {
    st->count = 0;
    st->total_size = 0;
}

static int arm_strtab_add(ARMStringTable *st, const char *text) {
    for (int i = 0; i < st->count; i++)
        if (strcmp(st->strings[i].text, text) == 0) return i;
    if (st->count >= ARM_MAX_STRINGS) {
        fprintf(stderr, "ARM: string table overflow (max %d)\n",
                ARM_MAX_STRINGS);
        return 0;
    }
    int idx = st->count++;
    int len = (int)strlen(text);
    st->strings[idx].text   = text;
    st->strings[idx].offset = st->total_size;
    st->strings[idx].length = len;
    st->total_size += len + 1;
    return idx;
}

/* =========================================================================
 *  Buffer table for ARM  —  collects BUFFER declarations
 *  Buffer data is appended after variable data in the output.
 * ========================================================================= */
#define ARM_MAX_BUFFERS  256

typedef struct {
    char name[UA_MAX_LABEL_LEN];
    int  size;
} ARMBufEntry;

typedef struct {
    ARMBufEntry bufs[ARM_MAX_BUFFERS];
    int         count;
    int         total_size;
} ARMBufTable;

static void arm_buftab_init(ARMBufTable *bt) {
    bt->count = 0;
    bt->total_size = 0;
}

static int arm_buftab_add(ARMBufTable *bt, const char *name, int size) {
    for (int i = 0; i < bt->count; i++)
        if (strcmp(bt->bufs[i].name, name) == 0) {
            fprintf(stderr, "ARM: duplicate buffer '%s'\n", name);
            return -1;
        }
    if (bt->count >= ARM_MAX_BUFFERS) {
        fprintf(stderr, "ARM: buffer table overflow\n");
        return -1;
    }
    ARMBufEntry *b = &bt->bufs[bt->count++];
    strncpy(b->name, name, UA_MAX_LABEL_LEN - 1);
    b->name[UA_MAX_LABEL_LEN - 1] = '\0';
    b->size = size;
    bt->total_size += size;
    return 0;
}

static int arm_buftab_has(const ARMBufTable *bt, const char *name) {
    for (int i = 0; i < bt->count; i++) {
        if (strcmp(bt->bufs[i].name, name) == 0) return 1;
    }
    return 0;
}

static int arm_vartab_add(ARMVarTable *vt, const char *name,
                          int32_t init_value, int has_init)
{
    for (int i = 0; i < vt->count; i++) {
        if (strcmp(vt->vars[i].name, name) == 0) {
            fprintf(stderr, "ARM: duplicate variable '%s'\n", name);
            return -1;
        }
    }
    if (vt->count >= ARM_MAX_VARS) {
        fprintf(stderr, "ARM: variable table overflow (max %d)\n",
                ARM_MAX_VARS);
        return -1;
    }
    ARMVarEntry *v = &vt->vars[vt->count++];
    strncpy(v->name, name, UA_MAX_LABEL_LEN - 1);
    v->name[UA_MAX_LABEL_LEN - 1] = '\0';
    v->init_value = init_value;
    v->has_init   = has_init;
    return 0;
}

/* =========================================================================
 *  generate_arm()  —  main entry point  (two-pass)
 * ========================================================================= */
CodeBuffer* generate_arm(const Instruction *ir, int ir_count)
{
    fprintf(stderr, "[ARM] Generating code for %d IR instructions ...\n",
            ir_count);

    /* --- Pass 1: collect label addresses + variable declarations ------- */
    ARMSymTab symtab;
    arm_symtab_init(&symtab);

    ARMVarTable vartab;
    arm_vartab_init(&vartab);

    ARMStringTable strtab;
    arm_strtab_init(&strtab);

    ARMBufTable buftab;
    arm_buftab_init(&buftab);

    int pc = 0;
    for (int i = 0; i < ir_count; i++) {
        const Instruction *inst = &ir[i];
        if (inst->is_label) {
            arm_symtab_add(&symtab, inst->label_name, pc);
        } else if (inst->opcode == OP_VAR) {
            const char *vname = inst->operands[0].data.label;
            int32_t init_val  = 0;
            int     has_init  = 0;
            if (inst->operand_count >= 2 &&
                inst->operands[1].type == OPERAND_IMMEDIATE) {
                init_val = (int32_t)inst->operands[1].data.imm;
                has_init = 1;
            }
            arm_vartab_add(&vartab, vname, init_val, has_init);
        } else if (inst->opcode == OP_BUFFER) {
            const char *bname = inst->operands[0].data.label;
            int bsize = 0;
            if (inst->operand_count >= 2 &&
                inst->operands[1].type == OPERAND_IMMEDIATE) {
                bsize = (int)inst->operands[1].data.imm;
            }
            arm_buftab_add(&buftab, bname, bsize);
        } else {
            if (inst->opcode == OP_LDS)
                arm_strtab_add(&strtab, inst->operands[1].data.string);
            pc += instruction_size_arm(inst);
        }
    }

    /* Register variable symbols: each at code_end + index * 4 */
    int var_base = pc;
    for (int v = 0; v < vartab.count; v++) {
        arm_symtab_add(&symtab, vartab.vars[v].name,
                       var_base + v * ARM_VAR_SIZE);
    }
    int buf_base = var_base + vartab.count * ARM_VAR_SIZE;
    {
        int buf_offset = 0;
        for (int b = 0; b < buftab.count; b++) {
            arm_symtab_add(&symtab, buftab.bufs[b].name,
                           buf_base + buf_offset);
            buf_offset += buftab.bufs[b].size;
        }
    }
    int str_base = buf_base + buftab.total_size;

    /* --- Pass 2: code emission ----------------------------------------- */
    CodeBuffer *code = create_code_buffer();
    if (!code) {
        fprintf(stderr, "UA ARM: out of memory\n");
        return NULL;
    }

    for (int i = 0; i < ir_count; i++) {
        const Instruction *inst = &ir[i];

        if (inst->is_label)
            continue;

        switch (inst->opcode) {

        /* ---- LDI Rd, #imm  ->  MOVW [+ MOVT] ------------ 4-8 bytes -- */
        case OP_LDI: {
            int rd = inst->operands[0].data.reg;
            int32_t imm = (int32_t)inst->operands[1].data.imm;
            arm_validate_register(inst, rd);
            uint8_t enc = ARM_REG_ENC[rd];
            fprintf(stderr, "  LDI R%d -> MOV %s, #%d\n",
                    rd, ARM_REG_NAME[rd], imm);
            emit_arm_load_imm32(code, enc, imm);
            break;
        }

        /* ---- MOV Rd, Rs  ->  MOV Rd, Rm ------------------- 4 bytes -- */
        case OP_MOV: {
            int rd = inst->operands[0].data.reg;
            int rs = inst->operands[1].data.reg;
            arm_validate_register(inst, rd);
            arm_validate_register(inst, rs);
            fprintf(stderr, "  MOV R%d, R%d -> MOV %s, %s\n",
                    rd, rs, ARM_REG_NAME[rd], ARM_REG_NAME[rs]);
            emit_arm_mov_reg(code, ARM_REG_ENC[rd], ARM_REG_ENC[rs]);
            break;
        }

        /* ---- LOAD Rd, Rs  ->  LDR Rd, [Rs] --------------- 4 bytes --- */
        case OP_LOAD: {
            int rd = inst->operands[0].data.reg;
            int rs = inst->operands[1].data.reg;
            arm_validate_register(inst, rd);
            arm_validate_register(inst, rs);
            fprintf(stderr, "  LOAD R%d, R%d -> LDR %s, [%s]\n",
                    rd, rs, ARM_REG_NAME[rd], ARM_REG_NAME[rs]);
            emit_arm_ldr(code, ARM_REG_ENC[rd], ARM_REG_ENC[rs]);
            break;
        }

        /* ---- STORE Rx, Ry  ->  STR Ry, [Rx] -------------- 4 bytes --- */
        case OP_STORE: {
            int rx = inst->operands[0].data.reg;
            int ry = inst->operands[1].data.reg;
            arm_validate_register(inst, rx);
            arm_validate_register(inst, ry);
            fprintf(stderr, "  STORE R%d, R%d -> STR %s, [%s]\n",
                    rx, ry, ARM_REG_NAME[ry], ARM_REG_NAME[rx]);
            emit_arm_str(code, ARM_REG_ENC[ry], ARM_REG_ENC[rx]);
            break;
        }

        /* ---- ADD Rd, Rs/imm -------------------------------- 4/4-12 --- */
        case OP_ADD: {
            int rd = inst->operands[0].data.reg;
            arm_validate_register(inst, rd);
            uint8_t enc_d = ARM_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                arm_validate_register(inst, rs);
                fprintf(stderr, "  ADD R%d, R%d -> ADD %s, %s, %s\n",
                        rd, rs, ARM_REG_NAME[rd], ARM_REG_NAME[rd],
                        ARM_REG_NAME[rs]);
                emit_arm_add_reg(code, enc_d, enc_d, ARM_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t rot, imm8;
                if (arm_encode_imm((uint32_t)imm, &rot, &imm8)) {
                    fprintf(stderr, "  ADD R%d, #%d -> ADD %s, %s, #%d\n",
                            rd, imm, ARM_REG_NAME[rd], ARM_REG_NAME[rd], imm);
                    emit_arm32(code, arm_dp_imm(ARM_COND_AL, ARM_DP_ADD, 0,
                                                 enc_d, enc_d, rot, imm8));
                } else {
                    uint8_t scratch = arm_scratch_reg(enc_d);
                    fprintf(stderr, "  ADD R%d, #%d -> MOV r12, #%d; ADD\n",
                            rd, imm, imm);
                    emit_arm_load_imm32(code, scratch, imm);
                    emit_arm_add_reg(code, enc_d, enc_d, scratch);
                }
            }
            break;
        }

        /* ---- SUB Rd, Rs/imm -------------------------------- 4/4-12 --- */
        case OP_SUB: {
            int rd = inst->operands[0].data.reg;
            arm_validate_register(inst, rd);
            uint8_t enc_d = ARM_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                arm_validate_register(inst, rs);
                fprintf(stderr, "  SUB R%d, R%d -> SUB %s, %s, %s\n",
                        rd, rs, ARM_REG_NAME[rd], ARM_REG_NAME[rd],
                        ARM_REG_NAME[rs]);
                emit_arm_sub_reg(code, enc_d, enc_d, ARM_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t rot, imm8;
                if (arm_encode_imm((uint32_t)imm, &rot, &imm8)) {
                    fprintf(stderr, "  SUB R%d, #%d -> SUB %s, %s, #%d\n",
                            rd, imm, ARM_REG_NAME[rd], ARM_REG_NAME[rd], imm);
                    emit_arm32(code, arm_dp_imm(ARM_COND_AL, ARM_DP_SUB, 0,
                                                 enc_d, enc_d, rot, imm8));
                } else {
                    uint8_t scratch = arm_scratch_reg(enc_d);
                    fprintf(stderr, "  SUB R%d, #%d -> MOV r12, #%d; SUB\n",
                            rd, imm, imm);
                    emit_arm_load_imm32(code, scratch, imm);
                    emit_arm_sub_reg(code, enc_d, enc_d, scratch);
                }
            }
            break;
        }

        /* ---- AND Rd, Rs/imm -------------------------------- 4/4-12 --- */
        case OP_AND: {
            int rd = inst->operands[0].data.reg;
            arm_validate_register(inst, rd);
            uint8_t enc_d = ARM_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                arm_validate_register(inst, rs);
                fprintf(stderr, "  AND R%d, R%d -> AND %s, %s, %s\n",
                        rd, rs, ARM_REG_NAME[rd], ARM_REG_NAME[rd],
                        ARM_REG_NAME[rs]);
                emit_arm_and_reg(code, enc_d, enc_d, ARM_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t rot, imm8;
                if (arm_encode_imm((uint32_t)imm, &rot, &imm8)) {
                    fprintf(stderr, "  AND R%d, #%d\n", rd, imm);
                    emit_arm32(code, arm_dp_imm(ARM_COND_AL, ARM_DP_AND, 0,
                                                 enc_d, enc_d, rot, imm8));
                } else {
                    uint8_t scratch = arm_scratch_reg(enc_d);
                    fprintf(stderr, "  AND R%d, #%d -> MOV r12, #%d; AND\n",
                            rd, imm, imm);
                    emit_arm_load_imm32(code, scratch, imm);
                    emit_arm_and_reg(code, enc_d, enc_d, scratch);
                }
            }
            break;
        }

        /* ---- OR Rd, Rs/imm --------------------------------- 4/4-12 --- */
        case OP_OR: {
            int rd = inst->operands[0].data.reg;
            arm_validate_register(inst, rd);
            uint8_t enc_d = ARM_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                arm_validate_register(inst, rs);
                fprintf(stderr, "  OR  R%d, R%d -> ORR %s, %s, %s\n",
                        rd, rs, ARM_REG_NAME[rd], ARM_REG_NAME[rd],
                        ARM_REG_NAME[rs]);
                emit_arm_orr_reg(code, enc_d, enc_d, ARM_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t rot, imm8;
                if (arm_encode_imm((uint32_t)imm, &rot, &imm8)) {
                    fprintf(stderr, "  OR  R%d, #%d\n", rd, imm);
                    emit_arm32(code, arm_dp_imm(ARM_COND_AL, ARM_DP_ORR, 0,
                                                 enc_d, enc_d, rot, imm8));
                } else {
                    uint8_t scratch = arm_scratch_reg(enc_d);
                    fprintf(stderr, "  OR  R%d, #%d -> MOV r12, #%d; ORR\n",
                            rd, imm, imm);
                    emit_arm_load_imm32(code, scratch, imm);
                    emit_arm_orr_reg(code, enc_d, enc_d, scratch);
                }
            }
            break;
        }

        /* ---- XOR Rd, Rs/imm -------------------------------- 4/4-12 --- */
        case OP_XOR: {
            int rd = inst->operands[0].data.reg;
            arm_validate_register(inst, rd);
            uint8_t enc_d = ARM_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                arm_validate_register(inst, rs);
                fprintf(stderr, "  XOR R%d, R%d -> EOR %s, %s, %s\n",
                        rd, rs, ARM_REG_NAME[rd], ARM_REG_NAME[rd],
                        ARM_REG_NAME[rs]);
                emit_arm_eor_reg(code, enc_d, enc_d, ARM_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t rot, imm8;
                if (arm_encode_imm((uint32_t)imm, &rot, &imm8)) {
                    fprintf(stderr, "  XOR R%d, #%d\n", rd, imm);
                    emit_arm32(code, arm_dp_imm(ARM_COND_AL, ARM_DP_EOR, 0,
                                                 enc_d, enc_d, rot, imm8));
                } else {
                    uint8_t scratch = arm_scratch_reg(enc_d);
                    fprintf(stderr, "  XOR R%d, #%d -> MOV r12, #%d; EOR\n",
                            rd, imm, imm);
                    emit_arm_load_imm32(code, scratch, imm);
                    emit_arm_eor_reg(code, enc_d, enc_d, scratch);
                }
            }
            break;
        }

        /* ---- NOT Rd  ->  MVN Rd, Rd ------------------------ 4 bytes -- */
        case OP_NOT: {
            int rd = inst->operands[0].data.reg;
            arm_validate_register(inst, rd);
            fprintf(stderr, "  NOT R%d -> MVN %s, %s\n",
                    rd, ARM_REG_NAME[rd], ARM_REG_NAME[rd]);
            emit_arm_mvn_reg(code, ARM_REG_ENC[rd], ARM_REG_ENC[rd]);
            break;
        }

        /* ---- INC Rd  ->  ADD Rd, Rd, #1 -------------------- 4 bytes -- */
        case OP_INC: {
            int rd = inst->operands[0].data.reg;
            arm_validate_register(inst, rd);
            fprintf(stderr, "  INC R%d -> ADD %s, %s, #1\n",
                    rd, ARM_REG_NAME[rd], ARM_REG_NAME[rd]);
            emit_arm_add_imm(code, ARM_REG_ENC[rd], ARM_REG_ENC[rd], 1);
            break;
        }

        /* ---- DEC Rd  ->  SUB Rd, Rd, #1 -------------------- 4 bytes -- */
        case OP_DEC: {
            int rd = inst->operands[0].data.reg;
            arm_validate_register(inst, rd);
            fprintf(stderr, "  DEC R%d -> SUB %s, %s, #1\n",
                    rd, ARM_REG_NAME[rd], ARM_REG_NAME[rd]);
            emit_arm_sub_imm(code, ARM_REG_ENC[rd], ARM_REG_ENC[rd], 1);
            break;
        }

        /* ---- MUL Rd, Rs/imm  ->  MUL Rd, Rd, Rm ------ 4/8-12 bytes - */
        case OP_MUL: {
            int rd = inst->operands[0].data.reg;
            arm_validate_register(inst, rd);
            uint8_t enc_d = ARM_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                arm_validate_register(inst, rs);
                fprintf(stderr, "  MUL R%d, R%d -> MUL %s, %s, %s\n",
                        rd, rs, ARM_REG_NAME[rd], ARM_REG_NAME[rd],
                        ARM_REG_NAME[rs]);
                emit_arm_mul(code, enc_d, enc_d, ARM_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t scratch = arm_scratch_reg(enc_d);
                fprintf(stderr, "  MUL R%d, #%d -> MOV r12, #%d; MUL\n",
                        rd, imm, imm);
                emit_arm_load_imm32(code, scratch, imm);
                emit_arm_mul(code, enc_d, enc_d, scratch);
            }
            break;
        }

        /* ---- DIV Rd, Rs/imm  ->  SDIV Rd, Rd, Rm ----- 4/8-12 bytes - */
        case OP_DIV: {
            int rd = inst->operands[0].data.reg;
            arm_validate_register(inst, rd);
            uint8_t enc_d = ARM_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                arm_validate_register(inst, rs);
                fprintf(stderr, "  DIV R%d, R%d -> SDIV %s, %s, %s\n",
                        rd, rs, ARM_REG_NAME[rd], ARM_REG_NAME[rd],
                        ARM_REG_NAME[rs]);
                emit_arm_sdiv(code, enc_d, enc_d, ARM_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t scratch = arm_scratch_reg(enc_d);
                fprintf(stderr, "  DIV R%d, #%d -> MOV r12, #%d; SDIV\n",
                        rd, imm, imm);
                emit_arm_load_imm32(code, scratch, imm);
                emit_arm_sdiv(code, enc_d, enc_d, scratch);
            }
            break;
        }

        /* ---- SHL Rd, Rs/imm -------------------------------- 4 bytes -- */
        case OP_SHL: {
            int rd = inst->operands[0].data.reg;
            arm_validate_register(inst, rd);
            uint8_t enc_d = ARM_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_IMMEDIATE) {
                uint8_t imm = (uint8_t)(inst->operands[1].data.imm & 0x1F);
                fprintf(stderr, "  SHL R%d, #%d -> LSL %s, %s, #%d\n",
                        rd, imm, ARM_REG_NAME[rd], ARM_REG_NAME[rd], imm);
                emit_arm_lsl_imm(code, enc_d, enc_d, imm);
            } else {
                int rs = inst->operands[1].data.reg;
                arm_validate_register(inst, rs);
                fprintf(stderr, "  SHL R%d, R%d -> LSL %s, %s, %s\n",
                        rd, rs, ARM_REG_NAME[rd], ARM_REG_NAME[rd],
                        ARM_REG_NAME[rs]);
                emit_arm_lsl_reg(code, enc_d, enc_d, ARM_REG_ENC[rs]);
            }
            break;
        }

        /* ---- SHR Rd, Rs/imm -------------------------------- 4 bytes -- */
        case OP_SHR: {
            int rd = inst->operands[0].data.reg;
            arm_validate_register(inst, rd);
            uint8_t enc_d = ARM_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_IMMEDIATE) {
                uint8_t imm = (uint8_t)(inst->operands[1].data.imm & 0x1F);
                fprintf(stderr, "  SHR R%d, #%d -> LSR %s, %s, #%d\n",
                        rd, imm, ARM_REG_NAME[rd], ARM_REG_NAME[rd], imm);
                emit_arm_lsr_imm(code, enc_d, enc_d, imm);
            } else {
                int rs = inst->operands[1].data.reg;
                arm_validate_register(inst, rs);
                fprintf(stderr, "  SHR R%d, R%d -> LSR %s, %s, %s\n",
                        rd, rs, ARM_REG_NAME[rd], ARM_REG_NAME[rd],
                        ARM_REG_NAME[rs]);
                emit_arm_lsr_reg(code, enc_d, enc_d, ARM_REG_ENC[rs]);
            }
            break;
        }

        /* ---- CMP Ra, Rb/imm -------------------------------- 4/4-12 --- */
        case OP_CMP: {
            int ra = inst->operands[0].data.reg;
            arm_validate_register(inst, ra);
            uint8_t enc_a = ARM_REG_ENC[ra];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rb = inst->operands[1].data.reg;
                arm_validate_register(inst, rb);
                fprintf(stderr, "  CMP R%d, R%d -> CMP %s, %s\n",
                        ra, rb, ARM_REG_NAME[ra], ARM_REG_NAME[rb]);
                emit_arm_cmp_reg(code, enc_a, ARM_REG_ENC[rb]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t rot, imm8;
                if (arm_encode_imm((uint32_t)imm, &rot, &imm8)) {
                    fprintf(stderr, "  CMP R%d, #%d\n", ra, imm);
                    emit_arm_cmp_imm(code, enc_a, rot, imm8);
                } else {
                    uint8_t scratch = arm_scratch_reg(enc_a);
                    fprintf(stderr, "  CMP R%d, #%d -> MOV r12, #%d; CMP\n",
                            ra, imm, imm);
                    emit_arm_load_imm32(code, scratch, imm);
                    emit_arm_cmp_reg(code, enc_a, scratch);
                }
            }
            break;
        }

        /* ---- JMP label  ->  B label ------------------------ 4 bytes -- */
        case OP_JMP: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  JMP %s -> B\n", label);
            int patch_off = code->size;
            emit_arm_branch_placeholder(code);
            arm_add_fixup(&symtab, label, patch_off, patch_off, inst->line,
                          0, ARM_COND_AL);
            break;
        }

        /* ---- JZ label  ->  BEQ label ----------------------- 4 bytes -- */
        case OP_JZ: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  JZ  %s -> BEQ\n", label);
            int patch_off = code->size;
            emit_arm_branch_placeholder(code);
            arm_add_fixup(&symtab, label, patch_off, patch_off, inst->line,
                          0, ARM_COND_EQ);
            break;
        }

        /* ---- JNZ label  ->  BNE label ---------------------- 4 bytes -- */
        case OP_JNZ: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  JNZ %s -> BNE\n", label);
            int patch_off = code->size;
            emit_arm_branch_placeholder(code);
            arm_add_fixup(&symtab, label, patch_off, patch_off, inst->line,
                          0, ARM_COND_NE);
            break;
        }

        /* ---- JL label  ->  BLT label ----------------------- 4 bytes -- */
        case OP_JL: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  JL  %s -> BLT\n", label);
            int patch_off = code->size;
            emit_arm_branch_placeholder(code);
            arm_add_fixup(&symtab, label, patch_off, patch_off, inst->line,
                          0, ARM_COND_LT);
            break;
        }

        /* ---- JG label  ->  BGT label ----------------------- 4 bytes -- */
        case OP_JG: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  JG  %s -> BGT\n", label);
            int patch_off = code->size;
            emit_arm_branch_placeholder(code);
            arm_add_fixup(&symtab, label, patch_off, patch_off, inst->line,
                          0, ARM_COND_GT);
            break;
        }

        /* ---- CALL label  ->  BL label ---------------------- 4 bytes -- */
        case OP_CALL: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  CALL %s -> BL\n", label);
            int patch_off = code->size;
            emit_arm_branch_placeholder(code);
            arm_add_fixup(&symtab, label, patch_off, patch_off, inst->line,
                          1, ARM_COND_AL);
            break;
        }

        /* ---- RET  ->  BX LR -------------------------------- 4 bytes -- */
        case OP_RET:
            fprintf(stderr, "  RET -> BX LR\n");
            emit_arm_bx(code, ARM_REG_LR);
            break;

        /* ---- PUSH Rs ---------------------------------------- 4 bytes -- */
        case OP_PUSH: {
            int rs = inst->operands[0].data.reg;
            arm_validate_register(inst, rs);
            fprintf(stderr, "  PUSH R%d -> STR %s, [SP, #-4]!\n",
                    rs, ARM_REG_NAME[rs]);
            emit_arm_push(code, ARM_REG_ENC[rs]);
            break;
        }

        /* ---- POP Rd ----------------------------------------- 4 bytes -- */
        case OP_POP: {
            int rd = inst->operands[0].data.reg;
            arm_validate_register(inst, rd);
            fprintf(stderr, "  POP  R%d -> LDR %s, [SP], #4\n",
                    rd, ARM_REG_NAME[rd]);
            emit_arm_pop(code, ARM_REG_ENC[rd]);
            break;
        }

        /* ---- NOP -------------------------------------------- 4 bytes -- */
        case OP_NOP:
            fprintf(stderr, "  NOP\n");
            emit_arm_nop(code);
            break;

        /* ---- HLT  ->  BX LR -------------------------------- 4 bytes -- */
        case OP_HLT:
            fprintf(stderr, "  HLT -> BX LR\n");
            emit_arm_bx(code, ARM_REG_LR);
            break;

        /* ---- INT #imm  ->  SVC #imm ------------------------ 4 bytes -- */
        case OP_INT: {
            uint32_t imm = (uint32_t)(inst->operands[0].data.imm & 0x00FFFFFF);
            fprintf(stderr, "  INT #%d -> SVC #%d\n", (int)imm, (int)imm);
            emit_arm_svc(code, imm);
            break;
        }

        /* ---- VAR — declaration only, no code emitted ------------------ */
        case OP_VAR:
            break;

        /* ---- BUFFER — declaration only, no code emitted --------------- */
        case OP_BUFFER:
            break;

        /* ---- SET name, Rs/imm — store to variable --------------------- */
        case OP_SET: {
            const char *vname = inst->operands[0].data.label;
            int var_addr = arm_symtab_lookup(&symtab, vname);
            if (var_addr < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "undefined variable '%s'", vname);
                arm_error(inst, msg);
            }
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                arm_validate_register(inst, rs);
                fprintf(stderr, "  SET %s, R%d -> STR %s, [r12]\n",
                        vname, rs, ARM_REG_NAME[rs]);
                /* Load address into r12 (scratch) */
                emit_arm_load_imm32_full(code, ARM_REG_IP,
                                         (int32_t)var_addr);
                /* STR Rs, [r12] */
                emit_arm_str(code, ARM_REG_ENC[rs], ARM_REG_IP);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                fprintf(stderr, "  SET %s, #%d -> STR r11, [r12]\n",
                        vname, imm);
                /* Load value into r11 */
                emit_arm_load_imm32_full(code, ARM_REG_FP, imm);
                /* Load address into r12 */
                emit_arm_load_imm32_full(code, ARM_REG_IP,
                                         (int32_t)var_addr);
                /* STR r11, [r12] */
                emit_arm_str(code, ARM_REG_FP, ARM_REG_IP);
            }
            break;
        }

        /* ---- GET Rd, name — load from variable or buffer address ------ */
        case OP_GET: {
            int rd = inst->operands[0].data.reg;
            const char *vname = inst->operands[1].data.label;
            arm_validate_register(inst, rd);
            int var_addr = arm_symtab_lookup(&symtab, vname);
            if (var_addr < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "undefined variable '%s'", vname);
                arm_error(inst, msg);
            }
            int is_buf = arm_buftab_has(&buftab, vname);
            if (is_buf) {
                fprintf(stderr, "  GET R%d, %s -> MOVW+MOVT %s, #%d (buffer address)\n",
                        rd, vname, ARM_REG_NAME[rd], var_addr);
                /* Load address into r12, then MOV Rd, r12 */
                emit_arm_load_imm32_full(code, ARM_REG_IP,
                                         (int32_t)var_addr);
                emit_arm_mov_reg(code, ARM_REG_ENC[rd], ARM_REG_IP);
            } else {
                fprintf(stderr, "  GET R%d, %s -> LDR %s, [r12]\n",
                        rd, vname, ARM_REG_NAME[rd]);
                /* Load address into r12 */
                emit_arm_load_imm32_full(code, ARM_REG_IP,
                                         (int32_t)var_addr);
                /* LDR Rd, [r12] */
                emit_arm_ldr(code, ARM_REG_ENC[rd], ARM_REG_IP);
            }
            break;
        }

        /* ---- LDS Rd, "str"  ->  MOVW+MOVT Rd, addr -------- 8 bytes --- */
        case OP_LDS: {
            int rd = inst->operands[0].data.reg;
            const char *str = inst->operands[1].data.string;
            arm_validate_register(inst, rd);
            int str_idx = arm_strtab_add(&strtab, str);
            int str_addr = str_base + strtab.strings[str_idx].offset;
            fprintf(stderr, "  LDS R%d, \"%s\" -> MOVW+MOVT %s, #%d\n",
                    rd, str, ARM_REG_NAME[rd], str_addr);
            emit_arm_load_imm32_full(code, ARM_REG_ENC[rd],
                                     (int32_t)str_addr);
            break;
        }

        /* ---- LOADB Rd, Rs  ->  LDRB Rd, [Rs] -------------- 4 bytes --- */
        case OP_LOADB: {
            int rd = inst->operands[0].data.reg;
            int rs = inst->operands[1].data.reg;
            arm_validate_register(inst, rd);
            arm_validate_register(inst, rs);
            fprintf(stderr, "  LOADB R%d, R%d -> LDRB %s, [%s]\n",
                    rd, rs, ARM_REG_NAME[rd], ARM_REG_NAME[rs]);
            /* LDRB: same as LDR but B=1 (bit 22) */
            {
                uint32_t word = ((uint32_t)ARM_COND_AL << 28)
                              | (0x01u << 26)
                              | (1u << 24)   /* P=1 pre-idx */
                              | (1u << 23)   /* U=1 add */
                              | (1u << 22)   /* B=1 byte */
                              | (0u << 21)   /* W=0 */
                              | (1u << 20)   /* L=1 load */
                              | ((uint32_t)ARM_REG_ENC[rs] << 16)
                              | ((uint32_t)ARM_REG_ENC[rd] << 12)
                              | 0;
                emit_arm32(code, word);
            }
            break;
        }

        /* ---- STOREB Rs, Rd  ->  STRB Rs, [Rd] ------------- 4 bytes --- */
        case OP_STOREB: {
            int rx = inst->operands[0].data.reg;
            int ry = inst->operands[1].data.reg;
            arm_validate_register(inst, rx);
            arm_validate_register(inst, ry);
            fprintf(stderr, "  STOREB R%d, R%d -> STRB %s, [%s]\n",
                    rx, ry, ARM_REG_NAME[rx], ARM_REG_NAME[ry]);
            /* STRB: same as STR but B=1 (bit 22) */
            {
                uint32_t word = ((uint32_t)ARM_COND_AL << 28)
                              | (0x01u << 26)
                              | (1u << 24)   /* P=1 */
                              | (1u << 23)   /* U=1 */
                              | (1u << 22)   /* B=1 byte */
                              | (0u << 21)   /* W=0 */
                              | (0u << 20)   /* L=0 store */
                              | ((uint32_t)ARM_REG_ENC[ry] << 16)
                              | ((uint32_t)ARM_REG_ENC[rx] << 12)
                              | 0;
                emit_arm32(code, word);
            }
            break;
        }

        /* ---- SYS  ->  SVC #0 ----------------------------- 4 bytes --- */
        case OP_SYS:
            fprintf(stderr, "  SYS -> SVC #0\n");
            emit_arm_svc(code, 0);
            break;

        /* ---- WFI ------------------------------------------ 4 bytes --- */
        case OP_WFI:
            fprintf(stderr, "  WFI\n");
            emit_arm32(code, 0xE320F003u);   /* WFI (cond=AL) */
            break;

        /* ---- DMB SY --------------------------------------- 4 bytes --- */
        case OP_DMB:
            fprintf(stderr, "  DMB SY\n");
            emit_arm32(code, 0xF57FF05Fu);   /* DMB SY (unconditional) */
            break;

        default: {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "opcode '%s' is not supported by the ARM backend",
                     opcode_name(inst->opcode));
            arm_error(inst, msg);
            break;
        }
        }
    }

    /* --- Pass 3: patch branch relocations ------------------------------ */
    for (int f = 0; f < symtab.fix_count; f++) {
        ARMFixup *fix = &symtab.fixups[f];
        int target = arm_symtab_lookup(&symtab, fix->label);
        if (target < 0) {
            fprintf(stderr,
                    "ARM: undefined label or variable '%s' (line %d)\n",
                    fix->label, fix->line);
            free_code_buffer(code);
            return NULL;
        }
        /* ARM branch offset: (target - (instr_addr + 8)) >> 2
         * The +8 accounts for the ARM pipeline (PC is 2 instructions ahead) */
        int32_t offset = (int32_t)(target - (fix->instr_addr + 8));
        int32_t offset24 = offset >> 2;

        /* Check range: 24-bit signed = ±32 MB */
        if (offset24 < -0x800000 || offset24 > 0x7FFFFF) {
            fprintf(stderr, "ARM: branch target '%s' out of range (line %d)\n",
                    fix->label, fix->line);
            free_code_buffer(code);
            return NULL;
        }

        uint32_t word = ((uint32_t)fix->cond << 28)
                      | (0x5u << 25)
                      | ((uint32_t)(fix->is_link ? 1 : 0) << 24)
                      | ((uint32_t)offset24 & 0x00FFFFFF);
        patch_arm_branch(code, fix->patch_offset, word);
    }

    /* --- Append variable data section --------------------------------- */
    int data_start = code->size;
    for (int v = 0; v < vartab.count; v++) {
        uint32_t val = (uint32_t)vartab.vars[v].init_value;
        emit_byte(code, (uint8_t)(val & 0xFF));
        emit_byte(code, (uint8_t)((val >> 8) & 0xFF));
        emit_byte(code, (uint8_t)((val >> 16) & 0xFF));
        emit_byte(code, (uint8_t)((val >> 24) & 0xFF));
    }

    /* --- Append buffer data section (zero-filled) -------------------- */
    for (int b = 0; b < buftab.count; b++)
        for (int i = 0; i < buftab.bufs[b].size; i++)
            emit_byte(code, 0x00);

    /* --- Append string data section ----------------------------------- */
    for (int s = 0; s < strtab.count; s++) {
        const char *p = strtab.strings[s].text;
        int len = strtab.strings[s].length;
        for (int b = 0; b < len; b++)
            emit_byte(code, (uint8_t)p[b]);
        emit_byte(code, 0x00);
    }

    fprintf(stderr, "[ARM] Emitted %d bytes (%d code + %d var + %d buf + %d str)\n",
            code->size, data_start,
            vartab.count * ARM_VAR_SIZE, buftab.total_size,
            strtab.total_size);
    return code;
}
