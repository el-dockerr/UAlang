/*
 * =============================================================================
 *  UA - Unified Assembler
 *  ARM64 (AArch64) Back-End (Code Generation)
 *
 *  File:    backend_arm64.c
 *  Purpose: Translate UA IR into raw AArch64 machine code.
 *           Compatible with Apple Silicon (M1/M2/M3/M4) and all ARMv8-A
 *           processors.
 *
 *  ┌──────────────────────────────────────────────────────────────────────┐
 *  │  AArch64 Instruction Encoding Reference (ARMv8-A, A64)            │
 *  │                                                                    │
 *  │  All instructions are 32 bits, little-endian.                      │
 *  │                                                                    │
 *  │  Data Processing (register, 64-bit):                               │
 *  │    sf=1 (64-bit), opc varies per instruction                       │
 *  │                                                                    │
 *  │  Key instruction encodings:                                        │
 *  │    ADD  Xd, Xn, Xm :  sf=1 op=0 S=0 0b01011 shift=00 Rm Xn Xd   │
 *  │    SUB  Xd, Xn, Xm :  sf=1 op=1 S=0 0b01011 shift=00 Rm Xn Xd   │
 *  │    SUBS Xd, Xn, Xm :  sf=1 op=1 S=1 0b01011 shift=00 Rm Xn Xd   │
 *  │    AND  Xd, Xn, Xm :  sf=1 opc=00 0b01010 shift=00 N=0 Rm Xn Xd │
 *  │    ORR  Xd, Xn, Xm :  sf=1 opc=01 0b01010 shift=00 N=0 Rm Xn Xd │
 *  │    EOR  Xd, Xn, Xm :  sf=1 opc=10 0b01010 shift=00 N=0 Rm Xn Xd │
 *  │    ORN  Xd, Xn, Xm :  sf=1 opc=01 0b01010 shift=00 N=1 Rm Xn Xd │
 *  │    MUL  Xd, Xn, Xm :  sf=1 0b0011011 000 Rm 0 11111 Xn Xd       │
 *  │    SDIV Xd, Xn, Xm :  sf=1 0b0011010 110 Rm 00001 1 Xn Xd       │
 *  │    MOVZ Xd, #imm16, LSL #shift :  sf=1 10 100101 hw imm16 Xd     │
 *  │    MOVK Xd, #imm16, LSL #shift :  sf=1 11 100101 hw imm16 Xd     │
 *  │    LDR  Xd, [Xn]  :  11 111 0 01 01 imm12=0 Xn Xd               │
 *  │    STR  Xd, [Xn]  :  11 111 0 01 00 imm12=0 Xn Xd               │
 *  │    B    #imm26     :  000101 imm26                                 │
 *  │    BL   #imm26     :  100101 imm26                                 │
 *  │    B.cond #imm19   :  01010100 imm19 0 cond                       │
 *  │    RET  Xn         :  1101011 0010 11111 0000 00 Xn 00000         │
 *  │    SVC  #imm16     :  11010100 000 imm16 000 01                   │
 *  │    NOP             :  11010101 0000 0011 0010 0000 000 11111      │
 *  │    LSL  Xd, Xn, #n :  UBFM encoding                               │
 *  │    LSR  Xd, Xn, #n :  UBFM encoding                               │
 *  │    LSLV Xd, Xn, Xm :  sf=1 0b0011010 110 Rm 0010 00 Xn Xd       │
 *  │    LSRV Xd, Xn, Xm :  sf=1 0b0011010 110 Rm 0010 01 Xn Xd       │
 *  │                                                                    │
 *  │  UA registers R0-R7 map to X0-X7.                                  │
 *  │  Scratch: X9, X10.   SP = X31(SP).  LR = X30.                     │
 *  └──────────────────────────────────────────────────────────────────────┘
 *
 *  License: MIT
 * =============================================================================
 */

#include "backend_arm64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 *  AArch64 register encoding table
 * =========================================================================
 *  UA R0-R7 map to X0-X7.  R8-R15 are rejected.
 * ========================================================================= */
#define A64_MAX_REG  8

static const uint8_t A64_REG_ENC[A64_MAX_REG] = {
    0, 1, 2, 3, 4, 5, 6, 7
};

static const char* A64_REG_NAME[A64_MAX_REG] = {
    "X0", "X1", "X2", "X3", "X4", "X5", "X6", "X7"
};

/* Special registers */
#define A64_REG_SCRATCH  9    /* X9  — scratch / temporary         */
#define A64_REG_SCRATCH2 10   /* X10 — secondary scratch           */
#define A64_REG_LR       30   /* X30 — link register               */
#define A64_REG_SP       31   /* SP when used as base/stack pointer */
#define A64_REG_XZR      31   /* XZR when used as zero register    */

/* =========================================================================
 *  AArch64 condition codes (for B.cond)
 * ========================================================================= */
#define A64_COND_EQ  0x0   /* Equal (Z set)           */
#define A64_COND_NE  0x1   /* Not equal (Z clear)     */
#define A64_COND_LT  0xB   /* Signed less than        */
#define A64_COND_GT  0xC   /* Signed greater than     */

/* =========================================================================
 *  Error helpers
 * ========================================================================= */
static void a64_error(const Instruction *inst, const char *msg)
{
    fprintf(stderr,
            "\n"
            "  UA ARM64 Backend Error\n"
            "  -----------------------\n"
            "  Line %d, Column %d: %s\n\n",
            inst->line, inst->column, msg);
    exit(1);
}

static void a64_validate_register(const Instruction *inst, int reg)
{
    if (reg < 0 || reg >= A64_MAX_REG) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "register R%d is not mapped in the ARM64 backend "
                 "(supports R0-R7: X0-X7)",
                 reg);
        a64_error(inst, msg);
    }
}

/* =========================================================================
 *  Emit helper — emit a 32-bit AArch64 instruction (little-endian)
 * ========================================================================= */
static void emit_a64(CodeBuffer *buf, uint32_t word)
{
    emit_byte(buf, (uint8_t)( word        & 0xFF));
    emit_byte(buf, (uint8_t)((word >>  8) & 0xFF));
    emit_byte(buf, (uint8_t)((word >> 16) & 0xFF));
    emit_byte(buf, (uint8_t)((word >> 24) & 0xFF));
}

/* =========================================================================
 *  AArch64 Instruction Builders
 * ========================================================================= */

/* --- ADD Xd, Xn, Xm  (64-bit, shifted register, shift=0) -------------- */
/* Encoding: sf=1 op=0 S=0 01011 shift=00 0 Rm imm6=0 Rn Rd             */
static void emit_a64_add_reg(CodeBuffer *buf, uint8_t rd, uint8_t rn,
                              uint8_t rm)
{
    uint32_t word = (1u << 31)           /* sf=1 (64-bit) */
                  | (0u << 30)           /* op=0 (ADD) */
                  | (0u << 29)           /* S=0 (no flags) */
                  | (0x0Bu << 24)        /* 01011 */
                  | (0u << 22)           /* shift=00 (LSL) */
                  | (0u << 21)           /* 0 */
                  | ((uint32_t)(rm & 0x1F) << 16)
                  | (0u << 10)           /* imm6=0 */
                  | ((uint32_t)(rn & 0x1F) << 5)
                  | ((uint32_t)(rd & 0x1F));
    emit_a64(buf, word);
}

/* --- ADD Xd, Xn, #imm12  (64-bit, immediate) -------------------------- */
/* Encoding: sf=1 op=0 S=0 100010 sh=0 imm12 Rn Rd                      */
static void emit_a64_add_imm(CodeBuffer *buf, uint8_t rd, uint8_t rn,
                              uint16_t imm12)
{
    uint32_t word = (1u << 31)           /* sf=1 */
                  | (0u << 30)           /* op=0 (ADD) */
                  | (0u << 29)           /* S=0 */
                  | (0x22u << 23)        /* 100010 */
                  | (0u << 22)           /* sh=0 */
                  | ((uint32_t)(imm12 & 0xFFF) << 10)
                  | ((uint32_t)(rn & 0x1F) << 5)
                  | ((uint32_t)(rd & 0x1F));
    emit_a64(buf, word);
}

/* --- SUB Xd, Xn, Xm  (64-bit, shifted register) ----------------------- */
static void emit_a64_sub_reg(CodeBuffer *buf, uint8_t rd, uint8_t rn,
                              uint8_t rm)
{
    uint32_t word = (1u << 31)           /* sf=1 */
                  | (1u << 30)           /* op=1 (SUB) */
                  | (0u << 29)           /* S=0 */
                  | (0x0Bu << 24)
                  | (0u << 22)
                  | (0u << 21)
                  | ((uint32_t)(rm & 0x1F) << 16)
                  | (0u << 10)
                  | ((uint32_t)(rn & 0x1F) << 5)
                  | ((uint32_t)(rd & 0x1F));
    emit_a64(buf, word);
}

