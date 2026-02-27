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
 *  Win32 target flag  (set by generate_x86_64 when sys="win32")
 *
 *  When targeting Windows, SYS emits CALL to a write dispatcher stub
 *  and HLT emits CALL to an exit dispatcher stub (both appended after
 *  the code/var/string data).  The stubs use the Windows x64 calling
 *  convention and call through an Import Address Table (IAT).
 * ========================================================================= */
static int g_win32 = 0;

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
        case OP_JL:     return 6;   /* 0F 8C rel32 */
        case OP_JG:     return 6;   /* 0F 8F rel32 */
        case OP_CALL:   return 5;   /* E8 rel32 */
        case OP_RET:    return 1;
        case OP_PUSH:   return 1;
        case OP_POP:    return 1;
        case OP_NOP:    return 1;
        case OP_HLT:    return g_win32 ? 5 : 1;   /* win32: CALL exit_stub; else RET */
        case OP_INT:    return 2;   /* CD ib */

        /* ---- Variable pseudo-instructions ----------------------------- */
        case OP_VAR:    return 0;   /* declaration only, no code emitted  */
        case OP_BUFFER: return 0;   /* declaration only, no code emitted  */
        case OP_SET:
            /* SET name, Rs  →  MOV [RIP+disp32], r64  (7 bytes)
             * SET name, imm →  MOV qword [RIP+disp32], imm32 (11 bytes) */
            if (inst->operands[1].type == OPERAND_REGISTER) return 7;
            return 11;
        case OP_GET:
            /* GET Rd, name  →  MOV r64, [RIP+disp32]  (7 bytes) */
            return 7;

        /* ---- Phase 8: String / Byte / Syscall -------------------------- */
        case OP_LDS:    return 7;   /* LEA r64, [RIP+disp32]  (7 bytes) */
        case OP_LOADB: {
            /* MOVZX r64, byte [r64]  : REX.W 0F B6 ModRM  = 4 bytes */
            int rs = inst->operands[1].data.reg;
            if (rs == 4) return 5;  /* RSP needs SIB */
            if (rs == 5) return 5;  /* RBP needs disp8=0 */
            return 4;
        }
        case OP_STOREB: {
            /* MOV byte [r64], r8  : 88 ModRM  = 2 bytes (or 3 with REX) */
            int rd = inst->operands[0].data.reg;
            if (rd == 4) return 3;
            if (rd == 5) return 3;
            return 2;
        }
        case OP_SYS:    return g_win32 ? 5 : 2;   /* win32: CALL write_stub; else syscall */

        /* ---- Architecture-specific opcodes (x86-64) -------------------- */
        case OP_CPUID:  return 2;   /* 0F A2 */
        case OP_RDTSC:  return 2;   /* 0F 31 */
        case OP_BSWAP:  return 3;   /* REX.W 0F C8+rd */

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
 *
 *  String literals from LDS are stored after the variable data section.
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
 *  Buffer table — BUFFER name, size  (contiguous byte allocations)
 * ========================================================================= */
#define X64_MAX_BUFFERS  256

typedef struct {
    char name[UA_MAX_LABEL_LEN];
    int  size;   /* byte count */
} X64BufEntry;

typedef struct {
    X64BufEntry bufs[X64_MAX_BUFFERS];
    int         count;
    int         total_size;  /* sum of all buffer sizes */
} X64BufTable;

static void x64_buftab_init(X64BufTable *bt) {
    bt->count = 0;
    bt->total_size = 0;
}

static int x64_buftab_add(X64BufTable *bt, const char *name, int size) {
    for (int i = 0; i < bt->count; i++) {
        if (strcmp(bt->bufs[i].name, name) == 0) {
            fprintf(stderr, "x86-64: duplicate buffer '%s'\n", name);
            return -1;
        }
    }
    if (bt->count >= X64_MAX_BUFFERS) {
        fprintf(stderr, "x86-64: buffer table overflow (max %d)\n",
                X64_MAX_BUFFERS);
        return -1;
    }
    X64BufEntry *b = &bt->bufs[bt->count++];
    strncpy(b->name, name, UA_MAX_LABEL_LEN - 1);
    b->name[UA_MAX_LABEL_LEN - 1] = '\0';
    b->size = size;
    bt->total_size += size;
    return 0;
}

