/*
 * =============================================================================
 *  UA - Unified Assembler
 *  Phase 7: x86-64 Back-End (Full ISA)
 *
 *  File:    backend_x86_64.c
 *  Purpose: Translate UA IR into raw x86-64 machine code.
 *
 *  ┌──────────────────────────────────────────────────────────────────────┐
 *  │  x86-64 Instruction Encoding Reference (Phase 7 — full MVIS ISA)  │
 *  │                                                                    │
 *  │  Register encoding (low 3 bits, no REX.B needed for 0-7):         │
 *  │    RAX=0  RCX=1  RDX=2  RBX=3  RSP=4  RBP=5  RSI=6  RDI=7       │
 *  │                                                                    │
 *  │  MOV  r64, imm32   REX.W C7 /0 id           7 bytes               │
 *  │  MOV  r64, r64     REX.W 89 ModRM            3 bytes               │
 *  │  MOV  r64,[r64]    REX.W 8B ModRM            3 bytes (LOAD)        │
 *  │  MOV  [r64],r64    REX.W 89 ModRM            3 bytes (STORE)       │
 *  │  ADD  r64, r64     REX.W 01 ModRM            3 bytes               │
 *  │  SUB  r64, r64     REX.W 29 ModRM            3 bytes               │
 *  │  AND  r64, r64     REX.W 21 ModRM            3 bytes               │
 *  │  OR   r64, r64     REX.W 09 ModRM            3 bytes               │
 *  │  XOR  r64, r64     REX.W 31 ModRM            3 bytes               │
 *  │  NOT  r64          REX.W F7 /2               3 bytes               │
 *  │  INC  r64          REX.W FF /0               3 bytes               │
 *  │  DEC  r64          REX.W FF /1               3 bytes               │
 *  │  IMUL r64, r64     REX.W 0F AF ModRM         4 bytes               │
 *  │  CQO               REX.W 99                  2 bytes               │
 *  │  IDIV r64          REX.W F7 /7               3 bytes               │
 *  │  SHL  r64, CL      REX.W D3 /4               3 bytes               │
 *  │  SHR  r64, CL      REX.W D3 /5               3 bytes               │
 *  │  SHL  r64, imm8    REX.W C1 /4 ib            4 bytes               │
 *  │  SHR  r64, imm8    REX.W C1 /5 ib            4 bytes               │
 *  │  CMP  r64, r64     REX.W 39 ModRM            3 bytes               │
 *  │  JMP  rel32        E9 cd                      5 bytes               │
 *  │  JZ   rel32        0F 84 cd                   6 bytes               │
 *  │  JNZ  rel32        0F 85 cd                   6 bytes               │
 *  │  CALL rel32        E8 cd                      5 bytes               │
 *  │  RET               C3                         1 byte                │
 *  │  PUSH r64          50+rd                      1 byte (reg 0-7)      │
 *  │  POP  r64          58+rd                      1 byte (reg 0-7)      │
 *  │  NOP               90                         1 byte                │
 *  │  INT  imm8         CD ib                      2 bytes               │
 *  └──────────────────────────────────────────────────────────────────────┘
 *
 *  UA registers R0-R7 map to RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI.
 *  R8-R15 require REX.B and are rejected (Phase 7 limitation).
 *
 *  License: MIT
 * =============================================================================
 */

#include "backend_x86_64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 *  x86-64 register encoding table
 * =========================================================================
 *  Index = UA register number,  Value = x86-64 register encoding.
 *  R0-R7 are mapped; R8-R15 need REX.B and are not supported.
 * ========================================================================= */
#define X64_MAX_REG  8

static const uint8_t X64_REG_ENC[X64_MAX_REG] = {
    0,  /* R0 -> RAX */
    1,  /* R1 -> RCX */
    2,  /* R2 -> RDX */
    3,  /* R3 -> RBX */
    4,  /* R4 -> RSP */
    5,  /* R5 -> RBP */
    6,  /* R6 -> RSI */
    7   /* R7 -> RDI */
};

static const char* X64_REG_NAME[X64_MAX_REG] = {
    "RAX", "RCX", "RDX", "RBX", "RSP", "RBP", "RSI", "RDI"
};

/* =========================================================================
 *  Error helpers
 * ========================================================================= */
static void x64_error(const Instruction *inst, const char *msg)
{
    fprintf(stderr,
            "\n"
            "  UA x86-64 Backend Error\n"
            "  ------------------------\n"
            "  Line %d, Column %d: %s\n\n",
            inst->line, inst->column, msg);
    exit(1);
}

static void x64_validate_register(const Instruction *inst, int reg)
{
    if (reg < 0 || reg >= X64_MAX_REG) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "register R%d is not mapped in the x86-64 backend "
                 "(supports R0-R7: RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI)",
                 reg);
        x64_error(inst, msg);
    }
}

/* =========================================================================
 *  Emit helpers  —  build x86-64 instruction bytes
 * ========================================================================= */

/* --- MOV r64, imm32 (sign-extended to 64) : 7 bytes -------------------- */
static void emit_mov_r64_imm32(CodeBuffer *buf, uint8_t rd, int32_t imm)
{
    emit_byte(buf, 0x48);
    emit_byte(buf, 0xC7);
    emit_byte(buf, (uint8_t)(0xC0 | rd));
    emit_byte(buf, (uint8_t)( imm        & 0xFF));
    emit_byte(buf, (uint8_t)((imm >>  8) & 0xFF));
    emit_byte(buf, (uint8_t)((imm >> 16) & 0xFF));
    emit_byte(buf, (uint8_t)((imm >> 24) & 0xFF));
}

/* --- Generic REX.W + 1-byte-opcode + ModR/M(reg,rm) : 3 bytes --------- */
static void emit_alu_r64_r64(CodeBuffer *buf, uint8_t opcode,
                             uint8_t dst, uint8_t src)
{
    emit_byte(buf, 0x48);
    emit_byte(buf, opcode);
    emit_byte(buf, (uint8_t)(0xC0 | (src << 3) | dst));
}