/* --- SUB Xd, Xn, #imm12  (64-bit, immediate) -------------------------- */
static void emit_a64_sub_imm(CodeBuffer *buf, uint8_t rd, uint8_t rn,
                              uint16_t imm12)
{
    uint32_t word = (1u << 31)           /* sf=1 */
                  | (1u << 30)           /* op=1 (SUB) */
                  | (0u << 29)           /* S=0 */
                  | (0x22u << 23)        /* 100010 */
                  | (0u << 22)           /* sh=0 */
                  | ((uint32_t)(imm12 & 0xFFF) << 10)
                  | ((uint32_t)(rn & 0x1F) << 5)
                  | ((uint32_t)(rd & 0x1F));
    emit_a64(buf, word);
}

/* --- SUBS XZR, Xn, Xm  (CMP — sets flags, discards result) ------------ */
static void emit_a64_cmp_reg(CodeBuffer *buf, uint8_t rn, uint8_t rm)
{
    uint32_t word = (1u << 31)           /* sf=1 */
                  | (1u << 30)           /* op=1 (SUB) */
                  | (1u << 29)           /* S=1 (set flags) */
                  | (0x0Bu << 24)
                  | (0u << 22)
                  | (0u << 21)
                  | ((uint32_t)(rm & 0x1F) << 16)
                  | (0u << 10)
                  | ((uint32_t)(rn & 0x1F) << 5)
                  | ((uint32_t)(A64_REG_XZR & 0x1F));  /* Rd = XZR */
    emit_a64(buf, word);
}

/* --- SUBS XZR, Xn, #imm12  (CMP immediate) ---------------------------- */
static void emit_a64_cmp_imm(CodeBuffer *buf, uint8_t rn, uint16_t imm12)
{
    uint32_t word = (1u << 31)           /* sf=1 */
                  | (1u << 30)           /* op=1 (SUB) */
                  | (1u << 29)           /* S=1 (set flags) */
                  | (0x22u << 23)        /* 100010 */
                  | (0u << 22)           /* sh=0 */
                  | ((uint32_t)(imm12 & 0xFFF) << 10)
                  | ((uint32_t)(rn & 0x1F) << 5)
                  | ((uint32_t)(A64_REG_XZR & 0x1F));
    emit_a64(buf, word);
}

/* --- AND Xd, Xn, Xm  (shifted register, shift=0) ---------------------- */
/* Encoding: sf=1 opc=00 01010 shift=00 N=0 Rm imm6=0 Rn Rd             */
static void emit_a64_and_reg(CodeBuffer *buf, uint8_t rd, uint8_t rn,
                              uint8_t rm)
{
    uint32_t word = (1u << 31)           /* sf=1 */
                  | (0u << 29)           /* opc=00 (AND) */
                  | (0x0Au << 24)        /* 01010 */
                  | (0u << 22)           /* shift=00 */
                  | (0u << 21)           /* N=0 */
                  | ((uint32_t)(rm & 0x1F) << 16)
                  | (0u << 10)
                  | ((uint32_t)(rn & 0x1F) << 5)
                  | ((uint32_t)(rd & 0x1F));
    emit_a64(buf, word);
}

/* --- ORR Xd, Xn, Xm  (shifted register, shift=0) ---------------------- */
static void emit_a64_orr_reg(CodeBuffer *buf, uint8_t rd, uint8_t rn,
                              uint8_t rm)
{
    uint32_t word = (1u << 31)           /* sf=1 */
                  | (1u << 29)           /* opc=01 (ORR) */
                  | (0x0Au << 24)
                  | (0u << 22)
                  | (0u << 21)           /* N=0 */
                  | ((uint32_t)(rm & 0x1F) << 16)
                  | (0u << 10)
                  | ((uint32_t)(rn & 0x1F) << 5)
                  | ((uint32_t)(rd & 0x1F));
    emit_a64(buf, word);
}

/* --- EOR Xd, Xn, Xm  (shifted register, shift=0) ---------------------- */
static void emit_a64_eor_reg(CodeBuffer *buf, uint8_t rd, uint8_t rn,
                              uint8_t rm)
{
    uint32_t word = (1u << 31)           /* sf=1 */
                  | (2u << 29)           /* opc=10 (EOR) */
                  | (0x0Au << 24)
                  | (0u << 22)
                  | (0u << 21)           /* N=0 */
                  | ((uint32_t)(rm & 0x1F) << 16)
                  | (0u << 10)
                  | ((uint32_t)(rn & 0x1F) << 5)
                  | ((uint32_t)(rd & 0x1F));
    emit_a64(buf, word);
}

/* --- ORN Xd, XZR, Xm  (bitwise NOT = MVN) ----------------------------- */
static void emit_a64_mvn(CodeBuffer *buf, uint8_t rd, uint8_t rm)
{
    uint32_t word = (1u << 31)           /* sf=1 */
                  | (1u << 29)           /* opc=01 (ORR with N=1 = ORN) */
                  | (0x0Au << 24)
                  | (0u << 22)
                  | (1u << 21)           /* N=1 (invert Rm) */
                  | ((uint32_t)(rm & 0x1F) << 16)
                  | (0u << 10)
                  | ((uint32_t)(A64_REG_XZR & 0x1F) << 5)  /* Rn = XZR */
                  | ((uint32_t)(rd & 0x1F));
    emit_a64(buf, word);
}

/* --- MOV Xd, Xn  (alias for ORR Xd, XZR, Xn) ------------------------- */
static void emit_a64_mov_reg(CodeBuffer *buf, uint8_t rd, uint8_t rn)
{
    emit_a64_orr_reg(buf, rd, A64_REG_XZR, rn);
}

/* --- MOVZ Xd, #imm16, LSL #shift  (move wide with zero) --------------- */
/* Encoding: sf=1 opc=10 100101 hw imm16 Rd                              */
static void emit_a64_movz(CodeBuffer *buf, uint8_t rd, uint16_t imm16,
                            uint8_t shift)
{
    uint8_t hw = shift / 16;  /* 0, 1, 2, or 3 */
    uint32_t word = (1u << 31)           /* sf=1 (64-bit) */
                  | (2u << 29)           /* opc=10 (MOVZ) */
                  | (0x25u << 23)        /* 100101 */
                  | ((uint32_t)(hw & 0x3) << 21)
                  | ((uint32_t)imm16 << 5)
                  | ((uint32_t)(rd & 0x1F));
    emit_a64(buf, word);
}

/* --- MOVK Xd, #imm16, LSL #shift  (move wide with keep) --------------- */
static void emit_a64_movk(CodeBuffer *buf, uint8_t rd, uint16_t imm16,
                            uint8_t shift)
{
    uint8_t hw = shift / 16;
    uint32_t word = (1u << 31)           /* sf=1 */
                  | (3u << 29)           /* opc=11 (MOVK) */
                  | (0x25u << 23)        /* 100101 */
                  | ((uint32_t)(hw & 0x3) << 21)
                  | ((uint32_t)imm16 << 5)
                  | ((uint32_t)(rd & 0x1F));
    emit_a64(buf, word);
}

/* --- Load a full 32-bit immediate into Xd ----------------------------- */
/*     Uses MOVZ for low 16 bits, MOVK for high 16 bits if needed.       */
static void emit_a64_load_imm32(CodeBuffer *buf, uint8_t rd, int32_t imm)
{
    uint32_t val = (uint32_t)imm;
    emit_a64_movz(buf, rd, (uint16_t)(val & 0xFFFF), 0);
    if ((val >> 16) != 0) {
        emit_a64_movk(buf, rd, (uint16_t)((val >> 16) & 0xFFFF), 16);
    }
}

/* Always emit MOVZ + MOVK (8 bytes) — for fixed-size encoding. */
static void emit_a64_load_imm32_full(CodeBuffer *buf, uint8_t rd, int32_t imm)
{
    uint32_t val = (uint32_t)imm;
    emit_a64_movz(buf, rd, (uint16_t)(val & 0xFFFF), 0);
    emit_a64_movk(buf, rd, (uint16_t)((val >> 16) & 0xFFFF), 16);
}

/* --- MUL Xd, Xn, Xm  (MADD Xd, Xn, Xm, XZR) ------------------------- */
/* Encoding: sf=1 00 11011 000 Rm 0 Ra=11111 Rn Rd                       */
static void emit_a64_mul(CodeBuffer *buf, uint8_t rd, uint8_t rn, uint8_t rm)
{
    uint32_t word = (1u << 31)           /* sf=1 */
                  | (0x0u << 29)         /* 00 */
                  | (0x1Bu << 24)        /* 11011 */
                  | (0x0u << 21)         /* 000 */
                  | ((uint32_t)(rm & 0x1F) << 16)
                  | (0u << 15)           /* o0=0 (MADD) */
                  | (0x1Fu << 10)        /* Ra = XZR (31) */
                  | ((uint32_t)(rn & 0x1F) << 5)
                  | ((uint32_t)(rd & 0x1F));
    emit_a64(buf, word);
}

