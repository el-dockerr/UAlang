/*
 * =============================================================================
 *  UA - Unified Assembler
 *  RISC-V Back-End (Code Generation)
 *
 *  File:    backend_risc_v.c
 *  Purpose: Translate UA IR into raw RISC-V (RV64I + RV64M) machine code.
 *
 *  ┌──────────────────────────────────────────────────────────────────────┐
 *  │  RISC-V Instruction Encoding Reference (RV64I + RV64M)            │
 *  │                                                                    │
 *  │  All instructions are 32 bits, little-endian.                      │
 *  │                                                                    │
 *  │  Instruction formats:                                              │
 *  │    R-type:  funct7[31:25] rs2[24:20] rs1[19:15] funct3[14:12]     │
 *  │             rd[11:7] opcode[6:0]                                   │
 *  │    I-type:  imm[31:20] rs1[19:15] funct3[14:12] rd[11:7]          │
 *  │             opcode[6:0]                                            │
 *  │    S-type:  imm[31:25] rs2[24:20] rs1[19:15] funct3[14:12]        │
 *  │             imm[11:7] opcode[6:0]                                  │
 *  │    B-type:  imm[12|10:5] rs2[24:20] rs1[19:15] funct3[14:12]      │
 *  │             imm[4:1|11] opcode[6:0]                                │
 *  │    U-type:  imm[31:12] rd[11:7] opcode[6:0]                       │
 *  │    J-type:  imm[20|10:1|11|19:12] rd[11:7] opcode[6:0]            │
 *  │                                                                    │
 *  │  Opcodes used:                                                     │
 *  │    0x33 = OP       (R-type ALU)                                    │
 *  │    0x3B = OP-32    (R-type ALU, 32-bit)                            │
 *  │    0x13 = OP-IMM   (I-type ALU)                                    │
 *  │    0x03 = LOAD     (I-type loads)                                  │
 *  │    0x23 = STORE    (S-type stores)                                 │
 *  │    0x63 = BRANCH   (B-type conditional branches)                   │
 *  │    0x6F = JAL      (J-type jump and link)                          │
 *  │    0x67 = JALR     (I-type jump and link register)                 │
 *  │    0x37 = LUI      (U-type load upper immediate)                   │
 *  │    0x73 = SYSTEM   (ECALL, EBREAK)                                 │
 *  │                                                                    │
 *  │  Register mapping:                                                 │
 *  │    UA R0-R7 -> x10-x17 (a0-a7, argument/return registers)         │
 *  │    Scratch  -> x5 (t0), x6 (t1)                                   │
 *  │    SP       -> x2                                                  │
 *  │    RA       -> x1                                                  │
 *  └──────────────────────────────────────────────────────────────────────┘
 *
 *  License: MIT
 * =============================================================================
 */

#include "backend_risc_v.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 *  RISC-V register encoding table
 * =========================================================================
 *  UA R0-R7 map to x10-x17 (a0-a7).
 * ========================================================================= */
#define RV_MAX_REG   8

static const uint8_t RV_REG_ENC[RV_MAX_REG] = {
    10, /* R0 -> x10 (a0) */
    11, /* R1 -> x11 (a1) */
    12, /* R2 -> x12 (a2) */
    13, /* R3 -> x13 (a3) */
    14, /* R4 -> x14 (a4) */
    15, /* R5 -> x15 (a5) */
    16, /* R6 -> x16 (a6) */
    17  /* R7 -> x17 (a7) */
};

static const char* RV_REG_NAME[RV_MAX_REG] = {
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"
};

/* Special registers */
#define RV_REG_ZERO  0   /* x0  — hardwired zero                */
#define RV_REG_RA    1   /* x1  — return address                */
#define RV_REG_SP    2   /* x2  — stack pointer                 */
#define RV_REG_T0    5   /* x5  — temporary / scratch           */
#define RV_REG_T1    6   /* x6  — temporary / scratch           */
#define RV_REG_T2    7   /* x7  — temporary / scratch           */

/* =========================================================================
 *  Error helpers
 * ========================================================================= */
static void rv_error(const Instruction *inst, const char *msg)
{
    fprintf(stderr,
            "\n"
            "  UA RISC-V Backend Error\n"
            "  ------------------------\n"
            "  Line %d, Column %d: %s\n\n",
            inst->line, inst->column, msg);
    exit(1);
}

static void rv_validate_register(const Instruction *inst, int reg)
{
    if (reg < 0 || reg >= RV_MAX_REG) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "register R%d is not mapped in the RISC-V backend "
                 "(supports R0-R7: a0-a7)",
                 reg);
        rv_error(inst, msg);
    }
}

/* =========================================================================
 *  Emit helper — emit a 32-bit RISC-V instruction (little-endian)
 * ========================================================================= */
static void emit_rv32(CodeBuffer *buf, uint32_t word)
{
    emit_byte(buf, (uint8_t)( word        & 0xFF));
    emit_byte(buf, (uint8_t)((word >>  8) & 0xFF));
    emit_byte(buf, (uint8_t)((word >> 16) & 0xFF));
    emit_byte(buf, (uint8_t)((word >> 24) & 0xFF));
}

/* =========================================================================
 *  RISC-V Instruction Format Encoders
 * ========================================================================= */

/* R-type: funct7[31:25] rs2[24:20] rs1[19:15] funct3[14:12] rd[11:7] op[6:0] */
static uint32_t rv_r_type(uint8_t funct7, uint8_t rs2, uint8_t rs1,
                           uint8_t funct3, uint8_t rd, uint8_t opcode)
{
    return ((uint32_t)(funct7 & 0x7F) << 25)
         | ((uint32_t)(rs2    & 0x1F) << 20)
         | ((uint32_t)(rs1    & 0x1F) << 15)
         | ((uint32_t)(funct3 & 0x07) << 12)
         | ((uint32_t)(rd     & 0x1F) <<  7)
         | ((uint32_t)(opcode & 0x7F));
}

/* I-type: imm[31:20] rs1[19:15] funct3[14:12] rd[11:7] opcode[6:0] */
static uint32_t rv_i_type(int32_t imm12, uint8_t rs1, uint8_t funct3,
                           uint8_t rd, uint8_t opcode)
{
    return ((uint32_t)(imm12  & 0xFFF) << 20)
         | ((uint32_t)(rs1    & 0x1F)  << 15)
         | ((uint32_t)(funct3 & 0x07)  << 12)
         | ((uint32_t)(rd     & 0x1F)  <<  7)
         | ((uint32_t)(opcode & 0x7F));
}

/* S-type: imm[31:25|11:7] rs2[24:20] rs1[19:15] funct3[14:12] opcode[6:0] */
static uint32_t rv_s_type(int32_t imm12, uint8_t rs2, uint8_t rs1,
                           uint8_t funct3, uint8_t opcode)
{
    uint32_t imm = (uint32_t)imm12;
    return ((imm >> 5) & 0x7F) << 25
         | ((uint32_t)(rs2    & 0x1F) << 20)
         | ((uint32_t)(rs1    & 0x1F) << 15)
         | ((uint32_t)(funct3 & 0x07) << 12)
         | ((imm & 0x1F) << 7)
         | ((uint32_t)(opcode & 0x7F));
}

