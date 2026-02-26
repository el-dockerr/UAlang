/*
 * =============================================================================
 *  UA - Unified Assembler
 *  x86-32 Back-End (Code Generation)
 *
 *  File:    backend_x86_32.c
 *  Purpose: Translate UA IR into raw x86-32 (IA-32) machine code.
 *
 *  ┌──────────────────────────────────────────────────────────────────────┐
 *  │  x86-32 Instruction Encoding Reference (full MVIS ISA)            │
 *  │                                                                    │
 *  │  Register encoding (low 3 bits):                                   │
 *  │    EAX=0  ECX=1  EDX=2  EBX=3  ESP=4  EBP=5  ESI=6  EDI=7       │
 *  │                                                                    │
 *  │  MOV  r32, imm32    B8+rd id                5 bytes                │
 *  │  MOV  r32, r32      89 ModRM                2 bytes                │
 *  │  MOV  r32,[r32]     8B ModRM                2 bytes (LOAD)         │
 *  │  MOV  [r32],r32     89 ModRM                2 bytes (STORE)        │
 *  │  ADD  r32, r32      01 ModRM                2 bytes                │
 *  │  SUB  r32, r32      29 ModRM                2 bytes                │
 *  │  AND  r32, r32      21 ModRM                2 bytes                │
 *  │  OR   r32, r32      09 ModRM                2 bytes                │
 *  │  XOR  r32, r32      31 ModRM                2 bytes                │
 *  │  NOT  r32           F7 /2                   2 bytes                │
 *  │  INC  r32           40+rd                   1 byte                 │
 *  │  DEC  r32           48+rd                   1 byte                 │
 *  │  IMUL r32, r32      0F AF ModRM             3 bytes                │
 *  │  CDQ                99                      1 byte                 │
 *  │  IDIV r32           F7 /7                   2 bytes                │
 *  │  SHL  r32, CL       D3 /4                   2 bytes                │
 *  │  SHR  r32, CL       D3 /5                   2 bytes                │
 *  │  SHL  r32, imm8     C1 /4 ib                3 bytes                │
 *  │  SHR  r32, imm8     C1 /5 ib                3 bytes                │
 *  │  CMP  r32, r32      39 ModRM                2 bytes                │
 *  │  CMP  r32, imm32    81 /7 id                6 bytes                │
 *  │  JMP  rel32         E9 cd                   5 bytes                │
 *  │  JZ   rel32         0F 84 cd                6 bytes                │
 *  │  JNZ  rel32         0F 85 cd                6 bytes                │
 *  │  CALL rel32         E8 cd                   5 bytes                │
 *  │  RET                C3                      1 byte                 │
 *  │  PUSH r32           50+rd                   1 byte                 │
 *  │  POP  r32           58+rd                   1 byte                 │
 *  │  NOP                90                      1 byte                 │
 *  │  INT  imm8          CD ib                   2 bytes                │
 *  └──────────────────────────────────────────────────────────────────────┘
 *
 *  UA registers R0-R7 map to EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI.
 *  R8-R15 are rejected (not available in 32-bit mode).
 *
 *  Key difference from x86-64:
 *    - No REX prefix needed (all registers 0-7 addressable natively)
 *    - MOV r32, imm32 uses 1-byte opcode B8+rd instead of REX.W C7
 *    - INC/DEC use single-byte 40+rd / 48+rd (not available in 64-bit)
 *    - CDQ instead of CQO for sign-extension before IDIV
 *    - All pointers and immediates are 32-bit
 *
 *  License: MIT
 * =============================================================================
 */

#include "backend_x86_32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 *  x86-32 register encoding table
 * ========================================================================= */
#define X32_MAX_REG  8

static const uint8_t X32_REG_ENC[X32_MAX_REG] = {
    0,  /* R0 -> EAX */
    1,  /* R1 -> ECX */
    2,  /* R2 -> EDX */
    3,  /* R3 -> EBX */
    4,  /* R4 -> ESP */
    5,  /* R5 -> EBP */
    6,  /* R6 -> ESI */
    7   /* R7 -> EDI */
};

static const char* X32_REG_NAME[X32_MAX_REG] = {
    "EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI"
};

/* =========================================================================
 *  Error helpers
 * ========================================================================= */
static void x32_error(const Instruction *inst, const char *msg)
{
    fprintf(stderr,
            "\n"
            "  UA x86-32 Backend Error\n"
            "  ------------------------\n"
            "  Line %d, Column %d: %s\n\n",
            inst->line, inst->column, msg);
    exit(1);
}

static void x32_validate_register(const Instruction *inst, int reg)
{
    if (reg < 0 || reg >= X32_MAX_REG) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "register R%d is not mapped in the x86-32 backend "
                 "(supports R0-R7: EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI)",
                 reg);
        x32_error(inst, msg);
    }
}

/* =========================================================================
 *  Emit helpers  —  build x86-32 instruction bytes
 * ========================================================================= */

/* --- MOV r32, imm32 : B8+rd id  — 5 bytes ------------------------------ */
static void emit_mov_r32_imm32(CodeBuffer *buf, uint8_t rd, int32_t imm)
{
    emit_byte(buf, (uint8_t)(0xB8 + rd));
    emit_byte(buf, (uint8_t)( imm        & 0xFF));
    emit_byte(buf, (uint8_t)((imm >>  8) & 0xFF));
    emit_byte(buf, (uint8_t)((imm >> 16) & 0xFF));
    emit_byte(buf, (uint8_t)((imm >> 24) & 0xFF));
}

/* --- Generic opcode + ModR/M(reg,rm) : 2 bytes ------------------------- */
static void emit_alu_r32_r32(CodeBuffer *buf, uint8_t opcode,
                             uint8_t dst, uint8_t src)
{
    emit_byte(buf, opcode);
    emit_byte(buf, (uint8_t)(0xC0 | (src << 3) | dst));
}