/* --- SDIV Xd, Xn, Xm -------------------------------------------------- */
/* Encoding: sf=1 0 0 11010110 Rm 00001 1 Rn Rd                          */
static void emit_a64_sdiv(CodeBuffer *buf, uint8_t rd, uint8_t rn, uint8_t rm)
{
    uint32_t word = (1u << 31)           /* sf=1 */
                  | (0u << 30)           /* 0 */
                  | (0u << 29)           /* 0 */
                  | (0xD6u << 21)        /* 11010110 */
                  | ((uint32_t)(rm & 0x1F) << 16)
                  | (0x3u << 10)         /* 000011 = SDIV */
                  | ((uint32_t)(rn & 0x1F) << 5)
                  | ((uint32_t)(rd & 0x1F));
    emit_a64(buf, word);
}

/* --- LSLV Xd, Xn, Xm  (shift left, register) -------------------------- */
/* Encoding: sf=1 0 0 11010110 Rm 0010 00 Rn Rd                          */
static void emit_a64_lslv(CodeBuffer *buf, uint8_t rd, uint8_t rn, uint8_t rm)
{
    uint32_t word = (1u << 31)
                  | (0u << 30)
                  | (0u << 29)
                  | (0xD6u << 21)
                  | ((uint32_t)(rm & 0x1F) << 16)
                  | (0x08u << 10)        /* 001000 = LSLV */
                  | ((uint32_t)(rn & 0x1F) << 5)
                  | ((uint32_t)(rd & 0x1F));
    emit_a64(buf, word);
}

/* --- LSRV Xd, Xn, Xm  (shift right, register) ------------------------- */
/* Encoding: sf=1 0 0 11010110 Rm 0010 01 Rn Rd                          */
static void emit_a64_lsrv(CodeBuffer *buf, uint8_t rd, uint8_t rn, uint8_t rm)
{
    uint32_t word = (1u << 31)
                  | (0u << 30)
                  | (0u << 29)
                  | (0xD6u << 21)
                  | ((uint32_t)(rm & 0x1F) << 16)
                  | (0x09u << 10)        /* 001001 = LSRV */
                  | ((uint32_t)(rn & 0x1F) << 5)
                  | ((uint32_t)(rd & 0x1F));
    emit_a64(buf, word);
}

/* --- LSL Xd, Xn, #shift  (alias for UBFM) ----------------------------- */
/* LSL Xd, Xn, #shift  = UBFM Xd, Xn, #(64-shift), #(63-shift)          */
/* Encoding: sf=1 10 100110 N=1 immr imms Rn Rd                          */
static void emit_a64_lsl_imm(CodeBuffer *buf, uint8_t rd, uint8_t rn,
                              uint8_t shift)
{
    uint8_t immr = (64 - shift) & 0x3F;
    uint8_t imms = (63 - shift) & 0x3F;
    uint32_t word = (1u << 31)           /* sf=1 */
                  | (2u << 29)           /* opc=10 */
                  | (0x26u << 23)        /* 100110 */
                  | (1u << 22)           /* N=1 (64-bit) */
                  | ((uint32_t)(immr & 0x3F) << 16)
                  | ((uint32_t)(imms & 0x3F) << 10)
                  | ((uint32_t)(rn & 0x1F) << 5)
                  | ((uint32_t)(rd & 0x1F));
    emit_a64(buf, word);
}

/* --- LSR Xd, Xn, #shift  (alias for UBFM) ----------------------------- */
/* LSR Xd, Xn, #shift  = UBFM Xd, Xn, #shift, #63                       */
static void emit_a64_lsr_imm(CodeBuffer *buf, uint8_t rd, uint8_t rn,
                              uint8_t shift)
{
    uint8_t immr = shift & 0x3F;
    uint8_t imms = 63;
    uint32_t word = (1u << 31)
                  | (2u << 29)
                  | (0x26u << 23)
                  | (1u << 22)           /* N=1 */
                  | ((uint32_t)(immr & 0x3F) << 16)
                  | ((uint32_t)(imms & 0x3F) << 10)
                  | ((uint32_t)(rn & 0x1F) << 5)
                  | ((uint32_t)(rd & 0x1F));
    emit_a64(buf, word);
}

/* --- LDR Xd, [Xn]  (unsigned offset = 0) ------------------------------ */
/* Encoding: 11 111 0 01 01 imm12=0 Rn Rd                                */
static void emit_a64_ldr(CodeBuffer *buf, uint8_t rd, uint8_t rn)
{
    uint32_t word = (3u << 30)           /* size=11 (64-bit) */
                  | (0x1C5u << 20)       /* 111 0 01 01 = LDR unsigned offset */
                  | (0u << 10)           /* imm12 = 0 (offset 0) */
                  | ((uint32_t)(rn & 0x1F) << 5)
                  | ((uint32_t)(rd & 0x1F));
    /* Re-derive carefully:
     * bits [31:30] = 11 (size=64)
     * bits [29:27] = 111
     * bit  [26]    = 0 (not SIMD)
     * bits [25:24] = 01
     * bits [23:22] = 01 (opc=01 = LDR)
     * bits [21:10] = imm12
     * bits [9:5]   = Rn
     * bits [4:0]   = Rt
     */
    word = (0xF9400000u)
         | ((uint32_t)(rn & 0x1F) << 5)
         | ((uint32_t)(rd & 0x1F));
    emit_a64(buf, word);
}

/* --- STR Xd, [Xn]  (unsigned offset = 0) ------------------------------ */
static void emit_a64_str(CodeBuffer *buf, uint8_t rt, uint8_t rn)
{
    uint32_t word = (0xF9000000u)
         | ((uint32_t)(rn & 0x1F) << 5)
         | ((uint32_t)(rt & 0x1F));
    emit_a64(buf, word);
}

/* --- STR Xd, [SP, #-16]!  (pre-index, decrement SP) --- PUSH ---------- */
/* Encoding: 11 111 0 00 00 0 imm9 11 Rn Rt  (pre-indexed)              */
static void emit_a64_push(CodeBuffer *buf, uint8_t rt)
{
    /* STR Xt, [SP, #-16]!
     * size=11 V=0 opc=00
     * imm9 = -16 = 0x1F0 (9-bit signed)
     * Pre-index: bits [11:10] = 11
     * Rn = SP (31)  */
    int32_t imm9 = -16;
    uint32_t word = (0xF8000000u)        /* 11 111 000 00 ... */
                  | (((uint32_t)imm9 & 0x1FF) << 12)
                  | (3u << 10)           /* pre-index (11) */
                  | ((uint32_t)(A64_REG_SP & 0x1F) << 5)
                  | ((uint32_t)(rt & 0x1F));
    emit_a64(buf, word);
}

/* --- LDR Xd, [SP], #16  (post-index, increment SP) --- POP ------------ */
static void emit_a64_pop(CodeBuffer *buf, uint8_t rt)
{
    /* LDR Xt, [SP], #16
     * size=11 V=0 opc=01
     * imm9 = 16
     * Post-index: bits [11:10] = 01  */
    int32_t imm9 = 16;
    uint32_t word = (0xF8400000u)        /* 11 111 000 01 ... (LDR) */
                  | (((uint32_t)imm9 & 0x1FF) << 12)
                  | (1u << 10)           /* post-index (01) */
                  | ((uint32_t)(A64_REG_SP & 0x1F) << 5)
                  | ((uint32_t)(rt & 0x1F));
    emit_a64(buf, word);
}

/* --- B #imm26  (unconditional branch) ---------------------------------- */
/* Encoding: 000101 imm26                                                 */
static void emit_a64_b(CodeBuffer *buf, int32_t offset)
{
    int32_t imm26 = (offset >> 2) & 0x03FFFFFF;
    uint32_t word = (0x05u << 26) | (uint32_t)imm26;
    emit_a64(buf, word);
}

/* --- BL #imm26  (branch with link) ------------------------------------- */
/* Encoding: 100101 imm26                                                 */
static void emit_a64_bl(CodeBuffer *buf, int32_t offset)
{
    int32_t imm26 = (offset >> 2) & 0x03FFFFFF;
    uint32_t word = (0x25u << 26) | (uint32_t)imm26;
    emit_a64(buf, word);
}

/* --- B.cond #imm19  (conditional branch) ------------------------------- */
/* Encoding: 01010100 imm19 0 cond                                        */
static void emit_a64_bcond(CodeBuffer *buf, int32_t offset, uint8_t cond)
{
    int32_t imm19 = (offset >> 2) & 0x7FFFF;
    uint32_t word = (0x54u << 24)
                  | ((uint32_t)imm19 << 5)
                  | (0u << 4)            /* o0 = 0 */
                  | ((uint32_t)(cond & 0xF));
    emit_a64(buf, word);
}

/* --- RET Xn  (return, default X30) ------------------------------------- */
/* Encoding: 1101011 0 0 10 11111 0000 00 Rn 00000                       */
static void emit_a64_ret(CodeBuffer *buf, uint8_t rn)
{
    uint32_t word = (0xD65F0000u)
                  | ((uint32_t)(rn & 0x1F) << 5);
    emit_a64(buf, word);
}