static int x64_buftab_has(const X64BufTable *bt, const char *name) {
    for (int i = 0; i < bt->count; i++) {
        if (strcmp(bt->bufs[i].name, name) == 0) return 1;
    }
    return 0;
}

/* =========================================================================
 *  String table — stores LDS string literals in the data section
 * ========================================================================= */
#define X64_MAX_STRINGS  256

typedef struct {
    char    text[UA_MAX_LABEL_LEN];
    int     offset;     /* byte offset within string data section */
    int     length;     /* length of string (excluding null terminator) */
} X64StringEntry;

typedef struct {
    X64StringEntry strings[X64_MAX_STRINGS];
    int            count;
    int            total_size;   /* total bytes used (with null terminators) */
} X64StringTable;

static void x64_strtab_init(X64StringTable *st)
{
    st->count      = 0;
    st->total_size = 0;
}

/* Add a string literal; returns the index.  De-duplicates identical strings. */
static int x64_strtab_add(X64StringTable *st, const char *text)
{
    /* Check for duplicate */
    for (int i = 0; i < st->count; i++) {
        if (strcmp(st->strings[i].text, text) == 0)
            return i;
    }
    if (st->count >= X64_MAX_STRINGS) {
        fprintf(stderr, "x86-64: string table overflow (max %d)\n",
                X64_MAX_STRINGS);
        return -1;
    }
    X64StringEntry *e = &st->strings[st->count];
    strncpy(e->text, text, UA_MAX_LABEL_LEN - 1);
    e->text[UA_MAX_LABEL_LEN - 1] = '\0';
    e->length = (int)strlen(text);
    e->offset = st->total_size;
    st->total_size += e->length + 1;   /* +1 for null terminator */
    return st->count++;
}

/* =========================================================================
 *  generate_x86_64()  —  main entry point  (two-pass)
 * ========================================================================= */