/* --- MOV r64, r64  (89 variant: MOV r/m64, r64) : 3 bytes ------------- */
static void emit_mov_r64_r64(CodeBuffer *buf, uint8_t dst, uint8_t src)
{
    emit_alu_r64_r64(buf, 0x89, dst, src);
}

/* --- MOV r64, [r64] (8B variant: MOV r64, r/m64, mod=00) : 3 bytes ---- */
static void emit_load_r64_mem(CodeBuffer *buf, uint8_t dst, uint8_t base)
{
    emit_byte(buf, 0x48);
    emit_byte(buf, 0x8B);
    /* ModR/M: mod=00, reg=dst, r/m=base.  RBP(5) needs [RBP+0] encoding. */
    if (base == 5) {
        /* mod=01, reg=dst, rm=5 + disp8=0 */
        emit_byte(buf, (uint8_t)(0x40 | (dst << 3) | base));
        emit_byte(buf, 0x00);  /* disp8 = 0 */
    } else if (base == 4) {
        /* RSP needs SIB byte: mod=00, rm=4, SIB=0x24 */
        emit_byte(buf, (uint8_t)(0x00 | (dst << 3) | 0x04));
        emit_byte(buf, 0x24);  /* SIB: ss=00, idx=RSP(none), base=RSP */
    } else {
        emit_byte(buf, (uint8_t)(0x00 | (dst << 3) | base));
    }
}

/* --- MOV [r64], r64 (89 variant, mod=00) : 3 bytes --------------------- */
static void emit_store_mem_r64(CodeBuffer *buf, uint8_t base, uint8_t src)
{
    emit_byte(buf, 0x48);
    emit_byte(buf, 0x89);
    if (base == 5) {
        emit_byte(buf, (uint8_t)(0x40 | (src << 3) | base));
        emit_byte(buf, 0x00);
    } else if (base == 4) {
        emit_byte(buf, (uint8_t)(0x00 | (src << 3) | 0x04));
        emit_byte(buf, 0x24);
    } else {
        emit_byte(buf, (uint8_t)(0x00 | (src << 3) | base));
    }
}

/* --- ADD r64, r64 : 3 bytes -------------------------------------------- */
static void emit_add_r64_r64(CodeBuffer *buf, uint8_t dst, uint8_t src)
{
    emit_alu_r64_r64(buf, 0x01, dst, src);
}

/* --- SUB r/m64, r64  (29) : 3 bytes ------------------------------------ */
static void emit_sub_r64_r64(CodeBuffer *buf, uint8_t dst, uint8_t src)
{
    emit_alu_r64_r64(buf, 0x29, dst, src);
}

/* --- AND r/m64, r64  (21) : 3 bytes ------------------------------------ */
static void emit_and_r64_r64(CodeBuffer *buf, uint8_t dst, uint8_t src)
{
    emit_alu_r64_r64(buf, 0x21, dst, src);
}

/* --- OR r/m64, r64  (09) : 3 bytes ------------------------------------- */
static void emit_or_r64_r64(CodeBuffer *buf, uint8_t dst, uint8_t src)
{
    emit_alu_r64_r64(buf, 0x09, dst, src);
}

/* --- XOR r/m64, r64  (31) : 3 bytes ------------------------------------ */
static void emit_xor_r64_r64(CodeBuffer *buf, uint8_t dst, uint8_t src)
{
    emit_alu_r64_r64(buf, 0x31, dst, src);
}

/* --- NOT r/m64  (F7 /2) : 3 bytes -------------------------------------- */
static void emit_not_r64(CodeBuffer *buf, uint8_t rd)
{
    emit_byte(buf, 0x48);
    emit_byte(buf, 0xF7);
    emit_byte(buf, (uint8_t)(0xC0 | (2 << 3) | rd));  /* /2 */
}

/* --- INC r/m64  (FF /0) : 3 bytes -------------------------------------- */
static void emit_inc_r64(CodeBuffer *buf, uint8_t rd)
{
    emit_byte(buf, 0x48);
    emit_byte(buf, 0xFF);
    emit_byte(buf, (uint8_t)(0xC0 | (0 << 3) | rd));  /* /0 */
}

/* --- DEC r/m64  (FF /1) : 3 bytes -------------------------------------- */
static void emit_dec_r64(CodeBuffer *buf, uint8_t rd)
{
    emit_byte(buf, 0x48);
    emit_byte(buf, 0xFF);
    emit_byte(buf, (uint8_t)(0xC0 | (1 << 3) | rd));  /* /1 */
}

/* --- IMUL r64, r64  (0F AF) : 4 bytes ---------------------------------- */
static void emit_imul_r64_r64(CodeBuffer *buf, uint8_t dst, uint8_t src)
{
    emit_byte(buf, 0x48);
    emit_byte(buf, 0x0F);
    emit_byte(buf, 0xAF);
    emit_byte(buf, (uint8_t)(0xC0 | (dst << 3) | src));
}

/* --- CQO  (sign-extend RAX into RDX:RAX) : 2 bytes -------------------- */
static void emit_cqo(CodeBuffer *buf)
{
    emit_byte(buf, 0x48);
    emit_byte(buf, 0x99);
}

/* --- IDIV r/m64  (F7 /7) : 3 bytes ------------------------------------- */
static void emit_idiv_r64(CodeBuffer *buf, uint8_t rm)
{
    emit_byte(buf, 0x48);
    emit_byte(buf, 0xF7);
    emit_byte(buf, (uint8_t)(0xC0 | (7 << 3) | rm));  /* /7 */
}

/* --- SHL r/m64, CL  (D3 /4) : 3 bytes --------------------------------- */
static void emit_shl_r64_cl(CodeBuffer *buf, uint8_t rd)
{
    emit_byte(buf, 0x48);
    emit_byte(buf, 0xD3);
    emit_byte(buf, (uint8_t)(0xC0 | (4 << 3) | rd));
}