/* --- MOV r32, r32  (89 variant: MOV r/m32, r32) : 2 bytes ------------- */
static void emit_mov_r32_r32(CodeBuffer *buf, uint8_t dst, uint8_t src)
{
    emit_alu_r32_r32(buf, 0x89, dst, src);
}

/* --- MOV r32, [r32] (8B variant: MOV r32, r/m32, mod=00) : 2+ bytes --- */
static void emit_load_r32_mem(CodeBuffer *buf, uint8_t dst, uint8_t base)
{
    emit_byte(buf, 0x8B);
    if (base == 5) {
        /* EBP needs mod=01 + disp8=0 */
        emit_byte(buf, (uint8_t)(0x40 | (dst << 3) | base));
        emit_byte(buf, 0x00);
    } else if (base == 4) {
        /* ESP needs SIB byte */
        emit_byte(buf, (uint8_t)(0x00 | (dst << 3) | 0x04));
        emit_byte(buf, 0x24);
    } else {
        emit_byte(buf, (uint8_t)(0x00 | (dst << 3) | base));
    }
}

/* --- MOV [r32], r32 (89 variant, mod=00) : 2+ bytes ------------------- */
static void emit_store_mem_r32(CodeBuffer *buf, uint8_t base, uint8_t src)
{
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

/* --- ADD r32, r32 : 2 bytes -------------------------------------------- */
static void emit_add_r32_r32(CodeBuffer *buf, uint8_t dst, uint8_t src)
{
    emit_alu_r32_r32(buf, 0x01, dst, src);
}

/* --- SUB r/m32, r32  (29) : 2 bytes ------------------------------------ */
static void emit_sub_r32_r32(CodeBuffer *buf, uint8_t dst, uint8_t src)
{
    emit_alu_r32_r32(buf, 0x29, dst, src);
}

/* --- AND r/m32, r32  (21) : 2 bytes ------------------------------------ */
static void emit_and_r32_r32(CodeBuffer *buf, uint8_t dst, uint8_t src)
{
    emit_alu_r32_r32(buf, 0x21, dst, src);
}

/* --- OR r/m32, r32  (09) : 2 bytes ------------------------------------- */
static void emit_or_r32_r32(CodeBuffer *buf, uint8_t dst, uint8_t src)
{
    emit_alu_r32_r32(buf, 0x09, dst, src);
}

/* --- XOR r/m32, r32  (31) : 2 bytes ------------------------------------ */
static void emit_xor_r32_r32(CodeBuffer *buf, uint8_t dst, uint8_t src)
{
    emit_alu_r32_r32(buf, 0x31, dst, src);
}

/* --- NOT r/m32  (F7 /2) : 2 bytes -------------------------------------- */
static void emit_not_r32(CodeBuffer *buf, uint8_t rd)
{
    emit_byte(buf, 0xF7);
    emit_byte(buf, (uint8_t)(0xC0 | (2 << 3) | rd));
}

/* --- INC r32  (40+rd) : 1 byte ----------------------------------------- */
static void emit_inc_r32(CodeBuffer *buf, uint8_t rd)
{
    emit_byte(buf, (uint8_t)(0x40 + rd));
}

/* --- DEC r32  (48+rd) : 1 byte ----------------------------------------- */
static void emit_dec_r32(CodeBuffer *buf, uint8_t rd)
{
    emit_byte(buf, (uint8_t)(0x48 + rd));
}

/* --- IMUL r32, r32  (0F AF) : 3 bytes ---------------------------------- */
static void emit_imul_r32_r32(CodeBuffer *buf, uint8_t dst, uint8_t src)
{
    emit_byte(buf, 0x0F);
    emit_byte(buf, 0xAF);
    emit_byte(buf, (uint8_t)(0xC0 | (dst << 3) | src));
}

/* --- CDQ  (sign-extend EAX into EDX:EAX) : 1 byte --------------------- */
static void emit_cdq(CodeBuffer *buf)
{
    emit_byte(buf, 0x99);
}

/* --- IDIV r/m32  (F7 /7) : 2 bytes ------------------------------------- */
static void emit_idiv_r32(CodeBuffer *buf, uint8_t rm)
{
    emit_byte(buf, 0xF7);
    emit_byte(buf, (uint8_t)(0xC0 | (7 << 3) | rm));
}

/* --- SHL r/m32, CL  (D3 /4) : 2 bytes --------------------------------- */
static void emit_shl_r32_cl(CodeBuffer *buf, uint8_t rd)
{
    emit_byte(buf, 0xD3);
    emit_byte(buf, (uint8_t)(0xC0 | (4 << 3) | rd));
}

/* --- SHR r/m32, CL  (D3 /5) : 2 bytes --------------------------------- */
static void emit_shr_r32_cl(CodeBuffer *buf, uint8_t rd)
{
    emit_byte(buf, 0xD3);
    emit_byte(buf, (uint8_t)(0xC0 | (5 << 3) | rd));
}

/* --- SHL r/m32, imm8  (C1 /4 ib) : 3 bytes ----------------------------- */
static void emit_shl_r32_imm8(CodeBuffer *buf, uint8_t rd, uint8_t imm)
{
    emit_byte(buf, 0xC1);
    emit_byte(buf, (uint8_t)(0xC0 | (4 << 3) | rd));
    emit_byte(buf, imm);
}

/* --- SHR r/m32, imm8  (C1 /5 ib) : 3 bytes ----------------------------- */
static void emit_shr_r32_imm8(CodeBuffer *buf, uint8_t rd, uint8_t imm)
{
    emit_byte(buf, 0xC1);
    emit_byte(buf, (uint8_t)(0xC0 | (5 << 3) | rd));
    emit_byte(buf, imm);
}

/* --- CMP r/m32, r32  (39) : 2 bytes ------------------------------------ */
static void emit_cmp_r32_r32(CodeBuffer *buf, uint8_t dst, uint8_t src)
{
    emit_alu_r32_r32(buf, 0x39, dst, src);
}

/* --- CMP r/m32, imm32  (81 /7) : 6 bytes ------------------------------- */
static void emit_cmp_r32_imm32(CodeBuffer *buf, uint8_t rd, int32_t imm)
{
    emit_byte(buf, 0x81);
    emit_byte(buf, (uint8_t)(0xC0 | (7 << 3) | rd));
    emit_byte(buf, (uint8_t)( imm        & 0xFF));
    emit_byte(buf, (uint8_t)((imm >>  8) & 0xFF));
    emit_byte(buf, (uint8_t)((imm >> 16) & 0xFF));
    emit_byte(buf, (uint8_t)((imm >> 24) & 0xFF));
}

/* --- PUSH r32  (50+rd) : 1 byte ---------------------------------------- */
static void emit_push_r32(CodeBuffer *buf, uint8_t rd)
{
    emit_byte(buf, (uint8_t)(0x50 + rd));
}

/* --- POP r32  (58+rd) : 1 byte ----------------------------------------- */
static void emit_pop_r32(CodeBuffer *buf, uint8_t rd)
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
 *  Symbol table for labels
 * ========================================================================= */
#define X32_MAX_SYMBOLS  256
#define X32_MAX_FIXUPS   256

typedef struct {
    char name[UA_MAX_LABEL_LEN];
    int  address;
} X32Symbol;

typedef struct {
    char  label[UA_MAX_LABEL_LEN];
    int   patch_offset;
    int   instr_end;
    int   line;
} X32Fixup;

typedef struct {
    X32Symbol symbols[X32_MAX_SYMBOLS];
    int       sym_count;
    X32Fixup  fixups[X32_MAX_FIXUPS];
    int       fix_count;
} X32SymTab;

static void x32_symtab_init(X32SymTab *st)
{
    st->sym_count = 0;
    st->fix_count = 0;
}

static void x32_symtab_add(X32SymTab *st, const char *name, int address)
{
    if (st->sym_count >= X32_MAX_SYMBOLS) {
        fprintf(stderr, "x86-32: symbol table overflow\n");
        exit(1);
    }
    strncpy(st->symbols[st->sym_count].name, name, UA_MAX_LABEL_LEN - 1);
    st->symbols[st->sym_count].name[UA_MAX_LABEL_LEN - 1] = '\0';
    st->symbols[st->sym_count].address = address;
    st->sym_count++;
}

static int x32_symtab_lookup(const X32SymTab *st, const char *name)
{
    for (int i = 0; i < st->sym_count; i++) {
        if (strcmp(st->symbols[i].name, name) == 0)
            return st->symbols[i].address;
    }
    return -1;
}

static void x32_add_fixup(X32SymTab *st, const char *label,
                           int patch_offset, int instr_end, int line)
{
    if (st->fix_count >= X32_MAX_FIXUPS) {
        fprintf(stderr, "x86-32: fixup table overflow\n");
        exit(1);
    }
    X32Fixup *f = &st->fixups[st->fix_count++];
    strncpy(f->label, label, UA_MAX_LABEL_LEN - 1);
    f->label[UA_MAX_LABEL_LEN - 1] = '\0';
    f->patch_offset = patch_offset;
    f->instr_end    = instr_end;
    f->line         = line;
}

/* =========================================================================
 *  instruction_size_x32()  —  compute byte size of each instruction
 *
 *  x86-32 encodings are shorter than x86-64 (no REX prefix).
 * ========================================================================= */
static int instruction_size_x32(const Instruction *inst)
{
    if (inst->is_label) return 0;

    switch (inst->opcode) {
        case OP_LDI:    return 5;   /* MOV r32, imm32  (B8+rd id) */
        case OP_MOV:    return 2;   /* MOV r32, r32 */
        case OP_LOAD: {
            int rs = inst->operands[1].data.reg;
            if (rs == 4) return 3;      /* ESP needs SIB */
            if (rs == 5) return 3;      /* EBP needs disp8=0 */
            return 2;
        }
        case OP_STORE: {
            int rd = inst->operands[0].data.reg;
            if (rd == 4) return 3;
            if (rd == 5) return 3;
            return 2;
        }
        case OP_ADD:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 2 : 7;
            /* reg:reg=2, reg:imm = MOV scratch,imm(5) + ADD(2) = 7 */
        case OP_SUB:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 2 : 7;
        case OP_AND:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 2 : 7;
        case OP_OR:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 2 : 7;
        case OP_XOR:
            return (inst->operands[1].type == OPERAND_REGISTER) ? 2 : 7;
        case OP_NOT:    return 2;
        case OP_INC:    return 1;   /* 40+rd (single-byte in 32-bit mode) */
        case OP_DEC:    return 1;   /* 48+rd */
        case OP_MUL:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 3;  /* IMUL r32,r32 */
            else
                return 8;  /* MOV scratch,imm(5) + IMUL(3) */
        case OP_DIV:
            /* PUSH EDX(1) + MOV EAX,Rd(2) + CDQ(1) + IDIV Rs(2)
             * + MOV Rd,EAX(2) + POP EDX(1) = 9 for reg */
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 9;
            else
                return 14; /* MOV scratch,imm(5) + 9 for division */
        case OP_SHL:
            return (inst->operands[1].type == OPERAND_IMMEDIATE) ? 3 : 9;
            /* imm: C1 /4 ib = 3 bytes
             * reg: PUSH ECX(1) + MOV ECX,Rs(2) + SHL Rd,CL(2) + POP ECX(1) = 6
             * padded to 9 for safety */
        case OP_SHR:
            return (inst->operands[1].type == OPERAND_IMMEDIATE) ? 3 : 9;
        case OP_CMP:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 2;
            else
                return 6;  /* CMP r/m32, imm32 */
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
        case OP_VAR:    return 0;   /* declaration only */
        case OP_SET:
            /* SET name, Rs  ->  MOV [disp32], r32  (6 bytes)
             * SET name, imm ->  MOV [disp32], imm32 (10 bytes) */
            if (inst->operands[1].type == OPERAND_REGISTER) return 6;
            return 10;
        case OP_GET:
            /* GET Rd, name  ->  MOV r32, [disp32]  (6 bytes) */
            return 6;

        /* ---- New Phase-8 instructions --------------------------------- */
        case OP_LDS:    return 6;   /* LEA r32, [disp32]  (8D ModRM + disp32) */
        case OP_LOADB:
            /* MOVZX r32, byte [r32]  (0F B6 ModRM = 3 bytes, +1 for ESP/EBP) */
            { int rs = inst->operands[1].data.reg;
              return (rs == 4 || rs == 5) ? 4 : 3; }
        case OP_STOREB:
            /* MOV byte [r32], r8  (88 ModRM = 2 bytes, +1 for ESP/EBP) */
            { int rd = inst->operands[1].data.reg;
              return (rd == 4 || rd == 5) ? 3 : 2; }
        case OP_SYS:    return 2;   /* INT 0x80  (CD 80) */
        default:        return 0;
    }
}

/* =========================================================================
 *  Variable table for x86-32
 * ========================================================================= */
#define X32_MAX_VARS   256
#define X32_VAR_SIZE   4      /* bytes per variable (dword) */

typedef struct {
    char    name[UA_MAX_LABEL_LEN];
    int32_t init_value;
    int     has_init;
} X32VarEntry;

typedef struct {
    X32VarEntry vars[X32_MAX_VARS];
    int         count;
} X32VarTable;

static void x32_vartab_init(X32VarTable *vt) { vt->count = 0; }

/* =========================================================================
 *  String table for x86-32  — collects LDS string literals
 *  String data is appended after variable data in the output.
 * ========================================================================= */
#define X32_MAX_STRINGS 256

typedef struct {
    const char *text;
    int         offset;     /* byte offset within string data section */
    int         length;     /* strlen (without null terminator)       */
} X32StringEntry;

typedef struct {
    X32StringEntry strings[X32_MAX_STRINGS];
    int            count;
    int            total_size;  /* sum of (length+1) for each string */
} X32StringTable;

static void x32_strtab_init(X32StringTable *st) {
    st->count = 0;
    st->total_size = 0;
}

static int x32_strtab_add(X32StringTable *st, const char *text) {
    /* De-duplicate */
    for (int i = 0; i < st->count; i++)
        if (strcmp(st->strings[i].text, text) == 0) return i;
    if (st->count >= X32_MAX_STRINGS) {
        fprintf(stderr, "x86-32: string table overflow (max %d)\n",
                X32_MAX_STRINGS);
        return 0;
    }
    int idx = st->count++;
    int len = (int)strlen(text);
    st->strings[idx].text   = text;
    st->strings[idx].offset = st->total_size;
    st->strings[idx].length = len;
    st->total_size += len + 1;  /* +1 for null terminator */
    return idx;
}

static int x32_vartab_add(X32VarTable *vt, const char *name,
                          int32_t init_value, int has_init)
{
    for (int i = 0; i < vt->count; i++) {
        if (strcmp(vt->vars[i].name, name) == 0) {
            fprintf(stderr, "x86-32: duplicate variable '%s'\n", name);
            return -1;
        }
    }
    if (vt->count >= X32_MAX_VARS) {
        fprintf(stderr, "x86-32: variable table overflow (max %d)\n",
                X32_MAX_VARS);
        return -1;
    }
    X32VarEntry *v = &vt->vars[vt->count++];
    strncpy(v->name, name, UA_MAX_LABEL_LEN - 1);
    v->name[UA_MAX_LABEL_LEN - 1] = '\0';
    v->init_value = init_value;
    v->has_init   = has_init;
    return 0;
}

/* =========================================================================
 *  generate_x86_32()  —  main entry point  (two-pass)
 * ========================================================================= */
CodeBuffer* generate_x86_32(const Instruction *ir, int ir_count)
{
    fprintf(stderr, "[x86-32] Generating code for %d IR instructions ...\n",
            ir_count);

    /* --- Pass 1: collect label addresses + variable declarations ------- */
    X32SymTab symtab;
    x32_symtab_init(&symtab);

    X32VarTable vartab;
    x32_vartab_init(&vartab);

    X32StringTable strtab;
    x32_strtab_init(&strtab);

    int pc = 0;
    for (int i = 0; i < ir_count; i++) {
        const Instruction *inst = &ir[i];
        if (inst->is_label) {
            x32_symtab_add(&symtab, inst->label_name, pc);
        } else if (inst->opcode == OP_VAR) {
            const char *vname = inst->operands[0].data.label;
            int32_t init_val  = 0;
            int     has_init  = 0;
            if (inst->operand_count >= 2 &&
                inst->operands[1].type == OPERAND_IMMEDIATE) {
                init_val = (int32_t)inst->operands[1].data.imm;
                has_init = 1;
            }
            x32_vartab_add(&vartab, vname, init_val, has_init);
        } else {
            /* Collect LDS string literals */
            if (inst->opcode == OP_LDS)
                x32_strtab_add(&strtab, inst->operands[1].data.string);
            pc += instruction_size_x32(inst);
        }
    }

    /* Register variable symbols: each at code_end + index * 4 */
    int var_base = pc;
    for (int v = 0; v < vartab.count; v++) {
        x32_symtab_add(&symtab, vartab.vars[v].name,
                        var_base + v * X32_VAR_SIZE);
    }
    int str_base = var_base + vartab.count * X32_VAR_SIZE;

    /* --- Pass 2: code emission ----------------------------------------- */
    CodeBuffer *code = create_code_buffer();
    if (!code) {
        fprintf(stderr, "UA x86-32: out of memory\n");
        return NULL;
    }

    for (int i = 0; i < ir_count; i++) {
        const Instruction *inst = &ir[i];

        if (inst->is_label)
            continue;

        switch (inst->opcode) {

        /* ---- LDI Rd, #imm  ->  MOV r32, imm32 ------------ 5 bytes --- */
        case OP_LDI: {
            int rd = inst->operands[0].data.reg;
            int32_t imm = (int32_t)inst->operands[1].data.imm;
            x32_validate_register(inst, rd);
            uint8_t enc = X32_REG_ENC[rd];
            fprintf(stderr, "  LDI R%d -> MOV %s, %d\n",
                    rd, X32_REG_NAME[rd], imm);
            emit_mov_r32_imm32(code, enc, imm);
            break;
        }

        /* ---- MOV Rd, Rs  ->  MOV r32, r32 ----------------- 2 bytes -- */
        case OP_MOV: {
            int rd = inst->operands[0].data.reg;
            int rs = inst->operands[1].data.reg;
            x32_validate_register(inst, rd);
            x32_validate_register(inst, rs);
            uint8_t enc_d = X32_REG_ENC[rd];
            uint8_t enc_s = X32_REG_ENC[rs];
            fprintf(stderr, "  MOV R%d, R%d -> MOV %s, %s\n",
                    rd, rs, X32_REG_NAME[rd], X32_REG_NAME[rs]);
            emit_mov_r32_r32(code, enc_d, enc_s);
            break;
        }

        /* ---- LOAD Rd, Rs  ->  MOV r32, [r32] -------------- 2-3 bytes  */
        case OP_LOAD: {
            int rd = inst->operands[0].data.reg;
            int rs = inst->operands[1].data.reg;
            x32_validate_register(inst, rd);
            x32_validate_register(inst, rs);
            fprintf(stderr, "  LOAD R%d, R%d -> MOV %s, [%s]\n",
                    rd, rs, X32_REG_NAME[rd], X32_REG_NAME[rs]);
            emit_load_r32_mem(code, X32_REG_ENC[rd], X32_REG_ENC[rs]);
            break;
        }

        /* ---- STORE Rx, Ry  ->  MOV [Rx], Ry --------------- 2-3 bytes  */
        case OP_STORE: {
            int rx = inst->operands[0].data.reg;
            int ry = inst->operands[1].data.reg;
            x32_validate_register(inst, rx);
            x32_validate_register(inst, ry);
            fprintf(stderr, "  STORE R%d, R%d -> MOV [%s], %s\n",
                    rx, ry, X32_REG_NAME[rx], X32_REG_NAME[ry]);
            emit_store_mem_r32(code, X32_REG_ENC[rx], X32_REG_ENC[ry]);
            break;
        }

        /* ---- ADD Rd, Rs/imm -------------------------------- 2/7 bytes  */
        case OP_ADD: {
            int rd = inst->operands[0].data.reg;
            x32_validate_register(inst, rd);
            uint8_t enc_d = X32_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                x32_validate_register(inst, rs);
                fprintf(stderr, "  ADD R%d, R%d -> ADD %s, %s\n",
                        rd, rs, X32_REG_NAME[rd], X32_REG_NAME[rs]);
                emit_add_r32_r32(code, enc_d, X32_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t scratch = (enc_d == 1) ? 2 : 1;
                fprintf(stderr, "  ADD R%d, #%d -> MOV scratch, %d; ADD %s, scratch\n",
                        rd, imm, imm, X32_REG_NAME[rd]);
                emit_mov_r32_imm32(code, scratch, imm);
                emit_add_r32_r32(code, enc_d, scratch);
            }
            break;
        }

        /* ---- SUB Rd, Rs/imm -------------------------------- 2/7 bytes  */
        case OP_SUB: {
            int rd = inst->operands[0].data.reg;
            x32_validate_register(inst, rd);
            uint8_t enc_d = X32_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                x32_validate_register(inst, rs);
                fprintf(stderr, "  SUB R%d, R%d -> SUB %s, %s\n",
                        rd, rs, X32_REG_NAME[rd], X32_REG_NAME[rs]);
                emit_sub_r32_r32(code, enc_d, X32_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t scratch = (enc_d == 1) ? 2 : 1;
                fprintf(stderr, "  SUB R%d, #%d -> MOV scratch, %d; SUB %s, scratch\n",
                        rd, imm, imm, X32_REG_NAME[rd]);
                emit_mov_r32_imm32(code, scratch, imm);
                emit_sub_r32_r32(code, enc_d, scratch);
            }
            break;
        }

        /* ---- AND Rd, Rs/imm -------------------------------- 2/7 bytes  */
        case OP_AND: {
            int rd = inst->operands[0].data.reg;
            x32_validate_register(inst, rd);
            uint8_t enc_d = X32_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                x32_validate_register(inst, rs);
                fprintf(stderr, "  AND R%d, R%d -> AND %s, %s\n",
                        rd, rs, X32_REG_NAME[rd], X32_REG_NAME[rs]);
                emit_and_r32_r32(code, enc_d, X32_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t scratch = (enc_d == 1) ? 2 : 1;
                fprintf(stderr, "  AND R%d, #%d\n", rd, imm);
                emit_mov_r32_imm32(code, scratch, imm);
                emit_and_r32_r32(code, enc_d, scratch);
            }
            break;
        }

        /* ---- OR Rd, Rs/imm --------------------------------- 2/7 bytes  */
        case OP_OR: {
            int rd = inst->operands[0].data.reg;
            x32_validate_register(inst, rd);
            uint8_t enc_d = X32_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                x32_validate_register(inst, rs);
                fprintf(stderr, "  OR  R%d, R%d -> OR %s, %s\n",
                        rd, rs, X32_REG_NAME[rd], X32_REG_NAME[rs]);
                emit_or_r32_r32(code, enc_d, X32_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t scratch = (enc_d == 1) ? 2 : 1;
                fprintf(stderr, "  OR  R%d, #%d\n", rd, imm);
                emit_mov_r32_imm32(code, scratch, imm);
                emit_or_r32_r32(code, enc_d, scratch);
            }
            break;
        }

        /* ---- XOR Rd, Rs/imm -------------------------------- 2/7 bytes  */
        case OP_XOR: {
            int rd = inst->operands[0].data.reg;
            x32_validate_register(inst, rd);
            uint8_t enc_d = X32_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                x32_validate_register(inst, rs);
                fprintf(stderr, "  XOR R%d, R%d -> XOR %s, %s\n",
                        rd, rs, X32_REG_NAME[rd], X32_REG_NAME[rs]);
                emit_xor_r32_r32(code, enc_d, X32_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t scratch = (enc_d == 1) ? 2 : 1;
                fprintf(stderr, "  XOR R%d, #%d\n", rd, imm);
                emit_mov_r32_imm32(code, scratch, imm);
                emit_xor_r32_r32(code, enc_d, scratch);
            }
            break;
        }

        /* ---- NOT Rd  ->  NOT r32 --------------------------- 2 bytes -- */
        case OP_NOT: {
            int rd = inst->operands[0].data.reg;
            x32_validate_register(inst, rd);
            fprintf(stderr, "  NOT R%d -> NOT %s\n", rd, X32_REG_NAME[rd]);
            emit_not_r32(code, X32_REG_ENC[rd]);
            break;
        }

        /* ---- INC Rd  ->  INC r32 --------------------------- 1 byte -- */
        case OP_INC: {
            int rd = inst->operands[0].data.reg;
            x32_validate_register(inst, rd);
            fprintf(stderr, "  INC R%d -> INC %s\n", rd, X32_REG_NAME[rd]);
            emit_inc_r32(code, X32_REG_ENC[rd]);
            break;
        }

        /* ---- DEC Rd  ->  DEC r32 --------------------------- 1 byte -- */
        case OP_DEC: {
            int rd = inst->operands[0].data.reg;
            x32_validate_register(inst, rd);
            fprintf(stderr, "  DEC R%d -> DEC %s\n", rd, X32_REG_NAME[rd]);
            emit_dec_r32(code, X32_REG_ENC[rd]);
            break;
        }

        /* ---- MUL Rd, Rs/imm  ->  IMUL r32, r32 ------- 3/8 bytes ----- */
        case OP_MUL: {
            int rd = inst->operands[0].data.reg;
            x32_validate_register(inst, rd);
            uint8_t enc_d = X32_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                x32_validate_register(inst, rs);
                fprintf(stderr, "  MUL R%d, R%d -> IMUL %s, %s\n",
                        rd, rs, X32_REG_NAME[rd], X32_REG_NAME[rs]);
                emit_imul_r32_r32(code, enc_d, X32_REG_ENC[rs]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t scratch = (enc_d == 1) ? 2 : 1;
                fprintf(stderr, "  MUL R%d, #%d -> MOV scratch, %d; IMUL %s, scratch\n",
                        rd, imm, imm, X32_REG_NAME[rd]);
                emit_mov_r32_imm32(code, scratch, imm);
                emit_imul_r32_r32(code, enc_d, scratch);
            }
            break;
        }

        /* ---- DIV Rd, Rs/imm  ->  PUSH EDX; MOV EAX,Rd; CDQ;
         *       IDIV Rs; MOV Rd,EAX; POP EDX
         *       Register variant: 9 bytes.  Imm: 14 bytes.
         * --------------------------------------------------------------- */
        case OP_DIV: {
            int rd = inst->operands[0].data.reg;
            x32_validate_register(inst, rd);
            uint8_t enc_d = X32_REG_ENC[rd];

            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                x32_validate_register(inst, rs);
                uint8_t enc_s = X32_REG_ENC[rs];
                fprintf(stderr, "  DIV R%d, R%d -> IDIV\n", rd, rs);
                emit_push_r32(code, 2);            /* PUSH EDX      1 */
                emit_mov_r32_r32(code, 0, enc_d);  /* MOV EAX, Rd   2 */
                emit_cdq(code);                    /* CDQ            1 */
                emit_idiv_r32(code, enc_s);        /* IDIV Rs        2 */
                emit_mov_r32_r32(code, enc_d, 0);  /* MOV Rd, EAX   2 */
                emit_pop_r32(code, 2);             /* POP EDX       1 */
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t scratch = 1; /* ECX */
                if (enc_d == 1) scratch = 3; /* EBX if Rd=ECX */
                fprintf(stderr, "  DIV R%d, #%d -> MOV scratch, %d; IDIV\n",
                        rd, imm, imm);
                emit_push_r32(code, 2);                /* PUSH EDX   1 */
                emit_mov_r32_imm32(code, scratch, imm); /* MOV scr,imm 5 */
                emit_mov_r32_r32(code, 0, enc_d);      /* MOV EAX,Rd 2 */
                emit_cdq(code);                        /* CDQ         1 */
                emit_idiv_r32(code, scratch);          /* IDIV scr    2 */
                emit_mov_r32_r32(code, enc_d, 0);      /* MOV Rd,EAX 2 */
                emit_pop_r32(code, 2);                 /* POP EDX    1 */
            }
            break;
        }

        /* ---- SHL Rd, Rs/imm -------------------------------- 3/9 bytes  */
        case OP_SHL: {
            int rd = inst->operands[0].data.reg;
            x32_validate_register(inst, rd);
            uint8_t enc_d = X32_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_IMMEDIATE) {
                uint8_t imm = (uint8_t)(inst->operands[1].data.imm & 0x1F);
                fprintf(stderr, "  SHL R%d, #%d\n", rd, imm);
                emit_shl_r32_imm8(code, enc_d, imm);
            } else {
                int rs = inst->operands[1].data.reg;
                x32_validate_register(inst, rs);
                uint8_t enc_s = X32_REG_ENC[rs];
                fprintf(stderr, "  SHL R%d, R%d -> SHL %s, CL\n",
                        rd, rs, X32_REG_NAME[rd]);
                emit_push_r32(code, 1);            /* PUSH ECX       1 */
                emit_mov_r32_r32(code, 1, enc_s);  /* MOV ECX, Rs    2 */
                emit_shl_r32_cl(code, enc_d);      /* SHL Rd, CL     2 */
                emit_pop_r32(code, 1);             /* POP ECX        1 */
                /* pad to 9 bytes: emit 3 NOPs (6 emitted above) */
                for (int p = 0; p < 3; p++) emit_nop(code);
            }
            break;
        }

        /* ---- SHR Rd, Rs/imm -------------------------------- 3/9 bytes  */
        case OP_SHR: {
            int rd = inst->operands[0].data.reg;
            x32_validate_register(inst, rd);
            uint8_t enc_d = X32_REG_ENC[rd];
            if (inst->operands[1].type == OPERAND_IMMEDIATE) {
                uint8_t imm = (uint8_t)(inst->operands[1].data.imm & 0x1F);
                fprintf(stderr, "  SHR R%d, #%d\n", rd, imm);
                emit_shr_r32_imm8(code, enc_d, imm);
            } else {
                int rs = inst->operands[1].data.reg;
                x32_validate_register(inst, rs);
                uint8_t enc_s = X32_REG_ENC[rs];
                fprintf(stderr, "  SHR R%d, R%d -> SHR %s, CL\n",
                        rd, rs, X32_REG_NAME[rd]);
                emit_push_r32(code, 1);
                emit_mov_r32_r32(code, 1, enc_s);
                emit_shr_r32_cl(code, enc_d);
                emit_pop_r32(code, 1);
                for (int p = 0; p < 3; p++) emit_nop(code);
            }
            break;
        }

        /* ---- CMP Ra, Rb/imm -------------------------------- 2/6 bytes  */
        case OP_CMP: {
            int ra = inst->operands[0].data.reg;
            x32_validate_register(inst, ra);
            uint8_t enc_a = X32_REG_ENC[ra];
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rb = inst->operands[1].data.reg;
                x32_validate_register(inst, rb);
                fprintf(stderr, "  CMP R%d, R%d -> CMP %s, %s\n",
                        ra, rb, X32_REG_NAME[ra], X32_REG_NAME[rb]);
                emit_cmp_r32_r32(code, enc_a, X32_REG_ENC[rb]);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                fprintf(stderr, "  CMP R%d, #%d\n", ra, imm);
                emit_cmp_r32_imm32(code, enc_a, imm);
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
            x32_add_fixup(&symtab, label, patch_off, code->size, inst->line);
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
            x32_add_fixup(&symtab, label, patch_off, code->size, inst->line);
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
            x32_add_fixup(&symtab, label, patch_off, code->size, inst->line);
            break;
        }

        /* ---- CALL label  ->  CALL rel32 -------------------- 5 bytes -- */
        case OP_CALL: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  CALL %s\n", label);
            emit_byte(code, 0xE8);
            int patch_off = code->size;
            emit_rel32_placeholder(code);
            x32_add_fixup(&symtab, label, patch_off, code->size, inst->line);
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
            x32_validate_register(inst, rs);
            fprintf(stderr, "  PUSH R%d -> PUSH %s\n", rs, X32_REG_NAME[rs]);
            emit_push_r32(code, X32_REG_ENC[rs]);
            break;
        }

        /* ---- POP Rd ----------------------------------------- 1 byte -- */
        case OP_POP: {
            int rd = inst->operands[0].data.reg;
            x32_validate_register(inst, rd);
            fprintf(stderr, "  POP  R%d -> POP %s\n", rd, X32_REG_NAME[rd]);
            emit_pop_r32(code, X32_REG_ENC[rd]);
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

        /* ---- VAR — declaration only, no code emitted ----------------- */
        case OP_VAR:
            break;

        /* ---- SET name, Rs/imm → MOV [disp32], r32/imm32 -------------- */
        case OP_SET: {
            const char *vname = inst->operands[0].data.label;
            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                x32_validate_register(inst, rs);
                uint8_t enc = X32_REG_ENC[rs];
                fprintf(stderr, "  SET %s, R%d -> MOV [disp32], %s\n",
                        vname, rs, X32_REG_NAME[rs]);
                emit_byte(code, 0x89);  /* MOV r/m32, r32 */
                emit_byte(code, (uint8_t)((enc << 3) | 0x05));  /* ModRM: [disp32] */
                int patch_off = code->size;
                emit_rel32_placeholder(code);  /* disp32 placeholder */
                /* For absolute addressing, instr_end=0 so patch = target */
                x32_add_fixup(&symtab, vname, patch_off, 0, inst->line);
            } else {
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                fprintf(stderr, "  SET %s, #%d -> MOV [disp32], imm32\n",
                        vname, imm);
                emit_byte(code, 0xC7);  /* MOV r/m32, imm32 */
                emit_byte(code, 0x05);  /* ModRM: [disp32], reg=000 */
                int patch_off = code->size;
                emit_rel32_placeholder(code);
                emit_byte(code, (uint8_t)( imm        & 0xFF));
                emit_byte(code, (uint8_t)((imm >>  8) & 0xFF));
                emit_byte(code, (uint8_t)((imm >> 16) & 0xFF));
                emit_byte(code, (uint8_t)((imm >> 24) & 0xFF));
                x32_add_fixup(&symtab, vname, patch_off, 0, inst->line);
            }
            break;
        }

        /* ---- GET Rd, name → MOV r32, [disp32] ------------------------- */
        case OP_GET: {
            int rd = inst->operands[0].data.reg;
            const char *vname = inst->operands[1].data.label;
            x32_validate_register(inst, rd);
            uint8_t enc = X32_REG_ENC[rd];
            fprintf(stderr, "  GET R%d, %s -> MOV %s, [disp32]\n",
                    rd, vname, X32_REG_NAME[rd]);
            emit_byte(code, 0x8B);  /* MOV r32, r/m32 */
            emit_byte(code, (uint8_t)((enc << 3) | 0x05));  /* ModRM: [disp32] */
            int patch_off = code->size;
            emit_rel32_placeholder(code);
            x32_add_fixup(&symtab, vname, patch_off, 0, inst->line);
            break;
        }

        /* ---- LDS Rd, "str"  ->  LEA r32, [disp32] -------- 6 bytes ---- */
        case OP_LDS: {
            int rd = inst->operands[0].data.reg;
            const char *str = inst->operands[1].data.string;
            x32_validate_register(inst, rd);
            uint8_t enc = X32_REG_ENC[rd];
            fprintf(stderr, "  LDS R%d, \"%s\" -> LEA %s, [disp32]\n",
                    rd, str, X32_REG_NAME[rd]);
            emit_byte(code, 0x8D);  /* LEA r32, [disp32] */
            emit_byte(code, (uint8_t)((enc << 3) | 0x05));  /* ModRM: [disp32] */
            int str_idx = x32_strtab_add(&strtab, str);
            int str_addr = str_base + strtab.strings[str_idx].offset;
            int patch_off = code->size;
            emit_rel32_placeholder(code);
            /* Absolute fixup (instr_end=0), patched to str_addr */
            patch_rel32(code, patch_off, (int32_t)str_addr);
            break;
        }

        /* ---- LOADB Rd, Rs  ->  MOVZX r32, byte [r32] ---- 3-4 bytes -- */
        case OP_LOADB: {
            int rd = inst->operands[0].data.reg;
            int rs = inst->operands[1].data.reg;
            x32_validate_register(inst, rd);
            x32_validate_register(inst, rs);
            uint8_t enc_d = X32_REG_ENC[rd];
            uint8_t enc_s = X32_REG_ENC[rs];
            fprintf(stderr, "  LOADB R%d, R%d -> MOVZX %s, byte [%s]\n",
                    rd, rs, X32_REG_NAME[rd], X32_REG_NAME[rs]);
            emit_byte(code, 0x0F);
            emit_byte(code, 0xB6);
            if (enc_s == 5) {
                emit_byte(code, (uint8_t)(0x40 | (enc_d << 3) | enc_s));
                emit_byte(code, 0x00);
            } else if (enc_s == 4) {
                emit_byte(code, (uint8_t)(0x00 | (enc_d << 3) | 0x04));
                emit_byte(code, 0x24);
            } else {
                emit_byte(code, (uint8_t)(0x00 | (enc_d << 3) | enc_s));
            }
            break;
        }

        /* ---- STOREB Rs, Rd  ->  MOV byte [Rd], Rs_low8 --- 2-3 bytes -- */
        case OP_STOREB: {
            int rx = inst->operands[0].data.reg;
            int ry = inst->operands[1].data.reg;
            x32_validate_register(inst, rx);
            x32_validate_register(inst, ry);
            uint8_t enc_x = X32_REG_ENC[rx];
            uint8_t enc_y = X32_REG_ENC[ry];
            fprintf(stderr, "  STOREB R%d, R%d -> MOV byte [%s], %s_low8\n",
                    rx, ry, X32_REG_NAME[rx], X32_REG_NAME[ry]);
            emit_byte(code, 0x88);
            if (enc_x == 5) {
                emit_byte(code, (uint8_t)(0x40 | (enc_y << 3) | enc_x));
                emit_byte(code, 0x00);
            } else if (enc_x == 4) {
                emit_byte(code, (uint8_t)(0x00 | (enc_y << 3) | 0x04));
                emit_byte(code, 0x24);
            } else {
                emit_byte(code, (uint8_t)(0x00 | (enc_y << 3) | enc_x));
            }
            break;
        }

        /* ---- SYS  ->  INT 0x80 ---------------------------- 2 bytes --- */
        case OP_SYS:
            fprintf(stderr, "  SYS -> INT 0x80\n");
            emit_byte(code, 0xCD);
            emit_byte(code, 0x80);
            break;

        default: {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "opcode '%s' is not supported by the x86-32 backend",
                     opcode_name(inst->opcode));
            x32_error(inst, msg);
            break;
        }
        }
    }

    /* --- Pass 3: patch relocations ------------------------------------- */
    for (int f = 0; f < symtab.fix_count; f++) {
        X32Fixup *fix = &symtab.fixups[f];
        int target = x32_symtab_lookup(&symtab, fix->label);
        if (target < 0) {
            fprintf(stderr, "x86-32: undefined label or variable '%s' "
                    "(line %d)\n", fix->label, fix->line);
            free_code_buffer(code);
            return NULL;
        }
        int32_t rel = (int32_t)(target - fix->instr_end);
        patch_rel32(code, fix->patch_offset, rel);
    }

    /* --- Append variable data section ---------------------------------- */
    for (int v = 0; v < vartab.count; v++) {
        int32_t val = vartab.vars[v].has_init ? vartab.vars[v].init_value : 0;
        emit_byte(code, (uint8_t)( val        & 0xFF));
        emit_byte(code, (uint8_t)((val >>  8) & 0xFF));
        emit_byte(code, (uint8_t)((val >> 16) & 0xFF));
        emit_byte(code, (uint8_t)((val >> 24) & 0xFF));
    }

    /* --- Append string data section ------------------------------------ */
    for (int s = 0; s < strtab.count; s++) {
        const char *p = strtab.strings[s].text;
        int len = strtab.strings[s].length;
        for (int b = 0; b < len; b++)
            emit_byte(code, (uint8_t)p[b]);
        emit_byte(code, 0x00);  /* null terminator */
    }

    fprintf(stderr, "[x86-32] Emitted %d bytes (%d code + %d var + %d str)\n",
            code->size, var_base, vartab.count * X32_VAR_SIZE,
            strtab.total_size);
    return code;
}