/* --- NOP --------------------------------------------------------------- */
/* Encoding: 0xD503201F                                                   */
static void emit_a64_nop(CodeBuffer *buf)
{
    emit_a64(buf, 0xD503201Fu);
}

/* --- SVC #imm16  (supervisor call / software interrupt) ---------------- */
/* Encoding: 11010100 000 imm16 000 01                                    */
static void emit_a64_svc(CodeBuffer *buf, uint16_t imm16)
{
    uint32_t word = (0xD4000001u)
                  | ((uint32_t)imm16 << 5);
    emit_a64(buf, word);
}

/* --- Branch placeholder (4 bytes, will be patched) --------------------- */
static void emit_a64_placeholder(CodeBuffer *buf)
{
    emit_byte(buf, 0x00);
    emit_byte(buf, 0x00);
    emit_byte(buf, 0x00);
    emit_byte(buf, 0x00);
}

static void patch_a64_word(CodeBuffer *buf, int offset, uint32_t word)
{
    buf->bytes[offset    ] = (uint8_t)( word        & 0xFF);
    buf->bytes[offset + 1] = (uint8_t)((word >>  8) & 0xFF);
    buf->bytes[offset + 2] = (uint8_t)((word >> 16) & 0xFF);
    buf->bytes[offset + 3] = (uint8_t)((word >> 24) & 0xFF);
}

/* =========================================================================
 *  Symbol table for labels
 * ========================================================================= */
#define A64_MAX_SYMBOLS  256
#define A64_MAX_FIXUPS   256

typedef struct {
    char name[UA_MAX_LABEL_LEN];
    int  address;
} A64Symbol;

/* Fixup types */
#define A64_FIXUP_B       0   /* Unconditional branch (B) */
#define A64_FIXUP_BL      1   /* Branch with link (BL) */
#define A64_FIXUP_BCOND   2   /* Conditional branch (B.cond) */

typedef struct {
    char    label[UA_MAX_LABEL_LEN];
    int     patch_offset;     /* offset into CodeBuffer where instr lives  */
    int     instr_addr;       /* byte address of the branch instruction    */
    int     line;
    int     fixup_type;       /* A64_FIXUP_B / BL / BCOND */
    uint8_t cond;             /* condition code for BCOND */
} A64Fixup;

typedef struct {
    A64Symbol symbols[A64_MAX_SYMBOLS];
    int       sym_count;
    A64Fixup  fixups[A64_MAX_FIXUPS];
    int       fix_count;
} A64SymTab;

static void a64_symtab_init(A64SymTab *st)
{
    st->sym_count = 0;
    st->fix_count = 0;
}

static void a64_symtab_add(A64SymTab *st, const char *name, int address)
{
    if (st->sym_count >= A64_MAX_SYMBOLS) {
        fprintf(stderr, "ARM64: symbol table overflow\n");
        exit(1);
    }
    strncpy(st->symbols[st->sym_count].name, name, UA_MAX_LABEL_LEN - 1);
    st->symbols[st->sym_count].name[UA_MAX_LABEL_LEN - 1] = '\0';
    st->symbols[st->sym_count].address = address;
    st->sym_count++;
}

static int a64_symtab_lookup(const A64SymTab *st, const char *name)
{
    for (int i = 0; i < st->sym_count; i++) {
        if (strcmp(st->symbols[i].name, name) == 0)
            return st->symbols[i].address;
    }
    return -1;
}

static void a64_add_fixup(A64SymTab *st, const char *label,
                           int patch_offset, int instr_addr, int line,
                           int fixup_type, uint8_t cond)
{
    if (st->fix_count >= A64_MAX_FIXUPS) {
        fprintf(stderr, "ARM64: fixup table overflow\n");
        exit(1);
    }
    A64Fixup *f = &st->fixups[st->fix_count++];
    strncpy(f->label, label, UA_MAX_LABEL_LEN - 1);
    f->label[UA_MAX_LABEL_LEN - 1] = '\0';
    f->patch_offset = patch_offset;
    f->instr_addr   = instr_addr;
    f->line         = line;
    f->fixup_type   = fixup_type;
    f->cond         = cond;
}

/* =========================================================================
 *  instruction_size_a64()  —  compute byte size of each instruction
 * ========================================================================= */
static int instruction_size_a64(const Instruction *inst)
{
    if (inst->is_label) return 0;

    switch (inst->opcode) {
        case OP_LDI: {
            int32_t imm = (int32_t)inst->operands[1].data.imm;
            uint32_t val = (uint32_t)imm;
            if ((val >> 16) == 0) return 4;  /* MOVZ only */
            return 8;  /* MOVZ + MOVK */
        }
        case OP_MOV:    return 4;
        case OP_LOAD:   return 4;
        case OP_STORE:  return 4;
        case OP_ADD:
            if (inst->operands[1].type == OPERAND_REGISTER) return 4;
            else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint32_t uval = (uint32_t)(imm < 0 ? -imm : imm);
                if (uval <= 0xFFF) return 4;   /* ADD/SUB imm12 */
                uint32_t val = (uint32_t)imm;
                return ((val >> 16) == 0 ? 4 : 8) + 4;
            }
        case OP_SUB:
            if (inst->operands[1].type == OPERAND_REGISTER) return 4;
            else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint32_t uval = (uint32_t)(imm < 0 ? -imm : imm);
                if (uval <= 0xFFF) return 4;
                uint32_t val = (uint32_t)imm;
                return ((val >> 16) == 0 ? 4 : 8) + 4;
            }
        case OP_AND:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 4 : 12;
        case OP_OR:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 4 : 12;
        case OP_XOR:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 4 : 12;
        case OP_NOT:    return 4;
        case OP_INC:    return 4;
        case OP_DEC:    return 4;
        case OP_MUL:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 4 : 12;
        case OP_DIV:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 4 : 12;
        case OP_SHL:    return 4;
        case OP_SHR:    return 4;
        case OP_CMP:
            if (inst->operands[1].type == OPERAND_REGISTER) return 4;
            else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint32_t uval = (uint32_t)(imm < 0 ? -imm : imm);
                if (uval <= 0xFFF) return 4;
                uint32_t val = (uint32_t)imm;
                return ((val >> 16) == 0 ? 4 : 8) + 4;
            }
        case OP_JMP:    return 4;   /* B */
        case OP_JZ:     return 4;   /* B.EQ */
        case OP_JNZ:    return 4;   /* B.NE */
        case OP_JL:     return 4;   /* B.LT */
        case OP_JG:     return 4;   /* B.GT */
        case OP_CALL:   return 4;   /* BL */
        case OP_RET:    return 4;   /* RET */
        case OP_PUSH:   return 4;   /* STR pre-indexed */
        case OP_POP:    return 4;   /* LDR post-indexed */
        case OP_NOP:    return 4;
        case OP_HLT:    return 4;   /* RET */
        case OP_INT:    return 4;   /* SVC */

        /* ---- Variable pseudo-instructions ----------------------------- */
        case OP_VAR:    return 0;
        case OP_BUFFER: return 0;
        case OP_SET:
            /* SET name, Rs  -> MOVZ+MOVK X9,addr(8) + STR Rs,[X9](4) = 12 */
            if (inst->operands[1].type == OPERAND_REGISTER) return 12;
            /* SET name, imm -> MOVZ+MOVK X10,imm(8) + MOVZ+MOVK X9,addr(8)
             *                + STR X10,[X9](4) = 20 */
            return 20;
        case OP_GET:
            /* GET Rd, name -> MOVZ+MOVK X9,addr(8) + LDR Rd,[X9](4) = 12 */
            return 12;

        /* ---- New Phase-8 instructions --------------------------------- */
        case OP_LDS:    return 8;   /* MOVZ+MOVK Xd, addr (load string ptr) */
        case OP_LOADB:  return 4;   /* LDRB Wd, [Xn]  */
        case OP_STOREB: return 4;   /* STRB Wt, [Xn]  */
        case OP_SYS:    return 8;   /* MOV X8,X7 + SVC #0 */

        /* ---- Architecture-specific opcodes (ARM64) --------------------- */
        case OP_WFI:    return 4;   /* WFI:   D503207F */
        case OP_DMB:    return 4;   /* DMB SY: D5033FBF */

        /* ---- Assembler directives ------------------------------------- */
        case OP_ORG:    return 0;   /* handled specially in pass 1 */

        default:        return 0;
    }
}

/* =========================================================================
 *  Variable table for ARM64
 * ========================================================================= */
#define A64_MAX_VARS   256
#define A64_VAR_SIZE   8      /* bytes per variable (64-bit) */

typedef struct {
    char    name[UA_MAX_LABEL_LEN];
    int64_t init_value;
    int     has_init;
} A64VarEntry;

typedef struct {
    A64VarEntry vars[A64_MAX_VARS];
    int         count;
} A64VarTable;