/* --- SHR r/m64, CL  (D3 /5) : 3 bytes --------------------------------- */
static void emit_shr_r64_cl(CodeBuffer *buf, uint8_t rd)
{
    emit_byte(buf, 0x48);
    emit_byte(buf, 0xD3);
    emit_byte(buf, (uint8_t)(0xC0 | (5 << 3) | rd));
}

/* --- SHL r/m64, imm8  (C1 /4 ib) : 4 bytes ----------------------------- */
static void emit_shl_r64_imm8(CodeBuffer *buf, uint8_t rd, uint8_t imm)
{
    emit_byte(buf, 0x48);
    emit_byte(buf, 0xC1);
    emit_byte(buf, (uint8_t)(0xC0 | (4 << 3) | rd));
    emit_byte(buf, imm);
}

/* --- SHR r/m64, imm8  (C1 /5 ib) : 4 bytes ----------------------------- */
static void emit_shr_r64_imm8(CodeBuffer *buf, uint8_t rd, uint8_t imm)
{
    emit_byte(buf, 0x48);
    emit_byte(buf, 0xC1);
    emit_byte(buf, (uint8_t)(0xC0 | (5 << 3) | rd));
    emit_byte(buf, imm);
}

/* --- CMP r/m64, r64  (39) : 3 bytes ------------------------------------ */
static void emit_cmp_r64_r64(CodeBuffer *buf, uint8_t dst, uint8_t src)
{
    emit_alu_r64_r64(buf, 0x39, dst, src);
}

/* --- CMP r/m64, imm32  (81 /7) : 7 bytes ------------------------------- */
static void emit_cmp_r64_imm32(CodeBuffer *buf, uint8_t rd, int32_t imm)
{
    emit_byte(buf, 0x48);
    emit_byte(buf, 0x81);
    emit_byte(buf, (uint8_t)(0xC0 | (7 << 3) | rd));
    emit_byte(buf, (uint8_t)( imm        & 0xFF));
    emit_byte(buf, (uint8_t)((imm >>  8) & 0xFF));
    emit_byte(buf, (uint8_t)((imm >> 16) & 0xFF));
    emit_byte(buf, (uint8_t)((imm >> 24) & 0xFF));
}

/* --- PUSH r64  (50+rd) : 1 byte ---------------------------------------- */
static void emit_push_r64(CodeBuffer *buf, uint8_t rd)
{
    emit_byte(buf, (uint8_t)(0x50 + rd));
}

/* --- POP r64  (58+rd) : 1 byte ----------------------------------------- */
static void emit_pop_r64(CodeBuffer *buf, uint8_t rd)
{
    emit_byte(buf, (uint8_t)(0x58 + rd));
}

/* --- RET  (C3) : 1 byte ------------------------------------------------ */
static void emit_ret(CodeBuffer *buf)
{
    emit_byte(buf, 0xC3);
}

/* --- NOP  (90) : 1 byte ------------------------------------------------ */
static void emit_nop(CodeBuffer *buf)
{
    emit_byte(buf, 0x90);
}

/* --- INT imm8  (CD ib) : 2 bytes --------------------------------------- */
static void emit_int_imm8(CodeBuffer *buf, uint8_t imm)
{
    emit_byte(buf, 0xCD);
    emit_byte(buf, imm);
}

/* --- rel32 placeholder helpers ----------------------------------------- */
static void emit_rel32_placeholder(CodeBuffer *buf)
{
    emit_byte(buf, 0x00);
    emit_byte(buf, 0x00);
    emit_byte(buf, 0x00);
    emit_byte(buf, 0x00);
}