/* B-type: imm[12|10:5] rs2 rs1 funct3 imm[4:1|11] opcode */
static uint32_t rv_b_type(int32_t offset, uint8_t rs2, uint8_t rs1,
                           uint8_t funct3, uint8_t opcode)
{
    uint32_t imm = (uint32_t)offset;
    uint32_t bit12  = (imm >> 12) & 0x1;
    uint32_t bit11  = (imm >> 11) & 0x1;
    uint32_t bit10_5 = (imm >> 5) & 0x3F;
    uint32_t bit4_1  = (imm >> 1) & 0xF;
    return (bit12 << 31)
         | (bit10_5 << 25)
         | ((uint32_t)(rs2 & 0x1F) << 20)
         | ((uint32_t)(rs1 & 0x1F) << 15)
         | ((uint32_t)(funct3 & 0x07) << 12)
         | (bit4_1 << 8)
         | (bit11 << 7)
         | ((uint32_t)(opcode & 0x7F));
}

/* U-type: imm[31:12] rd[11:7] opcode[6:0] */
static uint32_t rv_u_type(int32_t imm20, uint8_t rd, uint8_t opcode)
{
    return ((uint32_t)imm20 & 0xFFFFF000u)
         | ((uint32_t)(rd & 0x1F) << 7)
         | ((uint32_t)(opcode & 0x7F));
}

/* J-type: imm[20|10:1|11|19:12] rd[11:7] opcode[6:0] */
static uint32_t rv_j_type(int32_t offset, uint8_t rd, uint8_t opcode)
{
    uint32_t imm = (uint32_t)offset;
    uint32_t bit20    = (imm >> 20) & 0x1;
    uint32_t bit19_12 = (imm >> 12) & 0xFF;
    uint32_t bit11    = (imm >> 11) & 0x1;
    uint32_t bit10_1  = (imm >>  1) & 0x3FF;
    return (bit20 << 31)
         | (bit10_1 << 21)
         | (bit11 << 20)
         | (bit19_12 << 12)
         | ((uint32_t)(rd & 0x1F) << 7)
         | ((uint32_t)(opcode & 0x7F));
}

/* =========================================================================
 *  RISC-V Opcode constants
 * ========================================================================= */
#define RV_OP_LUI     0x37
#define RV_OP_AUIPC   0x17
#define RV_OP_JAL     0x6F
#define RV_OP_JALR    0x67
#define RV_OP_BRANCH  0x63
#define RV_OP_LOAD    0x03
#define RV_OP_STORE   0x23
#define RV_OP_OP_IMM  0x13
#define RV_OP_OP      0x33
#define RV_OP_SYSTEM  0x73

/* funct3 for branches */
#define RV_F3_BEQ     0x0
#define RV_F3_BNE     0x1

/* funct3 for loads */
#define RV_F3_LW      0x2   /* 32-bit load  */
#define RV_F3_LD      0x3   /* 64-bit load  */

/* funct3 for stores */
#define RV_F3_SW      0x2   /* 32-bit store */
#define RV_F3_SD      0x3   /* 64-bit store */

/* funct3 for ALU */
#define RV_F3_ADD     0x0   /* ADD/SUB (funct7 distinguishes) */
#define RV_F3_SLL     0x1
#define RV_F3_SRL     0x5   /* SRL/SRA (funct7 distinguishes) */
#define RV_F3_XOR     0x4
#define RV_F3_OR      0x6
#define RV_F3_AND     0x7

/* funct7 values */
#define RV_F7_NORMAL  0x00
#define RV_F7_SUB     0x20  /* SUB / SRA */
#define RV_F7_MULDIV  0x01  /* M-extension (MUL, DIV, etc.) */

/* funct3 for M-extension */
#define RV_F3_MUL     0x0
#define RV_F3_DIV     0x4

/* =========================================================================
 *  High-level emit functions
 * ========================================================================= */

/* --- ADD rd, rs1, rs2 -------------------------------------------------- */
static void emit_rv_add(CodeBuffer *buf, uint8_t rd, uint8_t rs1, uint8_t rs2)
{
    emit_rv32(buf, rv_r_type(RV_F7_NORMAL, rs2, rs1, RV_F3_ADD, rd, RV_OP_OP));
}

/* --- SUB rd, rs1, rs2 -------------------------------------------------- */
static void emit_rv_sub(CodeBuffer *buf, uint8_t rd, uint8_t rs1, uint8_t rs2)
{
    emit_rv32(buf, rv_r_type(RV_F7_SUB, rs2, rs1, RV_F3_ADD, rd, RV_OP_OP));
}

/* --- AND rd, rs1, rs2 -------------------------------------------------- */
static void emit_rv_and(CodeBuffer *buf, uint8_t rd, uint8_t rs1, uint8_t rs2)
{
    emit_rv32(buf, rv_r_type(RV_F7_NORMAL, rs2, rs1, RV_F3_AND, rd, RV_OP_OP));
}

/* --- OR rd, rs1, rs2 --------------------------------------------------- */
static void emit_rv_or(CodeBuffer *buf, uint8_t rd, uint8_t rs1, uint8_t rs2)
{
    emit_rv32(buf, rv_r_type(RV_F7_NORMAL, rs2, rs1, RV_F3_OR, rd, RV_OP_OP));
}

/* --- XOR rd, rs1, rs2 -------------------------------------------------- */
static void emit_rv_xor(CodeBuffer *buf, uint8_t rd, uint8_t rs1, uint8_t rs2)
{
    emit_rv32(buf, rv_r_type(RV_F7_NORMAL, rs2, rs1, RV_F3_XOR, rd, RV_OP_OP));
}

/* --- SLL rd, rs1, rs2  (shift left logical, register) ------------------ */
static void emit_rv_sll(CodeBuffer *buf, uint8_t rd, uint8_t rs1, uint8_t rs2)
{
    emit_rv32(buf, rv_r_type(RV_F7_NORMAL, rs2, rs1, RV_F3_SLL, rd, RV_OP_OP));
}

/* --- SRL rd, rs1, rs2  (shift right logical, register) ----------------- */
static void emit_rv_srl(CodeBuffer *buf, uint8_t rd, uint8_t rs1, uint8_t rs2)
{
    emit_rv32(buf, rv_r_type(RV_F7_NORMAL, rs2, rs1, RV_F3_SRL, rd, RV_OP_OP));
}

/* --- MUL rd, rs1, rs2  (M extension) ----------------------------------- */
static void emit_rv_mul(CodeBuffer *buf, uint8_t rd, uint8_t rs1, uint8_t rs2)
{
    emit_rv32(buf, rv_r_type(RV_F7_MULDIV, rs2, rs1, RV_F3_MUL, rd, RV_OP_OP));
}

/* --- DIV rd, rs1, rs2  (M extension) ----------------------------------- */
static void emit_rv_div(CodeBuffer *buf, uint8_t rd, uint8_t rs1, uint8_t rs2)
{
    emit_rv32(buf, rv_r_type(RV_F7_MULDIV, rs2, rs1, RV_F3_DIV, rd, RV_OP_OP));
}

/* --- ADDI rd, rs1, imm12 ----------------------------------------------- */
static void emit_rv_addi(CodeBuffer *buf, uint8_t rd, uint8_t rs1,
                          int32_t imm12)
{
    emit_rv32(buf, rv_i_type(imm12, rs1, RV_F3_ADD, rd, RV_OP_OP_IMM));
}