static void a64_vartab_init(A64VarTable *vt) { vt->count = 0; }

/* =========================================================================
 *  Buffer table for ARM64  —  collects BUFFER declarations
 * =========================================================================
 *  Each buffer is a named zero-initialised region of N bytes.
 * ========================================================================= */
#define A64_MAX_BUFS   256

typedef struct {
    char name[UA_MAX_LABEL_LEN];
    int  size;
} A64BufEntry;

typedef struct {
    A64BufEntry bufs[A64_MAX_BUFS];
    int         count;
    int         total_size;
} A64BufTable;

static void a64_buftab_init(A64BufTable *bt) {
    bt->count = 0;
    bt->total_size = 0;
}

static int a64_buftab_add(A64BufTable *bt, const char *name, int size) {
    for (int i = 0; i < bt->count; i++) {
        if (strcmp(bt->bufs[i].name, name) == 0) {
            fprintf(stderr, "ARM64: duplicate buffer '%s'\n", name);
            return -1;
        }
    }
    if (bt->count >= A64_MAX_BUFS) {
        fprintf(stderr, "ARM64: buffer table overflow (max %d)\n",
                A64_MAX_BUFS);
        return -1;
    }
    A64BufEntry *b = &bt->bufs[bt->count++];
    strncpy(b->name, name, UA_MAX_LABEL_LEN - 1);
    b->name[UA_MAX_LABEL_LEN - 1] = '\0';
    b->size = size;
    bt->total_size += size;
    return 0;
}

static int a64_buftab_has(const A64BufTable *bt, const char *name) {
    for (int i = 0; i < bt->count; i++) {
        if (strcmp(bt->bufs[i].name, name) == 0) return 1;
    }
    return 0;
}

/* =========================================================================
 *  String table for ARM64  —  collects LDS string literals
 * ========================================================================= */
#define A64_MAX_STRINGS 256

typedef struct {
    const char *text;
    int         offset;
    int         length;
} A64StringEntry;

typedef struct {
    A64StringEntry strings[A64_MAX_STRINGS];
    int            count;
    int            total_size;
} A64StringTable;

static void a64_strtab_init(A64StringTable *st) {
    st->count = 0;
    st->total_size = 0;
}