static void patch_rel32(CodeBuffer *buf, int offset, int32_t value)
{
    buf->bytes[offset    ] = (uint8_t)( value        & 0xFF);
    buf->bytes[offset + 1] = (uint8_t)((value >>  8) & 0xFF);
    buf->bytes[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    buf->bytes[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

/* =========================================================================
 *  Symbol table for labels (same approach as 8051 backend)
 * ========================================================================= */
#define X64_MAX_SYMBOLS  256
#define X64_MAX_FIXUPS   256

typedef struct {
    char name[UA_MAX_LABEL_LEN];
    int  address;
} X64Symbol;

typedef struct {
    char  label[UA_MAX_LABEL_LEN];
    int   patch_offset;     /* offset into CodeBuffer where rel32 lives  */
    int   instr_end;        /* PC after the instruction (for rel calc)   */
    int   line;
} X64Fixup;

typedef struct {
    X64Symbol symbols[X64_MAX_SYMBOLS];
    int       sym_count;
    X64Fixup  fixups[X64_MAX_FIXUPS];
    int       fix_count;
} X64SymTab;

static void x64_symtab_init(X64SymTab *st)
{
    st->sym_count = 0;
    st->fix_count = 0;
}

static void x64_symtab_add(X64SymTab *st, const char *name, int address)
{
    if (st->sym_count >= X64_MAX_SYMBOLS) {
        fprintf(stderr, "x86-64: symbol table overflow\n");
        exit(1);
    }
    strncpy(st->symbols[st->sym_count].name, name, UA_MAX_LABEL_LEN - 1);
    st->symbols[st->sym_count].name[UA_MAX_LABEL_LEN - 1] = '\0';
    st->symbols[st->sym_count].address = address;
    st->sym_count++;
}

static int x64_symtab_lookup(const X64SymTab *st, const char *name)
{
    for (int i = 0; i < st->sym_count; i++) {
        if (strcmp(st->symbols[i].name, name) == 0)
            return st->symbols[i].address;
    }
    return -1;
}

static void x64_add_fixup(X64SymTab *st, const char *label,
                           int patch_offset, int instr_end, int line)
{
    if (st->fix_count >= X64_MAX_FIXUPS) {
        fprintf(stderr, "x86-64: fixup table overflow\n");
        exit(1);
    }
    X64Fixup *f = &st->fixups[st->fix_count++];
    strncpy(f->label, label, UA_MAX_LABEL_LEN - 1);
    f->label[UA_MAX_LABEL_LEN - 1] = '\0';
    f->patch_offset = patch_offset;
    f->instr_end    = instr_end;
    f->line         = line;
}

/* =========================================================================
 *  instruction_size_x64()  —  compute byte size of each instruction
 *
 *  This allows a two-pass strategy: pass 1 collects label addresses,
 *  pass 2 emits code and patches jumps.
 * ========================================================================= */
static int instruction_size_x64(const Instruction *inst)
{
    if (inst->is_label) return 0;

    switch (inst->opcode) {
        case OP_LDI:    return 7;   /* MOV r64, imm32 */
        case OP_MOV:    return 3;   /* MOV r64, r64 */
        case OP_LOAD: {
            int rs = inst->operands[1].data.reg;
            if (rs == 4) return 4;      /* RSP needs SIB */
            if (rs == 5) return 4;      /* RBP needs disp8=0 */
            return 3;
        }
        case OP_STORE: {
            int rd = inst->operands[0].data.reg;
            if (rd == 4) return 4;
            if (rd == 5) return 4;
            return 3;
        }
        case OP_ADD:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 3 : 10;
            /* reg:reg=3, reg:imm = MOV scratch,imm(7) + ADD(3) = 10 */
        case OP_SUB:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 3 : 10;
        case OP_AND:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 3 : 10;
        case OP_OR:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 3 : 10;
        case OP_XOR:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 3 : 10;
        case OP_NOT:    return 3;
        case OP_INC:    return 3;
        case OP_DEC:    return 3;
        case OP_MUL:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 4;  /* IMUL r64,r64 */
            else
                return 11; /* MOV scratch,imm(7) + IMUL(4) */
        case OP_DIV:
            /* We need: save RDX(PUSH 1), save RAX if needed,
             * MOV RAX,Rd (3), CQO (2), IDIV Rs (3), MOV Rd,RAX (3),
             * restore RDX(POP 1).  Complex — use fixed size.
             * For Rd,Rs: PUSH RDX(1) + MOV RAX,Rd(3) + CQO(2) + IDIV Rs(3)
             *            + MOV Rd,RAX(3) + POP RDX(1) = 13  (if Rd!=RAX,Rs!=RDX)
             * Simplified: always 13 for reg, 20 for imm */
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 13;
            else
                return 20; /* MOV scratch,imm(7) + 13 for division */
        case OP_SHL:
            return (inst->operands[1].type == OPERAND_IMMEDIATE) ? 4 : 13;
            /* imm: SHL r64,imm8 = 4bytes
             * reg: PUSH RCX(1) + MOV RCX,Rs(3) + SHL Rd,CL(3) + POP RCX(1) = 8
             *      but if Rs==RCX already => less. Use conservative 13 */
        case OP_SHR:
            return (inst->operands[1].type == OPERAND_IMMEDIATE) ? 4 : 13;
        case OP_CMP:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 3;
            else
                return 7;  /* CMP r/m64, imm32 */
        case OP_JMP:    return 5;   /* E9 rel32 */
        case OP_JZ:     return 6;   /* 0F 84 rel32 */
        case OP_JNZ:    return 6;   /* 0F 85 rel32 */
        case OP_CALL:   return 5;   /* E8 rel32 */
        case OP_RET:    return 1;
        case OP_PUSH:   return 1;
        case OP_POP:    return 1;
        case OP_NOP:    return 1;
        case OP_HLT:    return 1;   /* -> RET */
        case OP_INT:    return 2;   /* CD ib */

        /* ---- Variable pseudo-instructions ----------------------------- */
        case OP_VAR:    return 0;   /* declaration only, no code emitted  */
        case OP_SET:
            /* SET name, Rs  →  MOV [RIP+disp32], r64  (7 bytes)
             * SET name, imm →  MOV qword [RIP+disp32], imm32 (11 bytes) */
            if (inst->operands[1].type == OPERAND_REGISTER) return 7;
            return 11;
        case OP_GET:
            /* GET Rd, name  →  MOV r64, [RIP+disp32]  (7 bytes) */
            return 7;
        default:        return 0;
    }
}

/* =========================================================================
 *  Variable table — compiler-managed named storage
 *
 *  Variables declared with VAR are allocated at the end of the code
 *  segment (8 bytes each for x86-64).  Their addresses are registered
 *  as symbols so that SET/GET can use the fixup mechanism for
 *  RIP-relative addressing.
 * ========================================================================= */
#define X64_MAX_VARS     256
#define X64_VAR_SIZE     8      /* bytes per variable (qword) */

typedef struct {
    char    name[UA_MAX_LABEL_LEN];
    int64_t init_value;
    int     has_init;
} X64VarEntry;

typedef struct {
    X64VarEntry vars[X64_MAX_VARS];
    int         count;
} X64VarTable;

static void x64_vartab_init(X64VarTable *vt)
{
    vt->count = 0;
}

static int x64_vartab_add(X64VarTable *vt, const char *name,
                           int64_t init_value, int has_init)
{
    /* Check for duplicate */
    for (int i = 0; i < vt->count; i++) {
        if (strcmp(vt->vars[i].name, name) == 0) {
            fprintf(stderr, "x86-64: duplicate variable '%s'\n", name);
            return -1;
        }
    }
    if (vt->count >= X64_MAX_VARS) {
        fprintf(stderr, "x86-64: variable table overflow (max %d)\n",
                X64_MAX_VARS);
        return -1;
    }
    X64VarEntry *v = &vt->vars[vt->count++];
    strncpy(v->name, name, UA_MAX_LABEL_LEN - 1);
    v->name[UA_MAX_LABEL_LEN - 1] = '\0';
    v->init_value = init_value;
    v->has_init   = has_init;
    return 0;
}

/* =========================================================================
 *  generate_x86_64()  —  main entry point  (two-pass)
 * ========================================================================= */
CodeBuffer* generate_x86_64(const Instruction *ir, int ir_count)
{
    fprintf(stderr, "[x86-64] Generating code for %d IR instructions ...\n",
            ir_count);

    /* --- Pass 1: collect label addresses + variable declarations ------- */
    X64SymTab symtab;
    x64_symtab_init(&symtab);

    X64VarTable vartab;
    x64_vartab_init(&vartab);

    int pc = 0;
    for (int i = 0; i < ir_count; i++) {
        const Instruction *inst = &ir[i];
        if (inst->is_label) {
            x64_symtab_add(&symtab, inst->label_name, pc);
        } else if (inst->opcode == OP_VAR) {
            /* Collect variable declaration — no code emitted */
            const char *vname = inst->operands[0].data.label;
            int64_t init_val  = 0;
            int     has_init  = 0;
            if (inst->operand_count >= 2 &&
                inst->operands[1].type == OPERAND_IMMEDIATE) {
                init_val = inst->operands[1].data.imm;
                has_init = 1;
            }
            x64_vartab_add(&vartab, vname, init_val, has_init);
        } else {
            pc += instruction_size_x64(inst);
        }
    }

    /* Register variable symbols: each lives at code_end + index * 8 */
    int var_base = pc;   /* total code size */
    for (int v = 0; v < vartab.count; v++) {
        x64_symtab_add(&symtab, vartab.vars[v].name,
                        var_base + v * X64_VAR_SIZE);
    }

    /* --- Pass 2: code emission ----------------------------------------- */
    CodeBuffer *code = create_code_buffer();
    if (!code) {
        fprintf(stderr, "UA x86-64: out of memory\n");
        return NULL;
    }

    for (int i = 0; i < ir_count; i++) {
        const Instruction *inst = &ir[i];

        if (inst->is_label)
            continue;

        switch (inst->opcode) {

        /* ---- LDI Rd, #imm  ->  MOV r64, imm32 ------------ 7 bytes --- */
        case OP_LDI: {
            int rd = inst->operands[0].data.reg;
            int32_t imm = (int32_t)inst->operands[1].data.imm;
            x64_validate_register(inst, rd);
            uint8_t enc = X64_REG_ENC[rd];
            fprintf(stderr, "  LDI R%d -> MOV %s, %d\n",
                    rd, X64_REG_NAME[rd], imm);
            emit_mov_r64_imm32(code, enc, imm);
            break;
        }

        /* ---- MOV Rd, Rs  ->  MOV r64, r64 ----------------- 3 bytes -- */
        case OP_MOV: {
            int rd = inst->operands[0].data.reg;
            int rs = inst->operands[1].data.reg;
            x64_validate_register(inst, rd);
            x64_validate_register(inst, rs);
            uint8_t enc_d = X64_REG_ENC[rd];
            uint8_t enc_s = X64_REG_ENC[rs];
            fprintf(stderr, "  MOV R%d, R%d -> MOV %s, %s\n",
                    rd, rs, X64_REG_NAME[rd], X64_REG_NAME[rs]);
            emit_mov_r64_r64(code, enc_d, enc_s);
            break;
        }

        /* ---- LOAD Rd, Rs  ->  MOV r64, [r64] -------------- 3-4 bytes  */
        case OP_LOAD: {
            int rd = inst->operands[0].data.reg;
            int rs = inst->operands[1].data.reg;
            x64_validate_register(inst, rd);
            x64_validate_register(inst, rs);
            fprintf(stderr, "  LOAD R%d, R%d -> MOV %s, [%s]\n",
                    rd, rs, X64_REG_NAME[rd], X64_REG_NAME[rs]);
            emit_load_r64_mem(code, X64_REG_ENC[rd], X64_REG_ENC[rs]);
            break;
        }

        /* ---- STORE Rx, Ry  ->  MOV [Rx], Ry --------------- 3-4 bytes  */
        case OP_STORE: {
            int rx = inst->operands[0].data.reg;
            int ry = inst->operands[1].data.reg;
            x64_validate_register(inst, rx);
            x64_validate_register(inst, ry);
            fprintf(stderr, "  STORE R%d, R%d -> MOV [%s], %s\n",
                    rx, ry, X64_REG_NAME[rx], X64_REG_NAME[ry]);
            emit_store_mem_r64(code, X64_REG_ENC[rx], X64_REG_ENC[ry]);
            break;
        }

        /* ---- ADD Rd, Rs/imm -------------------------------- 3/10 bytes */
        case OP_ADD: {
            int rd = inst->operands[0].data.reg;
            x64_validate_register(inst, rd);
            uint8_t enc_d = X64_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                x64_validate_register(inst, rs);
                fprintf(stderr, "  ADD R%d, R%d -> ADD %s, %s\n",
                        rd, rs, X64_REG_NAME[rd], X64_REG_NAME[rs]);
                emit_add_r64_r64(code, enc_d, X64_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t scratch = (enc_d == 1) ? 2 : 1;
                fprintf(stderr, "  ADD R%d, #%d -> MOV scratch, %d; ADD %s, scratch\n",
                        rd, imm, imm, X64_REG_NAME[rd]);
                emit_mov_r64_imm32(code, scratch, imm);
                emit_add_r64_r64(code, enc_d, scratch);
            }
            break;
        }

        /* ---- SUB Rd, Rs/imm -------------------------------- 3/10 bytes */
        case OP_SUB: {
            int rd = inst->operands[0].data.reg;
            x64_validate_register(inst, rd);
            uint8_t enc_d = X64_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                x64_validate_register(inst, rs);
                fprintf(stderr, "  SUB R%d, R%d -> SUB %s, %s\n",
                        rd, rs, X64_REG_NAME[rd], X64_REG_NAME[rs]);
                emit_sub_r64_r64(code, enc_d, X64_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t scratch = (enc_d == 1) ? 2 : 1;
                fprintf(stderr, "  SUB R%d, #%d -> MOV scratch, %d; SUB %s, scratch\n",
                        rd, imm, imm, X64_REG_NAME[rd]);
                emit_mov_r64_imm32(code, scratch, imm);
                emit_sub_r64_r64(code, enc_d, scratch);
            }
            break;
        }

        /* ---- AND Rd, Rs/imm -------------------------------- 3/10 bytes */
        case OP_AND: {
            int rd = inst->operands[0].data.reg;
            x64_validate_register(inst, rd);
            uint8_t enc_d = X64_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                x64_validate_register(inst, rs);
                fprintf(stderr, "  AND R%d, R%d -> AND %s, %s\n",
                        rd, rs, X64_REG_NAME[rd], X64_REG_NAME[rs]);
                emit_and_r64_r64(code, enc_d, X64_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t scratch = (enc_d == 1) ? 2 : 1;
                fprintf(stderr, "  AND R%d, #%d\n", rd, imm);
                emit_mov_r64_imm32(code, scratch, imm);
                emit_and_r64_r64(code, enc_d, scratch);
            }
            break;
        }

        /* ---- OR Rd, Rs/imm --------------------------------- 3/10 bytes */
        case OP_OR: {
            int rd = inst->operands[0].data.reg;
            x64_validate_register(inst, rd);
            uint8_t enc_d = X64_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                x64_validate_register(inst, rs);
                fprintf(stderr, "  OR  R%d, R%d -> OR %s, %s\n",
                        rd, rs, X64_REG_NAME[rd], X64_REG_NAME[rs]);
                emit_or_r64_r64(code, enc_d, X64_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t scratch = (enc_d == 1) ? 2 : 1;
                fprintf(stderr, "  OR  R%d, #%d\n", rd, imm);
                emit_mov_r64_imm32(code, scratch, imm);
                emit_or_r64_r64(code, enc_d, scratch);
            }
            break;
        }

        /* ---- XOR Rd, Rs/imm -------------------------------- 3/10 bytes */
        case OP_XOR: {
            int rd = inst->operands[0].data.reg;
            x64_validate_register(inst, rd);
            uint8_t enc_d = X64_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                x64_validate_register(inst, rs);
                fprintf(stderr, "  XOR R%d, R%d -> XOR %s, %s\n",
                        rd, rs, X64_REG_NAME[rd], X64_REG_NAME[rs]);
                emit_xor_r64_r64(code, enc_d, X64_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t scratch = (enc_d == 1) ? 2 : 1;
                fprintf(stderr, "  XOR R%d, #%d\n", rd, imm);
                emit_mov_r64_imm32(code, scratch, imm);
                emit_xor_r64_r64(code, enc_d, scratch);
            }
            break;
        }

        /* ---- NOT Rd  ->  NOT r64 --------------------------- 3 bytes -- */
        case OP_NOT: {
            int rd = inst->operands[0].data.reg;
            x64_validate_register(inst, rd);
            fprintf(stderr, "  NOT R%d -> NOT %s\n", rd, X64_REG_NAME[rd]);
            emit_not_r64(code, X64_REG_ENC[rd]);
            break;
        }

        /* ---- INC Rd  ->  INC r64 --------------------------- 3 bytes -- */
        case OP_INC: {
            int rd = inst->operands[0].data.reg;
            x64_validate_register(inst, rd);
            fprintf(stderr, "  INC R%d -> INC %s\n", rd, X64_REG_NAME[rd]);
            emit_inc_r64(code, X64_REG_ENC[rd]);
            break;
        }

        /* ---- DEC Rd  ->  DEC r64 --------------------------- 3 bytes -- */
        case OP_DEC: {
            int rd = inst->operands[0].data.reg;
            x64_validate_register(inst, rd);
            fprintf(stderr, "  DEC R%d -> DEC %s\n", rd, X64_REG_NAME[rd]);
            emit_dec_r64(code, X64_REG_ENC[rd]);
            break;
        }

        /* ---- MUL Rd, Rs/imm  ->  IMUL r64, r64 ------- 4/11 bytes ---- */
        case OP_MUL: {
            int rd = inst->operands[0].data.reg;
            x64_validate_register(inst, rd);
            uint8_t enc_d = X64_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                x64_validate_register(inst, rs);
                fprintf(stderr, "  MUL R%d, R%d -> IMUL %s, %s\n",
                        rd, rs, X64_REG_NAME[rd], X64_REG_NAME[rs]);
                emit_imul_r64_r64(code, enc_d, X64_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t scratch = (enc_d == 1) ? 2 : 1;
                fprintf(stderr, "  MUL R%d, #%d -> MOV scratch, %d; IMUL %s, scratch\n",
                        rd, imm, imm, X64_REG_NAME[rd]);
                emit_mov_r64_imm32(code, scratch, imm);
                emit_imul_r64_r64(code, enc_d, scratch);
            }
            break;
        }

        /* ---- DIV Rd, Rs/imm  ->  PUSH RDX; MOV RAX,Rd; CQO;
         *       IDIV Rs; MOV Rd,RAX; POP RDX
         *       Register variant: 13 bytes.  Imm: 20 bytes.
         * --------------------------------------------------------------- */
        case OP_DIV: {
            int rd = inst->operands[0].data.reg;
            x64_validate_register(inst, rd);
            uint8_t enc_d = X64_REG_ENC[rd];

            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                x64_validate_register(inst, rs);
                uint8_t enc_s = X64_REG_ENC[rs];
                fprintf(stderr, "  DIV R%d, R%d -> IDIV\n", rd, rs);
                emit_push_r64(code, 2);            /* PUSH RDX      1 */
                emit_mov_r64_r64(code, 0, enc_d);  /* MOV RAX, Rd   3 */
                emit_cqo(code);                    /* CQO            2 */
                emit_idiv_r64(code, enc_s);        /* IDIV Rs        3 */
                emit_mov_r64_r64(code, enc_d, 0);  /* MOV Rd, RAX   3 */
                emit_pop_r64(code, 2);             /* POP RDX       1 */
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                /* Use a scratch reg that isn't RAX(0), RDX(2), or Rd */
                uint8_t scratch = 1; /* RCX */
                if (enc_d == 1) scratch = 3; /* RBX if Rd=RCX */
                fprintf(stderr, "  DIV R%d, #%d -> MOV scratch, %d; IDIV\n",
                        rd, imm, imm);
                emit_push_r64(code, 2);                /* PUSH RDX   1 */
                emit_mov_r64_imm32(code, scratch, imm); /* MOV scr,imm 7 */
                emit_mov_r64_r64(code, 0, enc_d);      /* MOV RAX,Rd 3 */
                emit_cqo(code);                        /* CQO         2 */
                emit_idiv_r64(code, scratch);          /* IDIV scr    3 */
                emit_mov_r64_r64(code, enc_d, 0);      /* MOV Rd,RAX 3 */
                emit_pop_r64(code, 2);                 /* POP RDX    1 */
            }
            break;
        }

        /* ---- SHL Rd, Rs/imm -------------------------------- 4/13 bytes */
        case OP_SHL: {
            int rd = inst->operands[0].data.reg;
            x64_validate_register(inst, rd);
            uint8_t enc_d = X64_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_IMMEDIATE) {
                uint8_t imm = (uint8_t)(inst->operands[1].data.imm & 0x3F);
                fprintf(stderr, "  SHL R%d, #%d\n", rd, imm);
                emit_shl_r64_imm8(code, enc_d, imm);
            } else {
                int rs = inst->operands[1].data.reg;
                x64_validate_register(inst, rs);
                uint8_t enc_s = X64_REG_ENC[rs];
                fprintf(stderr, "  SHL R%d, R%d -> SHL %s, CL\n",
                        rd, rs, X64_REG_NAME[rd]);
                /* Save RCX, move shift count to CL, shift, restore */
                emit_push_r64(code, 1);            /* PUSH RCX       1 */
                emit_mov_r64_r64(code, 1, enc_s);  /* MOV RCX, Rs    3 */
                emit_shl_r64_cl(code, enc_d);      /* SHL Rd, CL     3 */
                emit_pop_r64(code, 1);             /* POP RCX        1 */
                /* pad to 13 bytes: emit 5 NOPs  (8 emitted above) */
                for (int p = 0; p < 5; p++) emit_nop(code);
            }
            break;
        }

        /* ---- SHR Rd, Rs/imm -------------------------------- 4/13 bytes */
        case OP_SHR: {
            int rd = inst->operands[0].data.reg;
            x64_validate_register(inst, rd);
            uint8_t enc_d = X64_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_IMMEDIATE) {
                uint8_t imm = (uint8_t)(inst->operands[1].data.imm & 0x3F);
                fprintf(stderr, "  SHR R%d, #%d\n", rd, imm);
                emit_shr_r64_imm8(code, enc_d, imm);
            } else {
                int rs = inst->operands[1].data.reg;
                x64_validate_register(inst, rs);
                uint8_t enc_s = X64_REG_ENC[rs];
                fprintf(stderr, "  SHR R%d, R%d -> SHR %s, CL\n",
                        rd, rs, X64_REG_NAME[rd]);
                emit_push_r64(code, 1);
                emit_mov_r64_r64(code, 1, enc_s);
                emit_shr_r64_cl(code, enc_d);
                emit_pop_r64(code, 1);
                for (int p = 0; p < 5; p++) emit_nop(code);
            }
            break;
        }

        /* ---- CMP Ra, Rb/imm -------------------------------- 3/7 bytes  */
        case OP_CMP: {
            int ra = inst->operands[0].data.reg;
            x64_validate_register(inst, ra);
            uint8_t enc_a = X64_REG_ENC[ra];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rb = inst->operands[1].data.reg;
                x64_validate_register(inst, rb);
                fprintf(stderr, "  CMP R%d, R%d -> CMP %s, %s\n",
                        ra, rb, X64_REG_NAME[ra], X64_REG_NAME[rb]);
                emit_cmp_r64_r64(code, enc_a, X64_REG_ENC[rb]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                fprintf(stderr, "  CMP R%d, #%d\n", ra, imm);
                emit_cmp_r64_imm32(code, enc_a, imm);
            }
            break;
        }

        /* ---- JMP label  ->  JMP rel32 ---------------------- 5 bytes -- */
        case OP_JMP: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  JMP %s\n", label);
            emit_byte(code, 0xE9);
            int patch_off = code->size;
            emit_rel32_placeholder(code);
            x64_add_fixup(&symtab, label, patch_off, code->size, inst->line);
            break;
        }

        /* ---- JZ label  ->  JZ rel32 (0F 84) --------------- 6 bytes -- */
        case OP_JZ: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  JZ  %s\n", label);
            emit_byte(code, 0x0F);
            emit_byte(code, 0x84);
            int patch_off = code->size;
            emit_rel32_placeholder(code);
            x64_add_fixup(&symtab, label, patch_off, code->size, inst->line);
            break;
        }

        /* ---- JNZ label  ->  JNZ rel32 (0F 85) ------------- 6 bytes -- */
        case OP_JNZ: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  JNZ %s\n", label);
            emit_byte(code, 0x0F);
            emit_byte(code, 0x85);
            int patch_off = code->size;
            emit_rel32_placeholder(code);
            x64_add_fixup(&symtab, label, patch_off, code->size, inst->line);
            break;
        }

        /* ---- CALL label  ->  CALL rel32 -------------------- 5 bytes -- */
        case OP_CALL: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  CALL %s\n", label);
            emit_byte(code, 0xE8);
            int patch_off = code->size;
            emit_rel32_placeholder(code);
            x64_add_fixup(&symtab, label, patch_off, code->size, inst->line);
            break;
        }

        /* ---- RET ------------------------------------------- 1 byte -- */
        case OP_RET:
            fprintf(stderr, "  RET\n");
            emit_ret(code);
            break;

        /* ---- PUSH Rs ---------------------------------------- 1 byte -- */
        case OP_PUSH: {
            int rs = inst->operands[0].data.reg;
            x64_validate_register(inst, rs);
            fprintf(stderr, "  PUSH R%d -> PUSH %s\n", rs, X64_REG_NAME[rs]);
            emit_push_r64(code, X64_REG_ENC[rs]);
            break;
        }

        /* ---- POP Rd ----------------------------------------- 1 byte -- */
        case OP_POP: {
            int rd = inst->operands[0].data.reg;
            x64_validate_register(inst, rd);
            fprintf(stderr, "  POP  R%d -> POP %s\n", rd, X64_REG_NAME[rd]);
            emit_pop_r64(code, X64_REG_ENC[rd]);
            break;
        }

        /* ---- NOP -------------------------------------------- 1 byte -- */
        case OP_NOP:
            fprintf(stderr, "  NOP\n");
            emit_nop(code);
            break;

        /* ---- HLT  ->  RET ---------------------------------- 1 byte -- */
        case OP_HLT:
            fprintf(stderr, "  HLT -> RET\n");
            emit_ret(code);
            break;

        /* ---- INT #imm  ->  INT imm8 (CD ib) --------------- 2 bytes -- */
        case OP_INT: {
            uint8_t imm = (uint8_t)(inst->operands[0].data.imm & 0xFF);
            fprintf(stderr, "  INT #%d -> INT 0x%02X\n", imm, imm);
            emit_int_imm8(code, imm);
            break;
        }

        /* ---- VAR  — declaration only, no code emitted ----------------- */
        case OP_VAR:
            /* Already handled in pass 1 */
            break;

        /* ---- SET name, Rs/imm  →  MOV [RIP+disp32], r64/imm ---------- */
        case OP_SET: {
            const char *vname = inst->operands[0].data.label;
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                x64_validate_register(inst, rs);
                fprintf(stderr, "  SET %s, R%d -> MOV [RIP+disp32], r64\n",
                        vname, rs);
                /* REX.W prefix (+ REX.R if reg >= 8) */
                emit_byte(code, (uint8_t)(0x48 | ((rs >= 8) ? 0x04 : 0x00)));
                emit_byte(code, 0x89);  /* MOV r/m64, r64 */
                emit_byte(code, (uint8_t)(((rs & 7) << 3) | 0x05));  /* ModRM: mod=00 rm=101 (RIP-rel) */
                int patch_off = code->size;
                emit_rel32_placeholder(code);
                x64_add_fixup(&symtab, vname, patch_off, code->size,
                              inst->line);
            } else {
                /* Immediate: MOV qword [RIP+disp32], imm32 */
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                fprintf(stderr, "  SET %s, #%d -> MOV [RIP+disp32], imm32\n",
                        vname, imm);
                emit_byte(code, 0x48);  /* REX.W */
                emit_byte(code, 0xC7);  /* MOV r/m64, imm32 */
                emit_byte(code, 0x05);  /* ModRM: mod=00 reg=000 rm=101 */
                int patch_off = code->size;
                emit_rel32_placeholder(code);
                /* imm32 */
                emit_byte(code, (uint8_t)( imm        & 0xFF));
                emit_byte(code, (uint8_t)((imm >>  8) & 0xFF));
                emit_byte(code, (uint8_t)((imm >> 16) & 0xFF));
                emit_byte(code, (uint8_t)((imm >> 24) & 0xFF));
                x64_add_fixup(&symtab, vname, patch_off,
                              code->size,  /* instr_end = end of full instruction */
                              inst->line);
            }
            break;
        }

        /* ---- GET Rd, name  →  MOV r64, [RIP+disp32] ------------------ */
        case OP_GET: {
            int rd = inst->operands[0].data.reg;
            const char *vname = inst->operands[1].data.label;
            x64_validate_register(inst, rd);
            fprintf(stderr, "  GET R%d, %s -> MOV r64, [RIP+disp32]\n",
                    rd, vname);
            /* REX.W prefix (+ REX.R if reg >= 8) */
            emit_byte(code, (uint8_t)(0x48 | ((rd >= 8) ? 0x04 : 0x00)));
            emit_byte(code, 0x8B);  /* MOV r64, r/m64 */
            emit_byte(code, (uint8_t)(((rd & 7) << 3) | 0x05));  /* ModRM */
            int patch_off = code->size;
            emit_rel32_placeholder(code);
            x64_add_fixup(&symtab, vname, patch_off, code->size,
                          inst->line);
            break;
        }

        default: {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "opcode '%s' is not supported by the x86-64 backend",
                     opcode_name(inst->opcode));
            x64_error(inst, msg);
            break;
        }
        }
    }

    /* --- Pass 3: patch relocations ------------------------------------- */
    for (int f = 0; f < symtab.fix_count; f++) {
        X64Fixup *fix = &symtab.fixups[f];
        int target = x64_symtab_lookup(&symtab, fix->label);
        if (target < 0) {
            fprintf(stderr, "x86-64: undefined label or variable '%s' "
                    "(line %d)\n", fix->label, fix->line);
            free_code_buffer(code);
            return NULL;
        }
        int32_t rel = (int32_t)(target - fix->instr_end);
        patch_rel32(code, fix->patch_offset, rel);
    }

    /* --- Append variable data section ---------------------------------- */
    for (int v = 0; v < vartab.count; v++) {
        int64_t val = vartab.vars[v].has_init ? vartab.vars[v].init_value : 0;
        /* Emit 8 bytes (little-endian qword) */
        for (int b = 0; b < X64_VAR_SIZE; b++) {
            emit_byte(code, (uint8_t)((val >> (b * 8)) & 0xFF));
        }
    }

    fprintf(stderr, "[x86-64] Emitted %d bytes (%d code + %d data)\n",
            code->size, var_base, vartab.count * X64_VAR_SIZE);
    return code;
}