/* --- XORI rd, rs1, imm12 ----------------------------------------------- */
static void emit_rv_xori(CodeBuffer *buf, uint8_t rd, uint8_t rs1,
                          int32_t imm12)
{
    emit_rv32(buf, rv_i_type(imm12, rs1, RV_F3_XOR, rd, RV_OP_OP_IMM));
}

/* --- ANDI rd, rs1, imm12 ----------------------------------------------- */
static void emit_rv_andi(CodeBuffer *buf, uint8_t rd, uint8_t rs1,
                          int32_t imm12)
{
    emit_rv32(buf, rv_i_type(imm12, rs1, RV_F3_AND, rd, RV_OP_OP_IMM));
}

/* --- ORI rd, rs1, imm12 ------------------------------------------------ */
static void emit_rv_ori(CodeBuffer *buf, uint8_t rd, uint8_t rs1,
                         int32_t imm12)
{
    emit_rv32(buf, rv_i_type(imm12, rs1, RV_F3_OR, rd, RV_OP_OP_IMM));
}

/* --- SLLI rd, rs1, shamt  (shift left logical immediate) --------------- */
static void emit_rv_slli(CodeBuffer *buf, uint8_t rd, uint8_t rs1,
                          uint8_t shamt)
{
    /* I-type: imm[11:0] = 0000000|shamt[5:0] for RV64 */
    int32_t imm = (int32_t)(shamt & 0x3F);
    emit_rv32(buf, rv_i_type(imm, rs1, RV_F3_SLL, rd, RV_OP_OP_IMM));
}

/* --- SRLI rd, rs1, shamt  (shift right logical immediate) -------------- */
static void emit_rv_srli(CodeBuffer *buf, uint8_t rd, uint8_t rs1,
                          uint8_t shamt)
{
    int32_t imm = (int32_t)(shamt & 0x3F);
    emit_rv32(buf, rv_i_type(imm, rs1, RV_F3_SRL, rd, RV_OP_OP_IMM));
}

/* --- LUI rd, imm20  (load upper immediate) ----------------------------- */
static void emit_rv_lui(CodeBuffer *buf, uint8_t rd, int32_t imm20)
{
    emit_rv32(buf, rv_u_type(imm20, rd, RV_OP_LUI));
}

/* --- LD rd, offset(rs1)  (64-bit load) --------------------------------- */
static void emit_rv_ld(CodeBuffer *buf, uint8_t rd, uint8_t rs1,
                        int32_t offset)
{
    emit_rv32(buf, rv_i_type(offset, rs1, RV_F3_LD, rd, RV_OP_LOAD));
}

/* --- SD rs2, offset(rs1)  (64-bit store) ------------------------------- */
static void emit_rv_sd(CodeBuffer *buf, uint8_t rs2, uint8_t rs1,
                        int32_t offset)
{
    emit_rv32(buf, rv_s_type(offset, rs2, rs1, RV_F3_SD, RV_OP_STORE));
}

/* --- JAL rd, offset  (jump and link) ----------------------------------- */
static void emit_rv_jal(CodeBuffer *buf, uint8_t rd, int32_t offset)
{
    emit_rv32(buf, rv_j_type(offset, rd, RV_OP_JAL));
}

/* --- JALR rd, rs1, offset  (jump and link register) -------------------- */
static void emit_rv_jalr(CodeBuffer *buf, uint8_t rd, uint8_t rs1,
                          int32_t offset)
{
    emit_rv32(buf, rv_i_type(offset, rs1, 0x0, rd, RV_OP_JALR));
}

/* --- BEQ rs1, rs2, offset  (branch if equal) --------------------------- */
static void emit_rv_beq(CodeBuffer *buf, uint8_t rs1, uint8_t rs2,
                         int32_t offset)
{
    emit_rv32(buf, rv_b_type(offset, rs2, rs1, RV_F3_BEQ, RV_OP_BRANCH));
}

/* --- BNE rs1, rs2, offset  (branch if not equal) ----------------------- */
static void emit_rv_bne(CodeBuffer *buf, uint8_t rs1, uint8_t rs2,
                         int32_t offset)
{
    emit_rv32(buf, rv_b_type(offset, rs2, rs1, RV_F3_BNE, RV_OP_BRANCH));
}

/* --- ECALL  (environment call / software interrupt) -------------------- */
static void emit_rv_ecall(CodeBuffer *buf)
{
    emit_rv32(buf, rv_i_type(0, 0, 0, 0, RV_OP_SYSTEM));
}

/* --- NOP  (ADDI x0, x0, 0) --------------------------------------------- */
static void emit_rv_nop(CodeBuffer *buf)
{
    emit_rv_addi(buf, RV_REG_ZERO, RV_REG_ZERO, 0);
}

/* --- Load a full 32-bit / 64-bit immediate into rd --------------------- */
/*     Uses LUI + ADDI for values that don't fit in 12-bit immediate.     */
static void emit_rv_load_imm(CodeBuffer *buf, uint8_t rd, int32_t imm)
{
    /* If the value fits in signed 12-bit immediate [-2048, 2047] */
    if (imm >= -2048 && imm <= 2047) {
        emit_rv_addi(buf, rd, RV_REG_ZERO, imm);
    } else {
        /* LUI loads bits [31:12].  If bit 11 of the immediate is set,
         * ADDI will sign-extend and subtract 0x1000, so we compensate. */
        int32_t upper = imm & (int32_t)0xFFFFF000;
        int32_t lower = imm & 0xFFF;
        if (lower & 0x800) {
            upper += 0x1000;  /* compensate for sign extension of ADDI */
        }
        emit_rv_lui(buf, rd, upper);
        if (lower != 0) {
            /* Sign-extend the 12-bit value */
            int32_t sext_lower = (lower & 0x800) ? (lower | (int32_t)0xFFFFF000)
                                                  : lower;
            emit_rv_addi(buf, rd, rd, sext_lower);
        }
    }
}

/* Always emit LUI + ADDI (8 bytes) — used for variable addresses
 * where a fixed instruction size is needed for pass-1 sizing. */
static void emit_rv_load_imm_full(CodeBuffer *buf, uint8_t rd, int32_t imm)
{
    int32_t upper = imm & (int32_t)0xFFFFF000;
    int32_t lower = imm & 0xFFF;
    if (lower & 0x800) {
        upper += 0x1000;
    }
    emit_rv_lui(buf, rd, upper);
    int32_t sext_lower = (lower & 0x800) ? (lower | (int32_t)0xFFFFF000) : lower;
    emit_rv_addi(buf, rd, rd, sext_lower);
}

/* --- Branch / jump placeholder (4 bytes, will be patched) -------------- */
static void emit_rv_placeholder(CodeBuffer *buf)
{
    emit_byte(buf, 0x00);
    emit_byte(buf, 0x00);
    emit_byte(buf, 0x00);
    emit_byte(buf, 0x00);
}

static void patch_rv_word(CodeBuffer *buf, int offset, uint32_t word)
{
    buf->bytes[offset    ] = (uint8_t)( word        & 0xFF);
    buf->bytes[offset + 1] = (uint8_t)((word >>  8) & 0xFF);
    buf->bytes[offset + 2] = (uint8_t)((word >> 16) & 0xFF);
    buf->bytes[offset + 3] = (uint8_t)((word >> 24) & 0xFF);
}

/* =========================================================================
 *  Symbol table for labels
 * ========================================================================= */
#define RV_MAX_SYMBOLS   256
#define RV_MAX_FIXUPS    256