static int a64_strtab_add(A64StringTable *st, const char *text) {
    for (int i = 0; i < st->count; i++)
        if (strcmp(st->strings[i].text, text) == 0) return i;
    if (st->count >= A64_MAX_STRINGS) {
        fprintf(stderr, "ARM64: string table overflow (max %d)\n",
                A64_MAX_STRINGS);
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

static int a64_vartab_add(A64VarTable *vt, const char *name,
                           int64_t init_value, int has_init)
{
    for (int i = 0; i < vt->count; i++) {
        if (strcmp(vt->vars[i].name, name) == 0) {
            fprintf(stderr, "ARM64: duplicate variable '%s'\n", name);
            return -1;
        }
    }
    if (vt->count >= A64_MAX_VARS) {
        fprintf(stderr, "ARM64: variable table overflow (max %d)\n",
                A64_MAX_VARS);
        return -1;
    }
    A64VarEntry *v = &vt->vars[vt->count++];
    strncpy(v->name, name, UA_MAX_LABEL_LEN - 1);
    v->name[UA_MAX_LABEL_LEN - 1] = '\0';
    v->init_value = init_value;
    v->has_init   = has_init;
    return 0;
}

/* =========================================================================
 *  generate_arm64()  —  main entry point  (two-pass)
 * ========================================================================= */
CodeBuffer* generate_arm64(const Instruction *ir, int ir_count)
{
    fprintf(stderr, "[ARM64] Generating code for %d IR instructions ...\n",
            ir_count);

    /* --- Pass 1: collect label addresses + variable declarations ------- */
    A64SymTab symtab;
    a64_symtab_init(&symtab);

    A64VarTable vartab;
    a64_vartab_init(&vartab);

    A64BufTable buftab;
    a64_buftab_init(&buftab);

    A64StringTable strtab;
    a64_strtab_init(&strtab);

    int pc = 0;
    for (int i = 0; i < ir_count; i++) {
        const Instruction *inst = &ir[i];
        if (inst->is_label) {
            a64_symtab_add(&symtab, inst->label_name, pc);
        } else if (inst->opcode == OP_VAR) {
            const char *vname = inst->operands[0].data.label;
            int64_t init_val  = 0;
            int     has_init  = 0;
            if (inst->operand_count >= 2 &&
                inst->operands[1].type == OPERAND_IMMEDIATE) {
                init_val = inst->operands[1].data.imm;
                has_init = 1;
            }
            a64_vartab_add(&vartab, vname, init_val, has_init);
        } else if (inst->opcode == OP_BUFFER) {
            const char *bname = inst->operands[0].data.label;
            int bsize = (int)inst->operands[1].data.imm;
            a64_buftab_add(&buftab, bname, bsize);
        } else if (inst->opcode == OP_ORG) {
            uint32_t target = (uint32_t)inst->operands[0].data.imm;
            if ((int)target < pc) {
                fprintf(stderr, "Error: @ORG 0x%X would move address "
                        "backwards (current PC = 0x%X)\n",
                        target, (unsigned)pc);
                exit(1);
            }
            pc = (int)target;
        } else {
            if (inst->opcode == OP_LDS)
                a64_strtab_add(&strtab, inst->operands[1].data.string);
            pc += instruction_size_a64(inst);
        }
    }

    /* Register variable symbols */
    int var_base = pc;
    for (int v = 0; v < vartab.count; v++) {
        a64_symtab_add(&symtab, vartab.vars[v].name,
                       var_base + v * A64_VAR_SIZE);
    }

    /* Register buffer symbols */
    int buf_base = var_base + vartab.count * A64_VAR_SIZE;
    {
        int buf_offset = 0;
        for (int b = 0; b < buftab.count; b++) {
            a64_symtab_add(&symtab, buftab.bufs[b].name,
                           buf_base + buf_offset);
            buf_offset += buftab.bufs[b].size;
        }
    }
    int str_base = buf_base + buftab.total_size;

    /* --- Pass 2: code emission ----------------------------------------- */
    CodeBuffer *code = create_code_buffer();
    if (!code) {
        fprintf(stderr, "UA ARM64: out of memory\n");
        return NULL;
    }

    for (int i = 0; i < ir_count; i++) {
        const Instruction *inst = &ir[i];

        if (inst->is_label)
            continue;

        switch (inst->opcode) {

        /* ---- LDI Rd, #imm  ->  MOVZ [+ MOVK] ------------ 4-8 bytes -- */
        case OP_LDI: {
            int rd = inst->operands[0].data.reg;
            int32_t imm = (int32_t)inst->operands[1].data.imm;
            a64_validate_register(inst, rd);
            uint8_t enc = A64_REG_ENC[rd];
            fprintf(stderr, "  LDI R%d -> MOVZ %s, #%d\n",
                    rd, A64_REG_NAME[rd], imm);
            emit_a64_load_imm32(code, enc, imm);
            break;
        }

        /* ---- MOV Rd, Rs  ->  ORR Xd, XZR, Xn ------------- 4 bytes -- */
        case OP_MOV: {
            int rd = inst->operands[0].data.reg;
            int rs = inst->operands[1].data.reg;
            a64_validate_register(inst, rd);
            a64_validate_register(inst, rs);
            fprintf(stderr, "  MOV R%d, R%d -> MOV %s, %s\n",
                    rd, rs, A64_REG_NAME[rd], A64_REG_NAME[rs]);
            emit_a64_mov_reg(code, A64_REG_ENC[rd], A64_REG_ENC[rs]);
            break;
        }

        /* ---- LOAD Rd, Rs  ->  LDR Xd, [Xn] --------------- 4 bytes --- */
        case OP_LOAD: {
            int rd = inst->operands[0].data.reg;
            int rs = inst->operands[1].data.reg;
            a64_validate_register(inst, rd);
            a64_validate_register(inst, rs);
            fprintf(stderr, "  LOAD R%d, R%d -> LDR %s, [%s]\n",
                    rd, rs, A64_REG_NAME[rd], A64_REG_NAME[rs]);
            emit_a64_ldr(code, A64_REG_ENC[rd], A64_REG_ENC[rs]);
            break;
        }

        /* ---- STORE Rx, Ry  ->  STR Xy, [Xx] -------------- 4 bytes --- */
        case OP_STORE: {
            int rx = inst->operands[0].data.reg;
            int ry = inst->operands[1].data.reg;
            a64_validate_register(inst, rx);
            a64_validate_register(inst, ry);
            fprintf(stderr, "  STORE R%d, R%d -> STR %s, [%s]\n",
                    rx, ry, A64_REG_NAME[ry], A64_REG_NAME[rx]);
            emit_a64_str(code, A64_REG_ENC[ry], A64_REG_ENC[rx]);
            break;
        }

        /* ---- ADD Rd, Rs/imm -------------------------------- 4/4-12 --- */
        case OP_ADD: {
            int rd = inst->operands[0].data.reg;
            a64_validate_register(inst, rd);
            uint8_t enc_d = A64_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                a64_validate_register(inst, rs);
                fprintf(stderr, "  ADD R%d, R%d -> ADD %s, %s, %s\n",
                        rd, rs, A64_REG_NAME[rd], A64_REG_NAME[rd],
                        A64_REG_NAME[rs]);
                emit_a64_add_reg(code, enc_d, enc_d, A64_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint32_t uval = (uint32_t)(imm < 0 ? -imm : imm);
                if (uval <= 0xFFF) {
                    if (imm >= 0) {
                        fprintf(stderr, "  ADD R%d, #%d\n", rd, imm);
                        emit_a64_add_imm(code, enc_d, enc_d, (uint16_t)imm);
                    } else {
                        fprintf(stderr, "  ADD R%d, #%d -> SUB #%d\n",
                                rd, imm, -imm);
                        emit_a64_sub_imm(code, enc_d, enc_d, (uint16_t)(-imm));
                    }
                } else {
                    fprintf(stderr, "  ADD R%d, #%d -> MOVZ X9; ADD\n",
                            rd, imm);
                    emit_a64_load_imm32(code, A64_REG_SCRATCH, imm);
                    emit_a64_add_reg(code, enc_d, enc_d, A64_REG_SCRATCH);
                }
            }
            break;
        }

        /* ---- SUB Rd, Rs/imm -------------------------------- 4/4-12 --- */
        case OP_SUB: {
            int rd = inst->operands[0].data.reg;
            a64_validate_register(inst, rd);
            uint8_t enc_d = A64_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                a64_validate_register(inst, rs);
                fprintf(stderr, "  SUB R%d, R%d -> SUB %s, %s, %s\n",
                        rd, rs, A64_REG_NAME[rd], A64_REG_NAME[rd],
                        A64_REG_NAME[rs]);
                emit_a64_sub_reg(code, enc_d, enc_d, A64_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint32_t uval = (uint32_t)(imm < 0 ? -imm : imm);
                if (uval <= 0xFFF) {
                    if (imm >= 0) {
                        fprintf(stderr, "  SUB R%d, #%d\n", rd, imm);
                        emit_a64_sub_imm(code, enc_d, enc_d, (uint16_t)imm);
                    } else {
                        fprintf(stderr, "  SUB R%d, #%d -> ADD #%d\n",
                                rd, imm, -imm);
                        emit_a64_add_imm(code, enc_d, enc_d, (uint16_t)(-imm));
                    }
                } else {
                    fprintf(stderr, "  SUB R%d, #%d -> MOVZ X9; SUB\n",
                            rd, imm);
                    emit_a64_load_imm32(code, A64_REG_SCRATCH, imm);
                    emit_a64_sub_reg(code, enc_d, enc_d, A64_REG_SCRATCH);
                }
            }
            break;
        }

        /* ---- AND Rd, Rs/imm -------------------------------- 4/12 ----- */
        case OP_AND: {
            int rd = inst->operands[0].data.reg;
            a64_validate_register(inst, rd);
            uint8_t enc_d = A64_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                a64_validate_register(inst, rs);
                fprintf(stderr, "  AND R%d, R%d -> AND %s, %s, %s\n",
                        rd, rs, A64_REG_NAME[rd], A64_REG_NAME[rd],
                        A64_REG_NAME[rs]);
                emit_a64_and_reg(code, enc_d, enc_d, A64_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                fprintf(stderr, "  AND R%d, #%d -> MOVZ X9; AND\n", rd, imm);
                emit_a64_load_imm32_full(code, A64_REG_SCRATCH, imm);
                emit_a64_and_reg(code, enc_d, enc_d, A64_REG_SCRATCH);
            }
            break;
        }

        /* ---- OR Rd, Rs/imm --------------------------------- 4/12 ----- */
        case OP_OR: {
            int rd = inst->operands[0].data.reg;
            a64_validate_register(inst, rd);
            uint8_t enc_d = A64_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                a64_validate_register(inst, rs);
                fprintf(stderr, "  OR  R%d, R%d -> ORR %s, %s, %s\n",
                        rd, rs, A64_REG_NAME[rd], A64_REG_NAME[rd],
                        A64_REG_NAME[rs]);
                emit_a64_orr_reg(code, enc_d, enc_d, A64_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                fprintf(stderr, "  OR  R%d, #%d -> MOVZ X9; ORR\n", rd, imm);
                emit_a64_load_imm32_full(code, A64_REG_SCRATCH, imm);
                emit_a64_orr_reg(code, enc_d, enc_d, A64_REG_SCRATCH);
            }
            break;
        }

        /* ---- XOR Rd, Rs/imm -------------------------------- 4/12 ----- */
        case OP_XOR: {
            int rd = inst->operands[0].data.reg;
            a64_validate_register(inst, rd);
            uint8_t enc_d = A64_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                a64_validate_register(inst, rs);
                fprintf(stderr, "  XOR R%d, R%d -> EOR %s, %s, %s\n",
                        rd, rs, A64_REG_NAME[rd], A64_REG_NAME[rd],
                        A64_REG_NAME[rs]);
                emit_a64_eor_reg(code, enc_d, enc_d, A64_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                fprintf(stderr, "  XOR R%d, #%d -> MOVZ X9; EOR\n", rd, imm);
                emit_a64_load_imm32_full(code, A64_REG_SCRATCH, imm);
                emit_a64_eor_reg(code, enc_d, enc_d, A64_REG_SCRATCH);
            }
            break;
        }

        /* ---- NOT Rd  ->  MVN Xd, Xd ------------------------ 4 bytes -- */
        case OP_NOT: {
            int rd = inst->operands[0].data.reg;
            a64_validate_register(inst, rd);
            fprintf(stderr, "  NOT R%d -> MVN %s, %s\n",
                    rd, A64_REG_NAME[rd], A64_REG_NAME[rd]);
            emit_a64_mvn(code, A64_REG_ENC[rd], A64_REG_ENC[rd]);
            break;
        }

        /* ---- INC Rd  ->  ADD Xd, Xd, #1 -------------------- 4 bytes -- */
        case OP_INC: {
            int rd = inst->operands[0].data.reg;
            a64_validate_register(inst, rd);
            fprintf(stderr, "  INC R%d -> ADD %s, %s, #1\n",
                    rd, A64_REG_NAME[rd], A64_REG_NAME[rd]);
            emit_a64_add_imm(code, A64_REG_ENC[rd], A64_REG_ENC[rd], 1);
            break;
        }

        /* ---- DEC Rd  ->  SUB Xd, Xd, #1 -------------------- 4 bytes -- */
        case OP_DEC: {
            int rd = inst->operands[0].data.reg;
            a64_validate_register(inst, rd);
            fprintf(stderr, "  DEC R%d -> SUB %s, %s, #1\n",
                    rd, A64_REG_NAME[rd], A64_REG_NAME[rd]);
            emit_a64_sub_imm(code, A64_REG_ENC[rd], A64_REG_ENC[rd], 1);
            break;
        }

        /* ---- MUL Rd, Rs/imm  ->  MUL Xd, Xd, Xm ------ 4/12 bytes --- */
        case OP_MUL: {
            int rd = inst->operands[0].data.reg;
            a64_validate_register(inst, rd);
            uint8_t enc_d = A64_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                a64_validate_register(inst, rs);
                fprintf(stderr, "  MUL R%d, R%d -> MUL %s, %s, %s\n",
                        rd, rs, A64_REG_NAME[rd], A64_REG_NAME[rd],
                        A64_REG_NAME[rs]);
                emit_a64_mul(code, enc_d, enc_d, A64_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                fprintf(stderr, "  MUL R%d, #%d -> MOVZ X9; MUL\n", rd, imm);
                emit_a64_load_imm32_full(code, A64_REG_SCRATCH, imm);
                emit_a64_mul(code, enc_d, enc_d, A64_REG_SCRATCH);
            }
            break;
        }

        /* ---- DIV Rd, Rs/imm  ->  SDIV Xd, Xd, Xm ---- 4/12 bytes ---- */
        case OP_DIV: {
            int rd = inst->operands[0].data.reg;
            a64_validate_register(inst, rd);
            uint8_t enc_d = A64_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                a64_validate_register(inst, rs);
                fprintf(stderr, "  DIV R%d, R%d -> SDIV %s, %s, %s\n",
                        rd, rs, A64_REG_NAME[rd], A64_REG_NAME[rd],
                        A64_REG_NAME[rs]);
                emit_a64_sdiv(code, enc_d, enc_d, A64_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                fprintf(stderr, "  DIV R%d, #%d -> MOVZ X9; SDIV\n", rd, imm);
                emit_a64_load_imm32_full(code, A64_REG_SCRATCH, imm);
                emit_a64_sdiv(code, enc_d, enc_d, A64_REG_SCRATCH);
            }
            break;
        }

        /* ---- SHL Rd, Rs/imm -------------------------------- 4 bytes -- */
        case OP_SHL: {
            int rd = inst->operands[0].data.reg;
            a64_validate_register(inst, rd);
            uint8_t enc_d = A64_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_IMMEDIATE) {
                uint8_t shamt = (uint8_t)(inst->operands[1].data.imm & 0x3F);
                fprintf(stderr, "  SHL R%d, #%d -> LSL %s, %s, #%d\n",
                        rd, shamt, A64_REG_NAME[rd], A64_REG_NAME[rd], shamt);
                emit_a64_lsl_imm(code, enc_d, enc_d, shamt);
            } else {
                int rs = inst->operands[1].data.reg;
                a64_validate_register(inst, rs);
                fprintf(stderr, "  SHL R%d, R%d -> LSLV %s, %s, %s\n",
                        rd, rs, A64_REG_NAME[rd], A64_REG_NAME[rd],
                        A64_REG_NAME[rs]);
                emit_a64_lslv(code, enc_d, enc_d, A64_REG_ENC[rs]);
            }
            break;
        }

        /* ---- SHR Rd, Rs/imm -------------------------------- 4 bytes -- */
        case OP_SHR: {
            int rd = inst->operands[0].data.reg;
            a64_validate_register(inst, rd);
            uint8_t enc_d = A64_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_IMMEDIATE) {
                uint8_t shamt = (uint8_t)(inst->operands[1].data.imm & 0x3F);
                fprintf(stderr, "  SHR R%d, #%d -> LSR %s, %s, #%d\n",
                        rd, shamt, A64_REG_NAME[rd], A64_REG_NAME[rd], shamt);
                emit_a64_lsr_imm(code, enc_d, enc_d, shamt);
            } else {
                int rs = inst->operands[1].data.reg;
                a64_validate_register(inst, rs);
                fprintf(stderr, "  SHR R%d, R%d -> LSRV %s, %s, %s\n",
                        rd, rs, A64_REG_NAME[rd], A64_REG_NAME[rd],
                        A64_REG_NAME[rs]);
                emit_a64_lsrv(code, enc_d, enc_d, A64_REG_ENC[rs]);
            }
            break;
        }

        /* ---- CMP Ra, Rb/imm  ->  SUBS XZR, Xa, Xb/imm12  4/4-12 ----- */
        case OP_CMP: {
            int ra = inst->operands[0].data.reg;
            a64_validate_register(inst, ra);
            uint8_t enc_a = A64_REG_ENC[ra];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rb = inst->operands[1].data.reg;
                a64_validate_register(inst, rb);
                fprintf(stderr, "  CMP R%d, R%d -> CMP %s, %s\n",
                        ra, rb, A64_REG_NAME[ra], A64_REG_NAME[rb]);
                emit_a64_cmp_reg(code, enc_a, A64_REG_ENC[rb]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint32_t uval = (uint32_t)(imm < 0 ? -imm : imm);
                if (uval <= 0xFFF && imm >= 0) {
                    fprintf(stderr, "  CMP R%d, #%d\n", ra, imm);
                    emit_a64_cmp_imm(code, enc_a, (uint16_t)imm);
                } else {
                    fprintf(stderr, "  CMP R%d, #%d -> MOVZ X9; CMP\n",
                            ra, imm);
                    emit_a64_load_imm32(code, A64_REG_SCRATCH, imm);
                    emit_a64_cmp_reg(code, enc_a, A64_REG_SCRATCH);
                }
            }
            break;
        }

        /* ---- JMP label  ->  B label ----------------------- 4 bytes --- */
        case OP_JMP: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  JMP %s -> B\n", label);
            int patch_off = code->size;
            emit_a64_placeholder(code);
            a64_add_fixup(&symtab, label, patch_off, patch_off, inst->line,
                          A64_FIXUP_B, 0);
            break;
        }

        /* ---- JZ label  ->  B.EQ label --------------------- 4 bytes --- */
        case OP_JZ: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  JZ  %s -> B.EQ\n", label);
            int patch_off = code->size;
            emit_a64_placeholder(code);
            a64_add_fixup(&symtab, label, patch_off, patch_off, inst->line,
                          A64_FIXUP_BCOND, A64_COND_EQ);
            break;
        }

        /* ---- JNZ label  ->  B.NE label -------------------- 4 bytes --- */
        case OP_JNZ: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  JNZ %s -> B.NE\n", label);
            int patch_off = code->size;
            emit_a64_placeholder(code);
            a64_add_fixup(&symtab, label, patch_off, patch_off, inst->line,
                          A64_FIXUP_BCOND, A64_COND_NE);
            break;
        }

        /* ---- JL label  ->  B.LT label --------------------- 4 bytes --- */
        case OP_JL: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  JL  %s -> B.LT\n", label);
            int patch_off = code->size;
            emit_a64_placeholder(code);
            a64_add_fixup(&symtab, label, patch_off, patch_off, inst->line,
                          A64_FIXUP_BCOND, A64_COND_LT);
            break;
        }

        /* ---- JG label  ->  B.GT label --------------------- 4 bytes --- */
        case OP_JG: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  JG  %s -> B.GT\n", label);
            int patch_off = code->size;
            emit_a64_placeholder(code);
            a64_add_fixup(&symtab, label, patch_off, patch_off, inst->line,
                          A64_FIXUP_BCOND, A64_COND_GT);
            break;
        }

        /* ---- CALL label  ->  BL label --------------------- 4 bytes --- */
        case OP_CALL: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  CALL %s -> BL\n", label);
            int patch_off = code->size;
            emit_a64_placeholder(code);
            a64_add_fixup(&symtab, label, patch_off, patch_off, inst->line,
                          A64_FIXUP_BL, 0);
            break;
        }

        /* ---- RET  ->  RET X30 ------------------------------ 4 bytes -- */
        case OP_RET:
            fprintf(stderr, "  RET -> RET X30\n");
            emit_a64_ret(code, A64_REG_LR);
            break;

        /* ---- PUSH Rs  ->  STR Xd, [SP, #-16]! ------------ 4 bytes --- */
        case OP_PUSH: {
            int rs = inst->operands[0].data.reg;
            a64_validate_register(inst, rs);
            fprintf(stderr, "  PUSH R%d -> STR %s, [SP, #-16]!\n",
                    rs, A64_REG_NAME[rs]);
            emit_a64_push(code, A64_REG_ENC[rs]);
            break;
        }

        /* ---- POP Rd  ->  LDR Xd, [SP], #16 --------------- 4 bytes --- */
        case OP_POP: {
            int rd = inst->operands[0].data.reg;
            a64_validate_register(inst, rd);
            fprintf(stderr, "  POP  R%d -> LDR %s, [SP], #16\n",
                    rd, A64_REG_NAME[rd]);
            emit_a64_pop(code, A64_REG_ENC[rd]);
            break;
        }

        /* ---- NOP -------------------------------------------- 4 bytes -- */
        case OP_NOP:
            fprintf(stderr, "  NOP\n");
            emit_a64_nop(code);
            break;

        /* ---- HLT  ->  RET X30 ------------------------------ 4 bytes -- */
        case OP_HLT:
            fprintf(stderr, "  HLT -> RET X30\n");
            emit_a64_ret(code, A64_REG_LR);
            break;

        /* ---- INT #imm  ->  SVC #imm ------------------------ 4 bytes -- */
        case OP_INT: {
            uint16_t imm = (uint16_t)(inst->operands[0].data.imm & 0xFFFF);
            fprintf(stderr, "  INT #%d -> SVC #%d\n", (int)imm, (int)imm);
            emit_a64_svc(code, imm);
            break;
        }

        /* ---- VAR — declaration only, no code emitted ------------------ */
        case OP_VAR:
            break;

        /* ---- BUFFER — declaration only, no code emitted --------------- */
        case OP_BUFFER:
            break;

        /* ---- ORG addr — pad with zeros until target address ----------- */
        case OP_ORG: {
            uint32_t target = (uint32_t)inst->operands[0].data.imm;
            while (code->size < (int)target) {
                emit_byte(code, 0x00);
            }
            break;
        }

        /* ---- SET name, Rs/imm — store to variable --------------------- */
        case OP_SET: {
            const char *vname = inst->operands[0].data.label;
            int var_addr = a64_symtab_lookup(&symtab, vname);
            if (var_addr < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "undefined variable '%s'", vname);
                a64_error(inst, msg);
            }
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                a64_validate_register(inst, rs);
                fprintf(stderr, "  SET %s, R%d -> STR %s, [X9]\n",
                        vname, rs, A64_REG_NAME[rs]);
                emit_a64_load_imm32_full(code, A64_REG_SCRATCH,
                                          (int32_t)var_addr);
                emit_a64_str(code, A64_REG_ENC[rs], A64_REG_SCRATCH);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                fprintf(stderr, "  SET %s, #%d -> STR X10, [X9]\n",
                        vname, imm);
                emit_a64_load_imm32_full(code, A64_REG_SCRATCH2, imm);
                emit_a64_load_imm32_full(code, A64_REG_SCRATCH,
                                          (int32_t)var_addr);
                emit_a64_str(code, A64_REG_SCRATCH2, A64_REG_SCRATCH);
            }
            break;
        }

        /* ---- GET Rd, name — load from variable or buffer address ------ */
        case OP_GET: {
            int rd = inst->operands[0].data.reg;
            const char *vname = inst->operands[1].data.label;
            a64_validate_register(inst, rd);
            int var_addr = a64_symtab_lookup(&symtab, vname);
            if (var_addr < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "undefined variable '%s'", vname);
                a64_error(inst, msg);
            }
            int is_buf = a64_buftab_has(&buftab, vname);
            if (is_buf) {
                fprintf(stderr, "  GET R%d, %s -> MOVZ+MOVK %s, #%d (buffer address)\n",
                        rd, vname, A64_REG_NAME[rd], var_addr);
                /* Load address into X9, then MOV Xd, X9 */
                emit_a64_load_imm32_full(code, A64_REG_SCRATCH,
                                          (int32_t)var_addr);
                emit_a64_mov_reg(code, A64_REG_ENC[rd], A64_REG_SCRATCH);
            } else {
                fprintf(stderr, "  GET R%d, %s -> LDR %s, [X9]\n",
                        rd, vname, A64_REG_NAME[rd]);
                emit_a64_load_imm32_full(code, A64_REG_SCRATCH,
                                          (int32_t)var_addr);
                emit_a64_ldr(code, A64_REG_ENC[rd], A64_REG_SCRATCH);
            }
            break;
        }

        /* ---- LDS Rd, "str"  ->  MOVZ+MOVK Xd, addr ------- 8 bytes --- */
        case OP_LDS: {
            int rd = inst->operands[0].data.reg;
            const char *str = inst->operands[1].data.string;
            a64_validate_register(inst, rd);
            int str_idx = a64_strtab_add(&strtab, str);
            int str_addr = str_base + strtab.strings[str_idx].offset;
            fprintf(stderr, "  LDS R%d, \"%s\" -> MOVZ+MOVK %s, #%d\n",
                    rd, str, A64_REG_NAME[rd], str_addr);
            emit_a64_load_imm32_full(code, A64_REG_ENC[rd],
                                     (int32_t)str_addr);
            break;
        }

        /* ---- LOADB Rd, Rs  ->  LDRB Wd, [Xn] ------------- 4 bytes --- */
        case OP_LOADB: {
            int rd = inst->operands[0].data.reg;
            int rs = inst->operands[1].data.reg;
            a64_validate_register(inst, rd);
            a64_validate_register(inst, rs);
            fprintf(stderr, "  LOADB R%d, R%d -> LDRB W%d, [X%d]\n",
                    rd, rs, A64_REG_ENC[rd], A64_REG_ENC[rs]);
            /* LDRB (unsigned offset): size=00, V=0, opc=01
             * 0011 1001 01 imm12 Rn Rt  (imm12=0) */
            {
                uint32_t word = 0x39400000u
                              | ((uint32_t)A64_REG_ENC[rs] << 5)
                              | (uint32_t)A64_REG_ENC[rd];
                emit_a64(code, word);
            }
            break;
        }

        /* ---- STOREB Rs, Rd  ->  STRB Wt, [Xn] ------------ 4 bytes --- */
        case OP_STOREB: {
            int rx = inst->operands[0].data.reg;
            int ry = inst->operands[1].data.reg;
            a64_validate_register(inst, rx);
            a64_validate_register(inst, ry);
            fprintf(stderr, "  STOREB R%d, R%d -> STRB W%d, [X%d]\n",
                    rx, ry, A64_REG_ENC[rx], A64_REG_ENC[ry]);
            /* STRB (unsigned offset): size=00, V=0, opc=00
             * 0011 1001 00 imm12 Rn Rt  (imm12=0) */
            {
                uint32_t word = 0x39000000u
                              | ((uint32_t)A64_REG_ENC[ry] << 5)
                              | (uint32_t)A64_REG_ENC[rx];
                emit_a64(code, word);
            }
            break;
        }

        /* ---- SYS  ->  MOV X8,X7 + SVC #0 ---------------- 8 bytes --- */
        case OP_SYS:
            fprintf(stderr, "  SYS -> MOV X8,X7 + SVC #0\n");
            /* Move syscall number from R7 (X7) to X8 (Linux ABI).
             * MOV X8, X7 is ORR X8, XZR, X7 = 0xAA0703E8 */
            emit_a64(code, 0xAA0703E8u);
            emit_a64_svc(code, 0);
            break;

        /* ---- WFI ------------------------------------------ 4 bytes --- */
        case OP_WFI:
            fprintf(stderr, "  WFI\n");
            emit_a64(code, 0xD503207Fu);   /* HINT #3 = WFI */
            break;

        /* ---- DMB SY --------------------------------------- 4 bytes --- */
        case OP_DMB:
            fprintf(stderr, "  DMB SY\n");
            emit_a64(code, 0xD5033FBFu);   /* DMB SY */
            break;

        default: {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "opcode '%s' is not supported by the ARM64 backend",
                     opcode_name(inst->opcode));
            a64_error(inst, msg);
            break;
        }
        }
    }

    /* --- Pass 3: patch branch relocations ------------------------------ */
    for (int f = 0; f < symtab.fix_count; f++) {
        A64Fixup *fix = &symtab.fixups[f];
        int target = a64_symtab_lookup(&symtab, fix->label);
        if (target < 0) {
            fprintf(stderr,
                    "ARM64: undefined label or variable '%s' (line %d)\n",
                    fix->label, fix->line);
            free_code_buffer(code);
            return NULL;
        }
        int32_t offset = (int32_t)(target - fix->instr_addr);

        if (fix->fixup_type == A64_FIXUP_B) {
            /* B: ±128 MiB range (26-bit signed offset, <<2) */
            int32_t imm26 = offset >> 2;
            if (imm26 < -(1 << 25) || imm26 >= (1 << 25)) {
                fprintf(stderr,
                        "ARM64: B target '%s' out of range (line %d)\n",
                        fix->label, fix->line);
                free_code_buffer(code);
                return NULL;
            }
            uint32_t word = (0x05u << 26)
                          | ((uint32_t)imm26 & 0x03FFFFFF);
            patch_a64_word(code, fix->patch_offset, word);
        }
        else if (fix->fixup_type == A64_FIXUP_BL) {
            /* BL: same range as B */
            int32_t imm26 = offset >> 2;
            if (imm26 < -(1 << 25) || imm26 >= (1 << 25)) {
                fprintf(stderr,
                        "ARM64: BL target '%s' out of range (line %d)\n",
                        fix->label, fix->line);
                free_code_buffer(code);
                return NULL;
            }
            uint32_t word = (0x25u << 26)
                          | ((uint32_t)imm26 & 0x03FFFFFF);
            patch_a64_word(code, fix->patch_offset, word);
        }
        else {
            /* B.cond: ±1 MiB range (19-bit signed offset, <<2) */
            int32_t imm19 = offset >> 2;
            if (imm19 < -(1 << 18) || imm19 >= (1 << 18)) {
                fprintf(stderr,
                        "ARM64: B.cond target '%s' out of range (line %d)\n",
                        fix->label, fix->line);
                free_code_buffer(code);
                return NULL;
            }
            uint32_t word = (0x54u << 24)
                          | (((uint32_t)imm19 & 0x7FFFF) << 5)
                          | ((uint32_t)(fix->cond & 0xF));
            patch_a64_word(code, fix->patch_offset, word);
        }
    }

    /* --- Append variable data section --------------------------------- */
    int data_start = code->size;
    for (int v = 0; v < vartab.count; v++) {
        int64_t val = vartab.vars[v].has_init ? vartab.vars[v].init_value : 0;
        for (int b = 0; b < A64_VAR_SIZE; b++) {
            emit_byte(code, (uint8_t)((val >> (b * 8)) & 0xFF));
        }
    }

    /* --- Append buffer data section (zero-filled) --------------------- */
    for (int b = 0; b < buftab.count; b++) {
        for (int z = 0; z < buftab.bufs[b].size; z++)
            emit_byte(code, 0x00);
    }

    /* --- Append string data section ----------------------------------- */
    for (int s = 0; s < strtab.count; s++) {
        const char *p = strtab.strings[s].text;
        int len = strtab.strings[s].length;
        for (int b = 0; b < len; b++)
            emit_byte(code, (uint8_t)p[b]);
        emit_byte(code, 0x00);
    }

    fprintf(stderr, "[ARM64] Emitted %d bytes (%d code + %d var + %d buf + %d str)\n",
            code->size, data_start,
            vartab.count * A64_VAR_SIZE, buftab.total_size,
            strtab.total_size);
    return code;
}