CodeBuffer* generate_x86_64(const Instruction *ir, int ir_count,
                             const char *sys)
{
    /* Set win32 flag for instruction sizing and code generation */
    g_win32 = (sys != NULL && (strcmp(sys, "win32") == 0 ||
                               strcmp(sys, "Win32") == 0 ||
                               strcmp(sys, "WIN32") == 0));

    fprintf(stderr, "[x86-64] Generating code for %d IR instructions%s ...\n",
            ir_count, g_win32 ? " (Win32 target)" : "");

    /* --- Pass 1: collect label addresses + variable declarations ------- */
    X64SymTab symtab;
    x64_symtab_init(&symtab);

    X64VarTable vartab;
    x64_vartab_init(&vartab);

    X64StringTable strtab;
    x64_strtab_init(&strtab);

    X64BufTable buftab;
    x64_buftab_init(&buftab);

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
        } else if (inst->opcode == OP_BUFFER) {
            /* Collect buffer declaration — no code emitted */
            const char *bname = inst->operands[0].data.label;
            int bsize = (int)inst->operands[1].data.imm;
            x64_buftab_add(&buftab, bname, bsize);
        } else if (inst->opcode == OP_LDS) {
            /* Collect string literal */
            x64_strtab_add(&strtab, inst->operands[1].data.string);
            pc += instruction_size_x64(inst);
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

    /* Register buffer symbols: each lives after variables */
    int buf_base = var_base + vartab.count * X64_VAR_SIZE;
    {
        int buf_offset = 0;
        for (int b = 0; b < buftab.count; b++) {
            x64_symtab_add(&symtab, buftab.bufs[b].name,
                            buf_base + buf_offset);
            buf_offset += buftab.bufs[b].size;
        }
    }

    /* String data lives after variables and buffers */
    int str_base = buf_base + buftab.total_size;

    /* --- Win32 runtime stub addresses (computed for pass 2 CALL targets) */
    /* Layout after string data:
     *   [syscall_dispatcher  6 bytes]  (cmp rax,0; je read)
     *   [write_dispatcher   84 bytes]
     *   [read_dispatcher    84 bytes]
     *   [exit_dispatcher    16 bytes]
     *   [stdout_handle       8 bytes]
     *   [stdin_handle        8 bytes]
     *   [written_var         8 bytes]
     *   [read_var            8 bytes]
     *   [IAT: 5 × 8        40 bytes]  (GetStdHandle, WriteFile, ReadFile,
     *                                   ExitProcess, null)
     */
    #define W32_DISPATCH_SIZE    6   /* syscall dispatcher (cmp+je)     */
    #define W32_WRITE_STUB_SIZE  84
    #define W32_READ_STUB_SIZE   84
    #define W32_EXIT_STUB_SIZE   16
    #define W32_DATA_SIZE        32  /* stdout(8)+stdin(8)+written(8)+read(8) */
    #define W32_IAT_SIZE         40  /* 5 entries × 8 bytes */
    int stub_base  = str_base + strtab.total_size;  /* start of syscall_dispatcher */
    int exit_base  = stub_base + W32_DISPATCH_SIZE + W32_WRITE_STUB_SIZE
                   + W32_READ_STUB_SIZE;
    int iat_offset = exit_base + W32_EXIT_STUB_SIZE + W32_DATA_SIZE;
    (void)iat_offset;  /* recorded later as code->pe_iat_offset */

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

        /* ---- JL label  ->  JL rel32 (0F 8C) --------------- 6 bytes -- */
        case OP_JL: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  JL  %s\n", label);
            emit_byte(code, 0x0F);
            emit_byte(code, 0x8C);
            int patch_off = code->size;
            emit_rel32_placeholder(code);
            x64_add_fixup(&symtab, label, patch_off, code->size, inst->line);
            break;
        }

        /* ---- JG label  ->  JG rel32 (0F 8F) --------------- 6 bytes -- */
        case OP_JG: {
            const char *label = inst->operands[0].data.label;
            fprintf(stderr, "  JG  %s\n", label);
            emit_byte(code, 0x0F);
            emit_byte(code, 0x8F);
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

        /* ---- HLT ------------------------------------------------ */
        case OP_HLT:
            if (g_win32) {
                /* CALL rel32 → exit_dispatcher */
                int32_t rel = (int32_t)(exit_base - (code->size + 5));
                fprintf(stderr, "  HLT -> CALL exit_dispatcher\n");
                emit_byte(code, 0xE8);
                emit_byte(code, (uint8_t)( rel        & 0xFF));
                emit_byte(code, (uint8_t)((rel >>  8) & 0xFF));
                emit_byte(code, (uint8_t)((rel >> 16) & 0xFF));
                emit_byte(code, (uint8_t)((rel >> 24) & 0xFF));
            } else {
                fprintf(stderr, "  HLT -> RET\n");
                emit_ret(code);
            }
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

        /* ---- BUFFER — declaration only, no code emitted --------------- */
        case OP_BUFFER:
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

        /* ---- GET Rd, name  →  MOV r64, [RIP+disp32] or LEA (buffer) -- */
        case OP_GET: {
            int rd = inst->operands[0].data.reg;
            const char *vname = inst->operands[1].data.label;
            x64_validate_register(inst, rd);
            int is_buf = x64_buftab_has(&buftab, vname);
            if (is_buf) {
                fprintf(stderr, "  GET R%d, %s -> LEA r64, [RIP+disp32] (buffer address)\n",
                        rd, vname);
                emit_byte(code, (uint8_t)(0x48 | ((rd >= 8) ? 0x04 : 0x00)));
                emit_byte(code, 0x8D);  /* LEA r64, [RIP+disp32] */
                emit_byte(code, (uint8_t)(((rd & 7) << 3) | 0x05));
            } else {
                fprintf(stderr, "  GET R%d, %s -> MOV r64, [RIP+disp32]\n",
                        rd, vname);
                /* REX.W prefix (+ REX.R if reg >= 8) */
                emit_byte(code, (uint8_t)(0x48 | ((rd >= 8) ? 0x04 : 0x00)));
                emit_byte(code, 0x8B);  /* MOV r64, r/m64 */
                emit_byte(code, (uint8_t)(((rd & 7) << 3) | 0x05));  /* ModRM */
            }
            int patch_off = code->size;
            emit_rel32_placeholder(code);
            x64_add_fixup(&symtab, vname, patch_off, code->size,
                          inst->line);
            break;
        }

        /* ---- LDS Rd, "str"  →  LEA r64, [RIP+disp32] ------- 7 bytes - */
        case OP_LDS: {
            int rd = inst->operands[0].data.reg;
            const char *str = inst->operands[1].data.string;
            x64_validate_register(inst, rd);
            uint8_t enc = X64_REG_ENC[rd];
            fprintf(stderr, "  LDS R%d, \"%s\" -> LEA %s, [RIP+disp32]\n",
                    rd, str, X64_REG_NAME[rd]);
            /* LEA r64, [RIP+disp32] : REX.W 8D ModRM(reg, [RIP+disp32]) */
            emit_byte(code, 0x48);  /* REX.W */
            emit_byte(code, 0x8D);  /* LEA */
            emit_byte(code, (uint8_t)((enc << 3) | 0x05));  /* ModRM: RIP-rel */
            /* Find string index and compute address */
            int str_idx = x64_strtab_add(&strtab, str);
            int str_addr = str_base + strtab.strings[str_idx].offset;
            int patch_off = code->size;
            emit_rel32_placeholder(code);
            x64_add_fixup(&symtab, "__str__", patch_off, code->size,
                          inst->line);
            /* Direct patch — we know the address already */
            {
                int32_t rel = (int32_t)(str_addr - code->size);
                patch_rel32(code, patch_off, rel);
            }
            /* Remove the fixup we just added (it's already resolved) */
            symtab.fix_count--;
            break;
        }

        /* ---- LOADB Rd, Rs  →  MOVZX r64, byte [r64] ---- 4-5 bytes --- */
        case OP_LOADB: {
            int rd = inst->operands[0].data.reg;
            int rs = inst->operands[1].data.reg;
            x64_validate_register(inst, rd);
            x64_validate_register(inst, rs);
            uint8_t enc_d = X64_REG_ENC[rd];
            uint8_t enc_s = X64_REG_ENC[rs];
            fprintf(stderr, "  LOADB R%d, R%d -> MOVZX %s, byte [%s]\n",
                    rd, rs, X64_REG_NAME[rd], X64_REG_NAME[rs]);
            /* REX.W 0F B6 ModRM */
            emit_byte(code, 0x48);
            emit_byte(code, 0x0F);
            emit_byte(code, 0xB6);
            if (enc_s == 5) {
                /* RBP needs mod=01 + disp8=0 */
                emit_byte(code, (uint8_t)(0x40 | (enc_d << 3) | enc_s));
                emit_byte(code, 0x00);
            } else if (enc_s == 4) {
                /* RSP needs SIB */
                emit_byte(code, (uint8_t)(0x00 | (enc_d << 3) | 0x04));
                emit_byte(code, 0x24);
            } else {
                emit_byte(code, (uint8_t)(0x00 | (enc_d << 3) | enc_s));
            }
            break;
        }

        /* ---- STOREB Rs, Rd  →  MOV byte [Rd], Rs_low8 ---- 2-3 bytes - */
        case OP_STOREB: {
            int rs = inst->operands[0].data.reg;  /* value register */
            int rd = inst->operands[1].data.reg;  /* address register */
            x64_validate_register(inst, rs);
            x64_validate_register(inst, rd);
            uint8_t enc_s = X64_REG_ENC[rs];
            uint8_t enc_d = X64_REG_ENC[rd];
            fprintf(stderr, "  STOREB R%d, R%d -> MOV byte [%s], %s_low8\n",
                    rs, rd, X64_REG_NAME[rd], X64_REG_NAME[rs]);
            /* 88 ModRM (MOV r/m8, r8): reg=source, rm=address */
            emit_byte(code, 0x88);
            if (enc_d == 5) {
                /* RBP base needs mod=01 + disp8=0 */
                emit_byte(code, (uint8_t)(0x40 | (enc_s << 3) | enc_d));
                emit_byte(code, 0x00);
            } else if (enc_d == 4) {
                /* RSP base needs SIB byte */
                emit_byte(code, (uint8_t)(0x00 | (enc_s << 3) | 0x04));
                emit_byte(code, 0x24);
            } else {
                emit_byte(code, (uint8_t)(0x00 | (enc_s << 3) | enc_d));
            }
            break;
        }

        /* ---- SYS ------------------------------------------------ */
        case OP_SYS:
            if (g_win32) {
                /* CALL rel32 → syscall_dispatcher */
                int32_t rel = (int32_t)(stub_base - (code->size + 5));
                fprintf(stderr, "  SYS -> CALL syscall_dispatcher\n");
                emit_byte(code, 0xE8);
                emit_byte(code, (uint8_t)( rel        & 0xFF));
                emit_byte(code, (uint8_t)((rel >>  8) & 0xFF));
                emit_byte(code, (uint8_t)((rel >> 16) & 0xFF));
                emit_byte(code, (uint8_t)((rel >> 24) & 0xFF));
            } else {
                fprintf(stderr, "  SYS -> SYSCALL\n");
                emit_byte(code, 0x0F);
                emit_byte(code, 0x05);
            }
            break;

        /* ---- CPUID ----------------------------------------- 2 bytes --- */
        case OP_CPUID:
            fprintf(stderr, "  CPUID\n");
            emit_byte(code, 0x0F);
            emit_byte(code, 0xA2);
            break;

        /* ---- RDTSC ----------------------------------------- 2 bytes --- */
        case OP_RDTSC:
            fprintf(stderr, "  RDTSC\n");
            emit_byte(code, 0x0F);
            emit_byte(code, 0x31);
            break;

        /* ---- BSWAP Rd ------------------------------------- 3 bytes --- */
        case OP_BSWAP: {
            int rd = inst->operands[0].data.reg;
            uint8_t enc = X64_REG_ENC[rd];
            fprintf(stderr, "  BSWAP %s\n", X64_REG_NAME[rd]);
            /* REX.W prefix for 64-bit operand */
            emit_byte(code, 0x48);
            emit_byte(code, 0x0F);
            emit_byte(code, (uint8_t)(0xC8 + enc));
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

    /* --- Append buffer data section (zero-initialised) ----------------- */
    for (int b = 0; b < buftab.count; b++) {
        for (int i = 0; i < buftab.bufs[b].size; i++) {
            emit_byte(code, 0x00);
        }
    }

    /* --- Append string data section -------------------------------------- */
    for (int s = 0; s < strtab.count; s++) {
        const char *p = strtab.strings[s].text;
        int len = strtab.strings[s].length;
        for (int b = 0; b < len; b++)
            emit_byte(code, (uint8_t)p[b]);
        emit_byte(code, 0x00);  /* null terminator */
    }

    /* --- Append Win32 runtime (dispatcher stubs + IAT) ----------------- */
    if (g_win32) {
        /* RIP-relative offsets within each stub are constants derived from
         * the fixed layout of the entire runtime block.
         *
         * All offsets below are relative to the start of this runtime area
         * (i.e. the first byte of syscall_dispatcher = "byte 0"):
         *
         *   Byte 0..5:     syscall_dispatcher  (6 bytes)
         *   Byte 6..89:    write_dispatcher    (84 bytes)
         *   Byte 90..173:  read_dispatcher     (84 bytes)
         *   Byte 174..189: exit_dispatcher     (16 bytes)
         *   Byte 190..197: stdout_handle       (8 bytes)
         *   Byte 198..205: stdin_handle        (8 bytes)
         *   Byte 206..213: written_var         (8 bytes)
         *   Byte 214..221: read_var            (8 bytes)
         *   Byte 222..229: IAT[0] GetStdHandle
         *   Byte 230..237: IAT[1] WriteFile
         *   Byte 238..245: IAT[2] ReadFile
         *   Byte 246..253: IAT[3] ExitProcess
         *   Byte 254..261: IAT[4] null terminator
         *
         * RIP-relative disp32 = target - instruction_end
         */

        /* ---- syscall_dispatcher (6 bytes) ---- */
        /* Checks RAX: 0 = read, nonzero = write (fall-through).
         *   cmp rax, 0    (4 bytes)
         *   je +84        (2 bytes) -> read_dispatcher at byte 90 */
        static const uint8_t syscall_dispatcher[6] = {
            0x48, 0x83, 0xF8, 0x00,              /* cmp rax, 0              */
            0x74, 0x54                            /* je +84 → read_dispatcher */
        };
        for (int b = 0; b < 6; b++)
            emit_byte(code, syscall_dispatcher[b]);

        /* ---- write_dispatcher (84 bytes, starts at runtime byte 6) ---- */
        /* Translates Linux write-syscall ABI (RSI=buf, RDX=count) to
         * WriteFile(hStdout, buf, count, &written, NULL) via Win32 ABI.
         *
         * RIP-relative targets for write_dispatcher (abs = byte within runtime):
         *   stdout_handle  @ 190:  end@6+21=27,  disp=190-27=163 (0xA3)
         *   IAT[0]         @ 222:  end@6+46=52,  disp=222-52=170 (0xAA)
         *   stdout_handle  @ 190:  end@6+57=63,  disp=190-63=127 (0x7F)
         *   IAT[1]         @ 230:  end@6+82=88,  disp=230-88=142 (0x8E) */
        static const uint8_t write_dispatcher[84] = {
            /* 0  */ 0x55,                               /* push rbp              */
            /* 1  */ 0x48, 0x89, 0xE5,                   /* mov rbp, rsp          */
            /* 4  */ 0x48, 0x83, 0xEC, 0x40,             /* sub rsp, 64           */
            /* 8  */ 0x49, 0x89, 0xD0,                   /* mov r8, rdx  (count)  */
            /* 11 */ 0x48, 0x89, 0xF2,                   /* mov rdx, rsi (buffer) */
            /* 14 */ 0x48, 0x8B, 0x0D, 0xA3,0x00,0x00,0x00,  /* mov rcx,[rip+163] → stdout_handle */
            /* 21 */ 0x48, 0x85, 0xC9,                   /* test rcx, rcx         */
            /* 24 */ 0x75, 0x25,                         /* jnz +37 → byte 63    */
            /* --- GetStdHandle(-11) path --- */
            /* 26 */ 0x41, 0x50,                         /* push r8               */
            /* 28 */ 0x52,                               /* push rdx              */
            /* 29 */ 0x48, 0xC7, 0xC1, 0xF5,0xFF,0xFF,0xFF,  /* mov rcx, -11 (STD_OUTPUT) */
            /* 36 */ 0x48, 0x83, 0xEC, 0x20,             /* sub rsp, 32 (shadow)  */
            /* 40 */ 0xFF, 0x15, 0xAA,0x00,0x00,0x00,    /* call [rip+170] → IAT[0] GetStdHandle */
            /* 46 */ 0x48, 0x83, 0xC4, 0x20,             /* add rsp, 32           */
            /* 50 */ 0x48, 0x89, 0x05, 0x7F,0x00,0x00,0x00,  /* mov [rip+127],rax → stdout_handle */
            /* 57 */ 0x48, 0x89, 0xC1,                   /* mov rcx, rax          */
            /* 60 */ 0x5A,                               /* pop rdx               */
            /* 61 */ 0x41, 0x58,                         /* pop r8                */
            /* --- have_handle: --- */
            /* 63 */ 0x4C, 0x8D, 0x4D, 0xF8,            /* lea r9, [rbp-8]       */
            /* 67 */ 0x48,0xC7,0x44,0x24,0x20, 0x00,0x00,0x00,0x00,  /* mov qword [rsp+32], 0 */
            /* 76 */ 0xFF, 0x15, 0x8E,0x00,0x00,0x00,    /* call [rip+142] → IAT[1] WriteFile */
            /* 82 */ 0xC9,                               /* leave                 */
            /* 83 */ 0xC3                                /* ret                   */
        };
        for (int b = 0; b < 84; b++)
            emit_byte(code, write_dispatcher[b]);

        /* ---- read_dispatcher (84 bytes, starts at runtime byte 90) ---- */
        /* Translates Linux read-syscall ABI (RSI=buf, RDX=count) to
         * ReadFile(hStdin, buf, count, &read_var, NULL) via Win32 ABI.
         *
         * RIP-relative targets for read_dispatcher:
         *   stdin_handle   @ 198:  end@90+21=111, disp=198-111=87  (0x57)
         *   IAT[0]         @ 222:  end@90+46=136, disp=222-136=86  (0x56)
         *   stdin_handle   @ 198:  end@90+57=147, disp=198-147=51  (0x33)
         *   IAT[2]         @ 238:  end@90+82=172, disp=238-172=66  (0x42) */
        static const uint8_t read_dispatcher[84] = {
            /* 0  */ 0x55,                               /* push rbp              */
            /* 1  */ 0x48, 0x89, 0xE5,                   /* mov rbp, rsp          */
            /* 4  */ 0x48, 0x83, 0xEC, 0x40,             /* sub rsp, 64           */
            /* 8  */ 0x49, 0x89, 0xD0,                   /* mov r8, rdx  (count)  */
            /* 11 */ 0x48, 0x89, 0xF2,                   /* mov rdx, rsi (buffer) */
            /* 14 */ 0x48, 0x8B, 0x0D, 0x57,0x00,0x00,0x00,  /* mov rcx,[rip+87] → stdin_handle */
            /* 21 */ 0x48, 0x85, 0xC9,                   /* test rcx, rcx         */
            /* 24 */ 0x75, 0x25,                         /* jnz +37 → byte 63    */
            /* --- GetStdHandle(-10) path --- */
            /* 26 */ 0x41, 0x50,                         /* push r8               */
            /* 28 */ 0x52,                               /* push rdx              */
            /* 29 */ 0x48, 0xC7, 0xC1, 0xF6,0xFF,0xFF,0xFF,  /* mov rcx, -10 (STD_INPUT) */
            /* 36 */ 0x48, 0x83, 0xEC, 0x20,             /* sub rsp, 32 (shadow)  */
            /* 40 */ 0xFF, 0x15, 0x56,0x00,0x00,0x00,    /* call [rip+86] → IAT[0] GetStdHandle */
            /* 46 */ 0x48, 0x83, 0xC4, 0x20,             /* add rsp, 32           */
            /* 50 */ 0x48, 0x89, 0x05, 0x33,0x00,0x00,0x00,  /* mov [rip+51],rax → stdin_handle */
            /* 57 */ 0x48, 0x89, 0xC1,                   /* mov rcx, rax          */
            /* 60 */ 0x5A,                               /* pop rdx               */
            /* 61 */ 0x41, 0x58,                         /* pop r8                */
            /* --- have_handle: --- */
            /* 63 */ 0x4C, 0x8D, 0x4D, 0xF8,            /* lea r9, [rbp-8]       */
            /* 67 */ 0x48,0xC7,0x44,0x24,0x20, 0x00,0x00,0x00,0x00,  /* mov qword [rsp+32], 0 */
            /* 76 */ 0xFF, 0x15, 0x42,0x00,0x00,0x00,    /* call [rip+66] → IAT[2] ReadFile */
            /* 82 */ 0xC9,                               /* leave                 */
            /* 83 */ 0xC3                                /* ret                   */
        };
        for (int b = 0; b < 84; b++)
            emit_byte(code, read_dispatcher[b]);

        /* exit_dispatcher (16 bytes, starts at runtime byte 174):
         *   Aligns stack, then calls ExitProcess(0) via IAT[3].
         *   end@174+16=190.  IAT[3] ExitProcess @ 246.  246-190=56 (0x38). */
        static const uint8_t exit_dispatcher[16] = {
            0x31, 0xC9,                                  /* xor ecx, ecx (exit 0)   */
            0x48, 0x83, 0xEC, 0x38,                      /* sub rsp, 56             */
            0x48, 0x83, 0xE4, 0xF0,                      /* and rsp, -16 (align)    */
            0xFF, 0x15, 0x38,0x00,0x00,0x00              /* call [rip+56] → IAT[3] ExitProcess */
        };
        for (int b = 0; b < 16; b++)
            emit_byte(code, exit_dispatcher[b]);

        /* stdout_handle  (8 bytes, init 0) */
        for (int b = 0; b < 8; b++) emit_byte(code, 0x00);

        /* stdin_handle   (8 bytes, init 0) */
        for (int b = 0; b < 8; b++) emit_byte(code, 0x00);

        /* written_var  (8 bytes, init 0) */
        for (int b = 0; b < 8; b++) emit_byte(code, 0x00);

        /* read_var  (8 bytes, init 0) */
        for (int b = 0; b < 8; b++) emit_byte(code, 0x00);

        /* IAT: 5 entries × 8 bytes (filled by PE emitter on disk,
         *       patched by Windows loader at runtime) */
        code->pe_iat_offset = code->size;
        code->pe_iat_count  = 5;   /* GetStdHandle, WriteFile, ReadFile, ExitProcess, null */
        for (int b = 0; b < 40; b++) emit_byte(code, 0x00);

        fprintf(stderr, "[x86-64] Emitted %d bytes (%d code + %d var + %d buf + %d str"
                " + %d win32rt)\n",
                code->size, var_base, vartab.count * X64_VAR_SIZE,
                buftab.total_size, strtab.total_size,
                W32_DISPATCH_SIZE + W32_WRITE_STUB_SIZE + W32_READ_STUB_SIZE
                + W32_EXIT_STUB_SIZE + W32_DATA_SIZE + W32_IAT_SIZE);
    } else {
        fprintf(stderr, "[x86-64] Emitted %d bytes (%d code + %d var + %d buf + %d str)\n",
                code->size, var_base, vartab.count * X64_VAR_SIZE,
                buftab.total_size, strtab.total_size);
    }
    return code;
}