typedef struct {
    char name[UA_MAX_LABEL_LEN];
    int  address;
} RVSymbol;

/* Fixup types */
#define RV_FIXUP_JAL     0   /* J-type (JAL) */
#define RV_FIXUP_BRANCH  1   /* B-type (BEQ, BNE) */

typedef struct {
    char  label[UA_MAX_LABEL_LEN];
    int   patch_offset;     /* offset into CodeBuffer where instr lives  */
    int   instr_addr;       /* byte address of the instruction           */
    int   line;
    int   fixup_type;       /* RV_FIXUP_JAL or RV_FIXUP_BRANCH          */
    uint8_t rd;             /* rd field for JAL (x0 or x1)              */
    uint8_t funct3;         /* funct3 for branch type (BEQ/BNE)         */
} RVFixup;

typedef struct {
    RVSymbol symbols[RV_MAX_SYMBOLS];
    int      sym_count;
    RVFixup  fixups[RV_MAX_FIXUPS];
    int      fix_count;
} RVSymTab;

static void rv_symtab_init(RVSymTab *st)
{
    st->sym_count = 0;
    st->fix_count = 0;
}

static void rv_symtab_add(RVSymTab *st, const char *name, int address)
{
    if (st->sym_count >= RV_MAX_SYMBOLS) {
        fprintf(stderr, "RISC-V: symbol table overflow\n");
        exit(1);
    }
    strncpy(st->symbols[st->sym_count].name, name, UA_MAX_LABEL_LEN - 1);
    st->symbols[st->sym_count].name[UA_MAX_LABEL_LEN - 1] = '\0';
    st->symbols[st->sym_count].address = address;
    st->sym_count++;
}

static int rv_symtab_lookup(const RVSymTab *st, const char *name)
{
    for (int i = 0; i < st->sym_count; i++) {
        if (strcmp(st->symbols[i].name, name) == 0)
            return st->symbols[i].address;
    }
    return -1;
}

static void rv_add_fixup(RVSymTab *st, const char *label,
                          int patch_offset, int instr_addr, int line,
                          int fixup_type, uint8_t rd, uint8_t funct3)
{
    if (st->fix_count >= RV_MAX_FIXUPS) {
        fprintf(stderr, "RISC-V: fixup table overflow\n");
        exit(1);
    }
    RVFixup *f = &st->fixups[st->fix_count++];
    strncpy(f->label, label, UA_MAX_LABEL_LEN - 1);
    f->label[UA_MAX_LABEL_LEN - 1] = '\0';
    f->patch_offset = patch_offset;
    f->instr_addr   = instr_addr;
    f->line         = line;
    f->fixup_type   = fixup_type;
    f->rd           = rd;
    f->funct3       = funct3;
}

/* =========================================================================
 *  instruction_size_rv()  —  compute byte size of each instruction
 * ========================================================================= */
static int instruction_size_rv(const Instruction *inst)
{
    if (inst->is_label) return 0;

    switch (inst->opcode) {
        case OP_LDI: {
            int32_t imm = (int32_t)inst->operands[1].data.imm;
            if (imm >= -2048 && imm <= 2047) return 4;  /* ADDI only */
            return 8;  /* LUI + ADDI */
        }
        case OP_MOV:    return 4;   /* ADDI rd, rs, 0 */
        case OP_LOAD:   return 4;   /* LD */
        case OP_STORE:  return 4;   /* SD */
        case OP_ADD:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 4 : 12;
            /* reg: ADD 4, imm: load_imm(4-8) + ADD(4) = max 12 */
        case OP_SUB:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 4 : 12;
        case OP_AND:
            if (inst->operands[1].type == OPERAND_REGISTER) return 4;
            else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                if (imm >= -2048 && imm <= 2047) return 4;  /* ANDI */
                return 12;  /* load_imm + AND */
            }
        case OP_OR:
            if (inst->operands[1].type == OPERAND_REGISTER) return 4;
            else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                if (imm >= -2048 && imm <= 2047) return 4;  /* ORI */
                return 12;  /* load_imm + OR */
            }
        case OP_XOR:
            if (inst->operands[1].type == OPERAND_REGISTER) return 4;
            else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                if (imm >= -2048 && imm <= 2047) return 4;  /* XORI */
                return 12;  /* load_imm + XOR */
            }
        case OP_NOT:    return 4;   /* XORI rd, rd, -1 */
        case OP_INC:    return 4;   /* ADDI rd, rd, 1 */
        case OP_DEC:    return 4;   /* ADDI rd, rd, -1 */
        case OP_MUL:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 4 : 12;
        case OP_DIV:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 4 : 12;
        case OP_SHL:
            return 4;   /* SLLI or SLL, both 4 bytes */
        case OP_SHR:
            return 4;   /* SRLI or SRL, both 4 bytes */
        case OP_CMP:
            if (inst->operands[1].type == OPERAND_REGISTER) return 4;
            return 12;  /* load_imm + SUB */
        case OP_JMP:    return 4;   /* JAL x0, offset */
        case OP_JZ:     return 4;   /* BEQ t0, x0, offset */
        case OP_JNZ:    return 4;   /* BNE t0, x0, offset */
        case OP_CALL:   return 4;   /* JAL ra, offset */
        case OP_RET:    return 4;   /* JALR x0, ra, 0 */
        case OP_PUSH:   return 8;   /* ADDI sp, sp, -8 + SD */
        case OP_POP:    return 8;   /* LD + ADDI sp, sp, 8 */
        case OP_NOP:    return 4;
        case OP_HLT:    return 4;   /* JALR x0, ra, 0 (= RET) */
        case OP_INT:    return 4;   /* ECALL */

        /* ---- Variable pseudo-instructions ----------------------------- */
        case OP_VAR:    return 0;   /* declaration only */
        case OP_SET:
            /* SET name, Rs  -> LUI+ADDI t0,addr(8) + SD Rs,0(t0)(4) = 12 */
            if (inst->operands[1].type == OPERAND_REGISTER) return 12;
            /* SET name, imm -> LUI+ADDI t1,imm(8) + LUI+ADDI t0,addr(8)
             *                + SD t1,0(t0)(4) = 20 */
            return 20;
        case OP_GET:
            /* GET Rd, name  -> LUI+ADDI t0,addr(8) + LD Rd,0(t0)(4) = 12 */
            return 12;

        /* ---- New Phase-8 instructions --------------------------------- */
        case OP_LDS:    return 8;   /* LUI+ADDI Rd, addr (load string ptr)  */
        case OP_LOADB:  return 4;   /* LBU Rd, 0(Rs)  */
        case OP_STOREB: return 4;   /* SB  Rs, 0(Rd)  */
        case OP_SYS:    return 4;   /* ECALL           */
        default:        return 0;
    }
}

/* =========================================================================
 *  Variable table for RISC-V
 * ========================================================================= */
#define RV_MAX_VARS   256
#define RV_VAR_SIZE   8      /* bytes per variable (doubleword, 64-bit) */

typedef struct {
    char    name[UA_MAX_LABEL_LEN];
    int64_t init_value;
    int     has_init;
} RVVarEntry;

typedef struct {
    RVVarEntry vars[RV_MAX_VARS];
    int        count;
} RVVarTable;

static void rv_vartab_init(RVVarTable *vt) { vt->count = 0; }

/* =========================================================================
 *  String table for RISC-V  —  collects LDS string literals
 * ========================================================================= */
#define RV_MAX_STRINGS 256

typedef struct {
    const char *text;
    int         offset;
    int         length;
} RVStringEntry;

typedef struct {
    RVStringEntry strings[RV_MAX_STRINGS];
    int           count;
    int           total_size;
} RVStringTable;

static void rv_strtab_init(RVStringTable *st) {
    st->count = 0;
    st->total_size = 0;
}

static int rv_strtab_add(RVStringTable *st, const char *text) {
    for (int i = 0; i < st->count; i++)
        if (strcmp(st->strings[i].text, text) == 0) return i;
    if (st->count >= RV_MAX_STRINGS) {
        fprintf(stderr, "RISC-V: string table overflow (max %d)\n",
                RV_MAX_STRINGS);
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

static int rv_vartab_add(RVVarTable *vt, const char *name,
                          int64_t init_value, int has_init)
{
    for (int i = 0; i < vt->count; i++) {
        if (strcmp(vt->vars[i].name, name) == 0) {
            fprintf(stderr, "RISC-V: duplicate variable '%s'\n", name);
            return -1;
        }
    }
    if (vt->count >= RV_MAX_VARS) {
        fprintf(stderr, "RISC-V: variable table overflow (max %d)\n",
                RV_MAX_VARS);
        return -1;
    }
    RVVarEntry *v = &vt->vars[vt->count++];
    strncpy(v->name, name, UA_MAX_LABEL_LEN - 1);
    v->name[UA_MAX_LABEL_LEN - 1] = '\0';
    v->init_value = init_value;
    v->has_init   = has_init;
    return 0;
}

/* =========================================================================
 *  generate_risc_v()  —  main entry point  (two-pass)
 * ========================================================================= */
CodeBuffer* generate_risc_v(const Instruction *ir, int ir_count)
{
    fprintf(stderr, "[RISC-V] Generating code for %d IR instructions ...\n",
            ir_count);

    /* --- Pass 1: collect label addresses + variable declarations ------- */
    RVSymTab symtab;
    rv_symtab_init(&symtab);

    RVVarTable vartab;
    rv_vartab_init(&vartab);

    RVStringTable strtab;
    rv_strtab_init(&strtab);

    int pc = 0;
    for (int i = 0; i < ir_count; i++) {
        const Instruction *inst = &ir[i];
        if (inst->is_label) {
            rv_symtab_add(&symtab, inst->label_name, pc);
        } else if (inst->opcode == OP_VAR) {
            const char *vname = inst->operands[0].data.label;
            int64_t init_val  = 0;
            int     has_init  = 0;
            if (inst->operand_count >= 2 &&
                inst->operands[1].type == OPERAND_IMMEDIATE) {
                init_val = inst->operands[1].data.imm;
                has_init = 1;
            }
            rv_vartab_add(&vartab, vname, init_val, has_init);
        } else {
            if (inst->opcode == OP_LDS)
                rv_strtab_add(&strtab, inst->operands[1].data.string);
            pc += instruction_size_rv(inst);
        }
    }

    /* Register variable symbols: each at code_end + index * 8 */
    int var_base = pc;
    for (int v = 0; v < vartab.count; v++) {
        rv_symtab_add(&symtab, vartab.vars[v].name,
                      var_base + v * RV_VAR_SIZE);
    }
    int str_base = var_base + vartab.count * RV_VAR_SIZE;

    /* --- Pass 2: code emission ----------------------------------------- */
    CodeBuffer *code = create_code_buffer();
    if (!code) {
        fprintf(stderr, "UA RISC-V: out of memory\n");
        return NULL;
    }

    for (int i = 0; i < ir_count; i++) {
        const Instruction *inst = &ir[i];

        if (inst->is_label)
            continue;

        switch (inst->opcode) {

        /* ---- LDI Rd, #imm  ->  ADDI / LUI+ADDI ---------- 4-8 bytes -- */
        case OP_LDI: {
            int rd = inst->operands[0].data.reg;
            int32_t imm = (int32_t)inst->operands[1].data.imm;
            rv_validate_register(inst, rd);
            uint8_t enc = RV_REG_ENC[rd];
            fprintf(stderr, "  LDI R%d -> load %s, %d\n",
                    rd, RV_REG_NAME[rd], imm);
            emit_rv_load_imm(code, enc, imm);
            break;
        }

        /* ---- MOV Rd, Rs  ->  ADDI Rd, Rs, 0 --------------- 4 bytes -- */
        case OP_MOV: {
            int rd = inst->operands[0].data.reg;
            int rs = inst->operands[1].data.reg;
            rv_validate_register(inst, rd);
            rv_validate_register(inst, rs);
            fprintf(stderr, "  MOV R%d, R%d -> ADDI %s, %s, 0\n",
                    rd, rs, RV_REG_NAME[rd], RV_REG_NAME[rs]);
            emit_rv_addi(code, RV_REG_ENC[rd], RV_REG_ENC[rs], 0);
            break;
        }

        /* ---- LOAD Rd, Rs  ->  LD Rd, 0(Rs) --------------- 4 bytes --- */
        case OP_LOAD: {
            int rd = inst->operands[0].data.reg;
            int rs = inst->operands[1].data.reg;
            rv_validate_register(inst, rd);
            rv_validate_register(inst, rs);
            fprintf(stderr, "  LOAD R%d, R%d -> LD %s, 0(%s)\n",
                    rd, rs, RV_REG_NAME[rd], RV_REG_NAME[rs]);
            emit_rv_ld(code, RV_REG_ENC[rd], RV_REG_ENC[rs], 0);
            break;
        }

        /* ---- STORE Rx, Ry  ->  SD Ry, 0(Rx) -------------- 4 bytes --- */
        case OP_STORE: {
            int rx = inst->operands[0].data.reg;
            int ry = inst->operands[1].data.reg;
            rv_validate_register(inst, rx);
            rv_validate_register(inst, ry);
            fprintf(stderr, "  STORE R%d, R%d -> SD %s, 0(%s)\n",
                    rx, ry, RV_REG_NAME[ry], RV_REG_NAME[rx]);
            emit_rv_sd(code, RV_REG_ENC[ry], RV_REG_ENC[rx], 0);
            break;
        }

        /* ---- ADD Rd, Rs/imm -------------------------------- 4/12 ----- */
        case OP_ADD: {
            int rd = inst->operands[0].data.reg;
            rv_validate_register(inst, rd);
            uint8_t enc_d = RV_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                rv_validate_register(inst, rs);
                fprintf(stderr, "  ADD R%d, R%d -> ADD %s, %s, %s\n",
                        rd, rs, RV_REG_NAME[rd], RV_REG_NAME[rd],
                        RV_REG_NAME[rs]);
                emit_rv_add(code, enc_d, enc_d, RV_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                fprintf(stderr, "  ADD R%d, #%d\n", rd, imm);
                emit_rv_load_imm_full(code, RV_REG_T0, imm);
                emit_rv_add(code, enc_d, enc_d, RV_REG_T0);
            }
            break;
        }

        /* ---- SUB Rd, Rs/imm -------------------------------- 4/12 ----- */
        case OP_SUB: {
            int rd = inst->operands[0].data.reg;
            rv_validate_register(inst, rd);
            uint8_t enc_d = RV_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                rv_validate_register(inst, rs);
                fprintf(stderr, "  SUB R%d, R%d -> SUB %s, %s, %s\n",
                        rd, rs, RV_REG_NAME[rd], RV_REG_NAME[rd],
                        RV_REG_NAME[rs]);
                emit_rv_sub(code, enc_d, enc_d, RV_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                fprintf(stderr, "  SUB R%d, #%d\n", rd, imm);
                emit_rv_load_imm_full(code, RV_REG_T0, imm);
                emit_rv_sub(code, enc_d, enc_d, RV_REG_T0);
            }
            break;
        }

        /* ---- AND Rd, Rs/imm -------------------------------- 4/4-12 --- */
        case OP_AND: {
            int rd = inst->operands[0].data.reg;
            rv_validate_register(inst, rd);
            uint8_t enc_d = RV_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                rv_validate_register(inst, rs);
                fprintf(stderr, "  AND R%d, R%d -> AND %s, %s, %s\n",
                        rd, rs, RV_REG_NAME[rd], RV_REG_NAME[rd],
                        RV_REG_NAME[rs]);
                emit_rv_and(code, enc_d, enc_d, RV_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                if (imm >= -2048 && imm <= 2047) {
                    fprintf(stderr, "  AND R%d, #%d -> ANDI\n", rd, imm);
                    emit_rv_andi(code, enc_d, enc_d, imm);
                } else {
                    fprintf(stderr, "  AND R%d, #%d -> load t0; AND\n", rd, imm);
                    emit_rv_load_imm_full(code, RV_REG_T0, imm);
                    emit_rv_and(code, enc_d, enc_d, RV_REG_T0);
                }
            }
            break;
        }

        /* ---- OR Rd, Rs/imm --------------------------------- 4/4-12 --- */
        case OP_OR: {
            int rd = inst->operands[0].data.reg;
            rv_validate_register(inst, rd);
            uint8_t enc_d = RV_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                rv_validate_register(inst, rs);
                fprintf(stderr, "  OR  R%d, R%d -> OR %s, %s, %s\n",
                        rd, rs, RV_REG_NAME[rd], RV_REG_NAME[rd],
                        RV_REG_NAME[rs]);
                emit_rv_or(code, enc_d, enc_d, RV_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                if (imm >= -2048 && imm <= 2047) {
                    fprintf(stderr, "  OR  R%d, #%d -> ORI\n", rd, imm);
                    emit_rv_ori(code, enc_d, enc_d, imm);
                } else {
                    fprintf(stderr, "  OR  R%d, #%d -> load t0; OR\n", rd, imm);
                    emit_rv_load_imm_full(code, RV_REG_T0, imm);
                    emit_rv_or(code, enc_d, enc_d, RV_REG_T0);
                }
            }
            break;
        }

        /* ---- XOR Rd, Rs/imm -------------------------------- 4/4-12 --- */
        case OP_XOR: {
            int rd = inst->operands[0].data.reg;
            rv_validate_register(inst, rd);
            uint8_t enc_d = RV_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                rv_validate_register(inst, rs);
                fprintf(stderr, "  XOR R%d, R%d -> XOR %s, %s, %s\n",
                        rd, rs, RV_REG_NAME[rd], RV_REG_NAME[rd],
                        RV_REG_NAME[rs]);
                emit_rv_xor(code, enc_d, enc_d, RV_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                if (imm >= -2048 && imm <= 2047) {
                    fprintf(stderr, "  XOR R%d, #%d -> XORI\n", rd, imm);
                    emit_rv_xori(code, enc_d, enc_d, imm);
                } else {
                    fprintf(stderr, "  XOR R%d, #%d -> load t0; XOR\n", rd, imm);
                    emit_rv_load_imm_full(code, RV_REG_T0, imm);
                    emit_rv_xor(code, enc_d, enc_d, RV_REG_T0);
                }
            }
            break;
        }

        /* ---- NOT Rd  ->  XORI Rd, Rd, -1 ------------------- 4 bytes -- */
        case OP_NOT: {
            int rd = inst->operands[0].data.reg;
            rv_validate_register(inst, rd);
            fprintf(stderr, "  NOT R%d -> XORI %s, %s, -1\n",
                    rd, RV_REG_NAME[rd], RV_REG_NAME[rd]);
            emit_rv_xori(code, RV_REG_ENC[rd], RV_REG_ENC[rd], -1);
            break;
        }

        /* ---- INC Rd  ->  ADDI Rd, Rd, 1 -------------------- 4 bytes -- */
        case OP_INC: {
            int rd = inst->operands[0].data.reg;
            rv_validate_register(inst, rd);
            fprintf(stderr, "  INC R%d -> ADDI %s, %s, 1\n",
                    rd, RV_REG_NAME[rd], RV_REG_NAME[rd]);
            emit_rv_addi(code, RV_REG_ENC[rd], RV_REG_ENC[rd], 1);
            break;
        }

        /* ---- DEC Rd  ->  ADDI Rd, Rd, -1 ------------------- 4 bytes -- */
        case OP_DEC: {
            int rd = inst->operands[0].data.reg;
            rv_validate_register(inst, rd);
            fprintf(stderr, "  DEC R%d -> ADDI %s, %s, -1\n",
                    rd, RV_REG_NAME[rd], RV_REG_NAME[rd]);
            emit_rv_addi(code, RV_REG_ENC[rd], RV_REG_ENC[rd], -1);
            break;
        }

        /* ---- MUL Rd, Rs/imm  ->  MUL Rd, Rd, Rm ------ 4/12 bytes ---- */
        case OP_MUL: {
            int rd = inst->operands[0].data.reg;
            rv_validate_register(inst, rd);
            uint8_t enc_d = RV_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                rv_validate_register(inst, rs);
                fprintf(stderr, "  MUL R%d, R%d -> MUL %s, %s, %s\n",
                        rd, rs, RV_REG_NAME[rd], RV_REG_NAME[rd],
                        RV_REG_NAME[rs]);
                emit_rv_mul(code, enc_d, enc_d, RV_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                fprintf(stderr, "  MUL R%d, #%d -> load t0; MUL\n", rd, imm);
                emit_rv_load_imm_full(code, RV_REG_T0, imm);
                emit_rv_mul(code, enc_d, enc_d, RV_REG_T0);
            }
            break;
        }

        /* ---- DIV Rd, Rs/imm  ->  DIV Rd, Rd, Rm ------ 4/12 bytes ---- */
        case OP_DIV: {
            int rd = inst->operands[0].data.reg;
            rv_validate_register(inst, rd);
            uint8_t enc_d = RV_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                rv_validate_register(inst, rs);
                fprintf(stderr, "  DIV R%d, R%d -> DIV %s, %s, %s\n",
                        rd, rs, RV_REG_NAME[rd], RV_REG_NAME[rd],
                        RV_REG_NAME[rs]);
                emit_rv_div(code, enc_d, enc_d, RV_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                fprintf(stderr, "  DIV R%d, #%d -> load t0; DIV\n", rd, imm);
                emit_rv_load_imm_full(code, RV_REG_T0, imm);
                emit_rv_div(code, enc_d, enc_d, RV_REG_T0);
            }
            break;
        }

        /* ---- SHL Rd, Rs/imm -------------------------------- 4 bytes -- */
        case OP_SHL: {
            int rd = inst->operands[0].data.reg;
            rv_validate_register(inst, rd);
            uint8_t enc_d = RV_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_IMMEDIATE) {
                uint8_t shamt = (uint8_t)(inst->operands[1].data.imm & 0x3F);
                fprintf(stderr, "  SHL R%d, #%d -> SLLI %s, %s, %d\n",
                        rd, shamt, RV_REG_NAME[rd], RV_REG_NAME[rd], shamt);
                emit_rv_slli(code, enc_d, enc_d, shamt);
            } else {
                int rs = inst->operands[1].data.reg;
                rv_validate_register(inst, rs);
                fprintf(stderr, "  SHL R%d, R%d -> SLL %s, %s, %s\n",
                        rd, rs, RV_REG_NAME[rd], RV_REG_NAME[rd],
                        RV_REG_NAME[rs]);
                emit_rv_sll(code, enc_d, enc_d, RV_REG_ENC[rs]);
            }
            break;
        }

        /* ---- SHR Rd, Rs/imm -------------------------------- 4 bytes -- */
        case OP_SHR: {
            int rd = inst->operands[0].data.reg;
            rv_validate_register(inst, rd);
            uint8_t enc_d = RV_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_IMMEDIATE) {
                uint8_t shamt = (uint8_t)(inst->operands[1].data.imm & 0x3F);
                fprintf(stderr, "  SHR R%d, #%d -> SRLI %s, %s, %d\n",
                        rd, shamt, RV_REG_NAME[rd], RV_REG_NAME[rd], shamt);
                emit_rv_srli(code, enc_d, enc_d, shamt);
            } else {
                int rs = inst->operands[1].data.reg;
                rv_validate_register(inst, rs);
                fprintf(stderr, "  SHR R%d, R%d -> SRL %s, %s, %s\n",
                        rd, rs, RV_REG_NAME[rd], RV_REG_NAME[rd],
                        RV_REG_NAME[rs]);
                emit_rv_srl(code, enc_d, enc_d, RV_REG_ENC[rs]);
            }
            break;
        }

        /* ---- CMP Ra, Rb/imm  ->  SUB t0, Ra, Rb ---- 4/12 bytes ------ */
        /*  RISC-V has no flags register.  We simulate comparison by       */
        /*  storing (Ra - Rb) in t0.  Subsequent BEQ/BNE test t0 vs x0.   */
        case OP_CMP: {
            int ra = inst->operands[0].data.reg;
            rv_validate_register(inst, ra);
            uint8_t enc_a = RV_REG_ENC[ra];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rb = inst->operands[1].data.reg;
                rv_validate_register(inst, rb);
                fprintf(stderr, "  CMP R%d, R%d -> SUB t0, %s, %s\n",
                        ra, rb, RV_REG_NAME[ra], RV_REG_NAME[rb]);
                emit_rv_sub(code, RV_REG_T0, enc_a, RV_REG_ENC[rb]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                fprintf(stderr, "  CMP R%d, #%d -> load t1; SUB t0\n", ra, imm);
                emit_rv_load_imm_full(code, RV_REG_T1, imm);
                emit_rv_sub(code, RV_REG_T0, enc_a, RV_REG_T1);
            }
            break;
        }

        /* ---- JMP label  ->  JAL x0, offset --------------- 4 bytes ---- */
        case OP_JMP: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  JMP %s -> JAL x0\n", label);
            int patch_off = code->size;
            emit_rv_placeholder(code);
            rv_add_fixup(&symtab, label, patch_off, patch_off, inst->line,
                         RV_FIXUP_JAL, RV_REG_ZERO, 0);
            break;
        }

        /* ---- JZ label  ->  BEQ t0, x0, offset ----------- 4 bytes ---- */
        case OP_JZ: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  JZ  %s -> BEQ t0, x0\n", label);
            int patch_off = code->size;
            emit_rv_placeholder(code);
            rv_add_fixup(&symtab, label, patch_off, patch_off, inst->line,
                         RV_FIXUP_BRANCH, 0, RV_F3_BEQ);
            break;
        }

        /* ---- JNZ label  ->  BNE t0, x0, offset ---------- 4 bytes ---- */
        case OP_JNZ: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  JNZ %s -> BNE t0, x0\n", label);
            int patch_off = code->size;
            emit_rv_placeholder(code);
            rv_add_fixup(&symtab, label, patch_off, patch_off, inst->line,
                         RV_FIXUP_BRANCH, 0, RV_F3_BNE);
            break;
        }

        /* ---- CALL label  ->  JAL ra, offset -------------- 4 bytes ---- */
        case OP_CALL: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  CALL %s -> JAL ra\n", label);
            int patch_off = code->size;
            emit_rv_placeholder(code);
            rv_add_fixup(&symtab, label, patch_off, patch_off, inst->line,
                         RV_FIXUP_JAL, RV_REG_RA, 0);
            break;
        }

        /* ---- RET  ->  JALR x0, ra, 0 -------------------- 4 bytes ---- */
        case OP_RET:
            fprintf(stderr, "  RET -> JALR x0, ra, 0\n");
            emit_rv_jalr(code, RV_REG_ZERO, RV_REG_RA, 0);
            break;

        /* ---- PUSH Rs  ->  ADDI sp, sp, -8; SD Rs, 0(sp) -- 8 bytes --- */
        case OP_PUSH: {
            int rs = inst->operands[0].data.reg;
            rv_validate_register(inst, rs);
            fprintf(stderr, "  PUSH R%d -> ADDI sp, sp, -8; SD %s, 0(sp)\n",
                    rs, RV_REG_NAME[rs]);
            emit_rv_addi(code, RV_REG_SP, RV_REG_SP, -8);
            emit_rv_sd(code, RV_REG_ENC[rs], RV_REG_SP, 0);
            break;
        }

        /* ---- POP Rd  ->  LD Rd, 0(sp); ADDI sp, sp, 8 ---- 8 bytes --- */
        case OP_POP: {
            int rd = inst->operands[0].data.reg;
            rv_validate_register(inst, rd);
            fprintf(stderr, "  POP  R%d -> LD %s, 0(sp); ADDI sp, sp, 8\n",
                    rd, RV_REG_NAME[rd]);
            emit_rv_ld(code, RV_REG_ENC[rd], RV_REG_SP, 0);
            emit_rv_addi(code, RV_REG_SP, RV_REG_SP, 8);
            break;
        }

        /* ---- NOP -------------------------------------------- 4 bytes -- */
        case OP_NOP:
            fprintf(stderr, "  NOP\n");
            emit_rv_nop(code);
            break;

        /* ---- HLT  ->  JALR x0, ra, 0 (RET) --------------- 4 bytes --- */
        case OP_HLT:
            fprintf(stderr, "  HLT -> JALR x0, ra, 0\n");
            emit_rv_jalr(code, RV_REG_ZERO, RV_REG_RA, 0);
            break;

        /* ---- INT #imm  ->  ECALL -------------------------- 4 bytes --- */
        /*  Note: RISC-V ECALL doesn't take an immediate operand.         */
        /*  The syscall number should be loaded into a7 (R7) beforehand.  */
        case OP_INT: {
            uint32_t imm = (uint32_t)(inst->operands[0].data.imm & 0xFF);
            fprintf(stderr, "  INT #%d -> ECALL (a7 should hold syscall #)\n",
                    (int)imm);
            (void)imm;  /* ECALL uses a7 for syscall number */
            emit_rv_ecall(code);
            break;
        }

        /* ---- VAR — declaration only, no code emitted ------------------ */
        case OP_VAR:
            break;

        /* ---- SET name, Rs/imm — store to variable --------------------- */
        case OP_SET: {
            const char *vname = inst->operands[0].data.label;
            int var_addr = rv_symtab_lookup(&symtab, vname);
            if (var_addr < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "undefined variable '%s'", vname);
                rv_error(inst, msg);
            }
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                rv_validate_register(inst, rs);
                fprintf(stderr, "  SET %s, R%d -> SD %s, [t0]\n",
                        vname, rs, RV_REG_NAME[rs]);
                /* Load address into t0 */
                emit_rv_load_imm_full(code, RV_REG_T0, (int32_t)var_addr);
                /* SD Rs, 0(t0) */
                emit_rv_sd(code, RV_REG_ENC[rs], RV_REG_T0, 0);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                fprintf(stderr, "  SET %s, #%d -> SD t1, [t0]\n",
                        vname, imm);
                /* Load value into t1 */
                emit_rv_load_imm_full(code, RV_REG_T1, imm);
                /* Load address into t0 */
                emit_rv_load_imm_full(code, RV_REG_T0, (int32_t)var_addr);
                /* SD t1, 0(t0) */
                emit_rv_sd(code, RV_REG_T1, RV_REG_T0, 0);
            }
            break;
        }

        /* ---- GET Rd, name — load from variable ------------------------ */
        case OP_GET: {
            int rd = inst->operands[0].data.reg;
            const char *vname = inst->operands[1].data.label;
            rv_validate_register(inst, rd);
            int var_addr = rv_symtab_lookup(&symtab, vname);
            if (var_addr < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "undefined variable '%s'", vname);
                rv_error(inst, msg);
            }
            fprintf(stderr, "  GET R%d, %s -> LD %s, [t0]\n",
                    rd, vname, RV_REG_NAME[rd]);
            /* Load address into t0 */
            emit_rv_load_imm_full(code, RV_REG_T0, (int32_t)var_addr);
            /* LD Rd, 0(t0) */
            emit_rv_ld(code, RV_REG_ENC[rd], RV_REG_T0, 0);
            break;
        }

        /* ---- LDS Rd, "str"  ->  LUI+ADDI Rd, addr -------- 8 bytes --- */
        case OP_LDS: {
            int rd = inst->operands[0].data.reg;
            const char *str = inst->operands[1].data.string;
            rv_validate_register(inst, rd);
            int str_idx = rv_strtab_add(&strtab, str);
            int str_addr = str_base + strtab.strings[str_idx].offset;
            fprintf(stderr, "  LDS R%d, \"%s\" -> LUI+ADDI %s, #%d\n",
                    rd, str, RV_REG_NAME[rd], str_addr);
            emit_rv_load_imm_full(code, RV_REG_ENC[rd], (int32_t)str_addr);
            break;
        }

        /* ---- LOADB Rd, Rs  ->  LBU Rd, 0(Rs) ------------- 4 bytes --- */
        case OP_LOADB: {
            int rd = inst->operands[0].data.reg;
            int rs = inst->operands[1].data.reg;
            rv_validate_register(inst, rd);
            rv_validate_register(inst, rs);
            fprintf(stderr, "  LOADB R%d, R%d -> LBU %s, 0(%s)\n",
                    rd, rs, RV_REG_NAME[rd], RV_REG_NAME[rs]);
            /* LBU: I-type, funct3=0x4, opcode=0x03 */
            emit_rv32(code, rv_i_type(0, RV_REG_ENC[rs], 0x4,
                                      RV_REG_ENC[rd], RV_OP_LOAD));
            break;
        }

        /* ---- STOREB Rs, Rd  ->  SB Rs, 0(Rd) ------------- 4 bytes --- */
        case OP_STOREB: {
            int rx = inst->operands[0].data.reg;
            int ry = inst->operands[1].data.reg;
            rv_validate_register(inst, rx);
            rv_validate_register(inst, ry);
            fprintf(stderr, "  STOREB R%d, R%d -> SB %s, 0(%s)\n",
                    rx, ry, RV_REG_NAME[rx], RV_REG_NAME[ry]);
            /* SB: S-type, funct3=0x0, opcode=0x23 */
            emit_rv32(code, rv_s_type(0, RV_REG_ENC[rx], RV_REG_ENC[ry],
                                      0x0, RV_OP_STORE));
            break;
        }

        /* ---- SYS  ->  ECALL ----------------------------- 4 bytes --- */
        case OP_SYS:
            fprintf(stderr, "  SYS -> ECALL\n");
            emit_rv_ecall(code);
            break;

        default: {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "opcode '%s' is not supported by the RISC-V backend",
                     opcode_name(inst->opcode));
            rv_error(inst, msg);
            break;
        }
        }
    }

    /* --- Pass 3: patch branch / jump relocations ----------------------- */
    for (int f = 0; f < symtab.fix_count; f++) {
        RVFixup *fix = &symtab.fixups[f];
        int target = rv_symtab_lookup(&symtab, fix->label);
        if (target < 0) {
            fprintf(stderr,
                    "RISC-V: undefined label or variable '%s' (line %d)\n",
                    fix->label, fix->line);
            free_code_buffer(code);
            return NULL;
        }
        int32_t offset = (int32_t)(target - fix->instr_addr);

        if (fix->fixup_type == RV_FIXUP_JAL) {
            /* J-type offset: ±1 MiB.  Check range. */
            if (offset < -(1 << 20) || offset >= (1 << 20)) {
                fprintf(stderr,
                        "RISC-V: JAL target '%s' out of range (line %d)\n",
                        fix->label, fix->line);
                free_code_buffer(code);
                return NULL;
            }
            uint32_t word = rv_j_type(offset, fix->rd, RV_OP_JAL);
            patch_rv_word(code, fix->patch_offset, word);
        } else {
            /* B-type offset: ±4 KiB.  Check range. */
            if (offset < -(1 << 12) || offset >= (1 << 12)) {
                fprintf(stderr,
                        "RISC-V: branch target '%s' out of range (line %d)\n",
                        fix->label, fix->line);
                free_code_buffer(code);
                return NULL;
            }
            uint32_t word = rv_b_type(offset, RV_REG_ZERO, RV_REG_T0,
                                       fix->funct3, RV_OP_BRANCH);
            patch_rv_word(code, fix->patch_offset, word);
        }
    }

    /* --- Append variable data section --------------------------------- */
    int data_start = code->size;
    for (int v = 0; v < vartab.count; v++) {
        int64_t val = vartab.vars[v].has_init ? vartab.vars[v].init_value : 0;
        for (int b = 0; b < RV_VAR_SIZE; b++) {
            emit_byte(code, (uint8_t)((val >> (b * 8)) & 0xFF));
        }
    }

    /* --- Append string data section ----------------------------------- */
    for (int s = 0; s < strtab.count; s++) {
        const char *p = strtab.strings[s].text;
        int len = strtab.strings[s].length;
        for (int b = 0; b < len; b++)
            emit_byte(code, (uint8_t)p[b]);
        emit_byte(code, 0x00);
    }

    fprintf(stderr, "[RISC-V] Emitted %d bytes (%d code + %d var + %d str)\n",
            code->size, data_start,
            vartab.count * RV_VAR_SIZE, strtab.total_size);
    return code;
}
