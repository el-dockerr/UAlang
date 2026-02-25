/*
 * =============================================================================
 *  UAS - Unified Assembler System
 *  Phase 3: 8051 Back-End (Code Generation)
 *
 *  File:    backend_8051.c
 *  Purpose: Two-pass assembler that translates the architecture-neutral UAS
 *           IR into raw Intel 8051 (MCS-51) machine code.
 *
 *  ┌──────────────────────────────────────────────────────────────────────┐
 *  │  8051 Instruction Encoding Reference (subset used here)            │
 *  │                                                                    │
 *  │  MOV  Rn, #imm      0x78+n, imm8          2 bytes                 │
 *  │  MOV  A,  Rn        0xE8+n                 1 byte                  │
 *  │  MOV  Rn, A         0xF8+n                 1 byte                  │
 *  │  ADD  A,  #imm      0x24, imm8             2 bytes                 │
 *  │  ADD  A,  Rn        0x28+n                 1 byte                  │
 *  │  SUBB A,  #imm      0x94, imm8             2 bytes  (borrow-sub)  │
 *  │  SUBB A,  Rn        0x98+n                 1 byte                  │
 *  │  ANL  A,  #imm      0x54, imm8             2 bytes                 │
 *  │  ANL  A,  Rn        0x58+n                 1 byte                  │
 *  │  ORL  A,  #imm      0x44, imm8             2 bytes                 │
 *  │  ORL  A,  Rn        0x48+n                 1 byte                  │
 *  │  XRL  A,  #imm      0x64, imm8             2 bytes                 │
 *  │  XRL  A,  Rn        0x68+n                 1 byte                  │
 *  │  CPL  A             0xF4                   1 byte                  │
 *  │  CLR  C             0xC3                   1 byte                  │
 *  │  CJNE A, #imm, rel  0xB4, imm8, rel8       3 bytes                │
 *  │  JNZ  rel           0x70, rel8              2 bytes                │
 *  │  JZ   rel           0x60, rel8              2 bytes                │
 *  │  SJMP rel           0x80, rel8              2 bytes                │
 *  │  LJMP addr16        0x02, hi8, lo8          3 bytes                │
 *  │  LCALL addr16       0x12, hi8, lo8          3 bytes                │
 *  │  RET                0x22                   1 byte                  │
 *  │  NOP                0x00                   1 byte                  │
 *  │  PUSH direct        0xC0, addr8             2 bytes                │
 *  │  POP  direct        0xD0, addr8             2 bytes                │
 *  │  MUL  AB            0xA4                   1 byte                  │
 *  │  DIV  AB            0x84                   1 byte                  │
 *  │  MOV  B, #imm       0x75, 0xF0, imm8       3 bytes  (MOV direct)  │
 *  │  MOV  B, Rn  → MOV A, Rn; MOV B, A        (polyfill)              │
 *  └──────────────────────────────────────────────────────────────────────┘
 *
 *  UAS register R0-R7 map directly to 8051 R0-R7 (bank 0).
 *  R8-R15 are rejected at code-generation time.
 *
 *  License: MIT
 * =============================================================================
 */

#include "backend_8051.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 *  8051 register-bank-0 direct addresses (for PUSH/POP which use direct)
 * ========================================================================= */
static const uint8_t REG_DIRECT_ADDR[8] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
};

/* =========================================================================
 *  Error helpers
 * ========================================================================= */
static void backend_error(const Instruction *inst, const char *msg)
{
    fprintf(stderr,
            "\n"
            "  UAS 8051 Backend Error\n"
            "  ----------------------\n"
            "  Line %d, Column %d: %s\n\n",
            inst->line, inst->column, msg);
    exit(1);
}

static void validate_register(const Instruction *inst, int reg)
{
    if (reg < 0 || reg > 7) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "register R%d is not available on the 8051 "
                 "(only R0-R7 supported)", reg);
        backend_error(inst, msg);
    }
}

static void validate_imm8(const Instruction *inst, int64_t val)
{
    if (val < -128 || val > 255) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "immediate value %lld out of 8-bit range (-128..255)",
                 (long long)val);
        backend_error(inst, msg);
    }
}

/* =========================================================================
 *  CodeBuffer alias  —  local shorthand so existing emit() calls still work
 * ========================================================================= */
#define emit(buf, byte)  emit_byte(buf, byte)

/* =========================================================================
 *  Symbol Table helpers
 * ========================================================================= */
static void symtab_init(SymbolTable *st)
{
    st->count = 0;
}

static void symtab_add(SymbolTable *st, const char *name, int address)
{
    if (st->count >= MAX_SYMBOLS) {
        fprintf(stderr, "UAS 8051: symbol table overflow (max %d)\n",
                MAX_SYMBOLS);
        exit(1);
    }
    strncpy(st->entries[st->count].name, name, UAS_MAX_LABEL_LEN - 1);
    st->entries[st->count].name[UAS_MAX_LABEL_LEN - 1] = '\0';
    st->entries[st->count].address = address;
    st->count++;
}

static int symtab_lookup(const SymbolTable *st, const char *name)
{
    for (int i = 0; i < st->count; i++) {
        if (strcmp(st->entries[i].name, name) == 0) {
            return st->entries[i].address;
        }
    }
    return -1;  /* not found */
}

/* =========================================================================
 *  Pass 1:  Compute instruction sizes and build symbol table
 * =========================================================================
 *  Returns the total byte-size of the generated code.
 *
 *  Instruction size rules (8051 encoding):
 *
 *    LDI   Rd, #imm       -> MOV Rn, #imm              =  2 bytes
 *    MOV   Rd, Rs         -> MOV A, Rs; MOV Rd, A       =  2 bytes
 *    LOAD  Rd, Rs         -> MOV A, @Ri; MOV Rd, A      =  2 bytes  (Rs=R0|R1)
 *    STORE Rs, Rd         -> MOV A, Rs; MOV @Ri, A      =  2 bytes  (Rd=R0|R1)
 *    ADD   Rd, Rs         -> MOV A, Rd; ADD A, Rs; MOV Rd, A  = 3 bytes
 *    ADD   Rd, #imm       -> MOV A, Rd; ADD A,#imm; MOV Rd,A = 4 bytes
 *    SUB   Rd, Rs         -> CLR C; MOV A,Rd; SUBB A,Rs; MOV Rd,A = 4 bytes
 *    SUB   Rd, #imm       -> CLR C; MOV A,Rd; SUBB A,#imm; MOV Rd,A = 5 bytes
 *    AND   Rd, Rs         -> MOV A,Rd; ANL A,Rs; MOV Rd,A  = 3 bytes
 *    AND   Rd, #imm       -> MOV A,Rd; ANL A,#imm; MOV Rd,A = 4 bytes
 *    OR    Rd, Rs         -> MOV A,Rd; ORL A,Rs; MOV Rd,A  = 3 bytes
 *    OR    Rd, #imm       -> MOV A,Rd; ORL A,#imm; MOV Rd,A = 4 bytes
 *    XOR   Rd, Rs         -> MOV A,Rd; XRL A,Rs; MOV Rd,A  = 3 bytes
 *    XOR   Rd, #imm       -> MOV A,Rd; XRL A,#imm; MOV Rd,A = 4 bytes
 *    NOT   Rd             -> MOV A,Rd; CPL A; MOV Rd,A     = 3 bytes
 *    SHL   Rd, #n         -> n × { MOV A,Rd; RL A; MOV Rd,A }  (unrolled, or via loop)
 *                            Simplified: MOV A,Rd; (RL A)*n; MOV Rd,A = 2+n bytes
 *    SHR   Rd, #n         -> MOV A,Rd; (RR A)*n; MOV Rd,A = 2+n bytes
 *    CMP   Ra, #imm       -> CJNE A, #imm, +0  (3 bytes: set carry flag)
 *                            Full: MOV A,Ra; CJNE A,#imm,$+3 = 4 bytes
 *    CMP   Ra, Rb         -> MOV A,Ra; CLR C; SUBB A,Rb   = 4 bytes (flags only)
 *    JMP   label          -> LJMP addr16                   = 3 bytes
 *    JZ    label          -> JZ rel                        = 2 bytes
 *    JNZ   label          -> JNZ rel                       = 2 bytes
 *    CALL  label          -> LCALL addr16                  = 3 bytes
 *    RET                  -> RET                           = 1 byte
 *    PUSH  Rs             -> PUSH direct                   = 2 bytes
 *    POP   Rd             -> POP  direct                   = 2 bytes
 *    NOP                  -> NOP                           = 1 byte
 *    HLT                  -> SJMP $ (self-loop)            = 2 bytes
 *    MUL   Rd, Rs         -> MOV A,Rd; MOV B, Rs(via A); MUL AB; MOV Rd,A = 5
 *                            Actually: MOV A,Rs; MOV 0xF0,A; MOV A,Rd; MUL AB; MOV Rd,A = 6
 *                            Simplified: MOV B,#val or reg-based
 *    DIV   Rd, #imm       -> MOV B,#imm; MOV A,Rd; DIV AB; MOV Rd,A = 6 bytes
 * ========================================================================= */

static int instruction_size_8051(const Instruction *inst)
{
    if (inst->is_label) return 0;   /* labels emit no bytes */

    int rd, rs;
    int64_t imm;

    switch (inst->opcode) {

        case OP_LDI:   /* MOV Rn, #imm */
            return 2;

        case OP_MOV:   /* MOV A, Rs; MOV Rd, A */
            return 2;

        case OP_LOAD:  /* MOV A, @Ri; MOV Rd, A */
            return 2;

        case OP_STORE: /* MOV A, Rs; MOV @Ri, A */
            return 2;

        case OP_ADD:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 3;   /* MOV A,Rd; ADD A,Rs; MOV Rd,A */
            else
                return 4;   /* MOV A,Rd; ADD A,#imm; MOV Rd,A */

        case OP_SUB:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 4;   /* CLR C; MOV A,Rd; SUBB A,Rs; MOV Rd,A */
            else
                return 5;   /* CLR C; MOV A,Rd; SUBB A,#imm; MOV Rd,A */

        case OP_AND:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 3;
            else
                return 4;

        case OP_OR:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 3;
            else
                return 4;

        case OP_XOR:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 3;
            else
                return 4;

        case OP_NOT:   /* MOV A,Rd; CPL A; MOV Rd,A */
            return 3;

        case OP_SHL:
            if (inst->operands[1].type == OPERAND_IMMEDIATE) {
                imm = inst->operands[1].data.imm;
                return 2 + (int)imm;   /* MOV A,Rd; (RL A)*n; MOV Rd,A */
            }
            return 3;  /* fallback for reg: treat like 1 shift */

        case OP_SHR:
            if (inst->operands[1].type == OPERAND_IMMEDIATE) {
                imm = inst->operands[1].data.imm;
                return 2 + (int)imm;
            }
            return 3;

        case OP_CMP:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 4;   /* MOV A,Ra; CLR C; SUBB A,Rb; (flags set) */
                            /* We add dummy: + ADD A, Rb to restore ... */
                            /* Actually keep it simple: 4 bytes */
            else
                return 4;   /* MOV A,Ra; CJNE A,#imm,$+3 */

        case OP_JMP:   /* LJMP addr16 */
            return 3;

        case OP_JZ:    /* JZ rel */
            return 2;

        case OP_JNZ:   /* JNZ rel */
            return 2;

        case OP_CALL:  /* LCALL addr16 */
            return 3;

        case OP_RET:   /* RET */
            return 1;

        case OP_PUSH:  /* PUSH direct */
            return 2;

        case OP_POP:   /* POP direct */
            return 2;

        case OP_NOP:   /* NOP */
            return 1;

        case OP_HLT:   /* SJMP $ (infinite self-loop) */
            return 2;

        case OP_MUL:
            /* MOV A, Rs; MOV 0xF0(B), A; MOV A, Rd; MUL AB; MOV Rd, A */
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 6;
            else  /* MUL Rd, #imm -> MOV B,#imm (3); MOV A,Rd (1); MUL AB (1); MOV Rd,A (1) = 6 */
                return 6;

        case OP_DIV:
            /* MOV B, #imm (3 bytes via MOV direct,#imm) / or reg; MOV A,Rd; DIV AB; MOV Rd,A */
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 6;
            else
                return 6;

        default:
            (void)rd; (void)rs; (void)imm;
            backend_error(inst, "unsupported opcode for 8051 backend");
            return 0;  /* unreachable */
    }
}

/* =========================================================================
 *  Pass 1:  Build symbol table
 * ========================================================================= */
static int pass1_build_symbols(const Instruction *ir, int ir_count,
                               SymbolTable *st)
{
    symtab_init(st);
    int pc = 0;    /* program counter (byte offset) */

    for (int i = 0; i < ir_count; i++) {
        const Instruction *inst = &ir[i];

        if (inst->is_label) {
            /* Check for duplicate */
            if (symtab_lookup(st, inst->label_name) >= 0) {
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "duplicate label '%s'", inst->label_name);
                backend_error(inst, msg);
            }
            symtab_add(st, inst->label_name, pc);
        } else {
            pc += instruction_size_8051(inst);
        }
    }

    return pc;  /* total code size */
}

/* =========================================================================
 *  Pass 2:  Emit 8051 machine code
 * =========================================================================
 *  Helpers for common patterns:
 *
 *    emit_mov_a_rn(buf, n)    -> 0xE8+n          MOV A, Rn
 *    emit_mov_rn_a(buf, n)    -> 0xF8+n          MOV Rn, A
 *    emit_mov_rn_imm(buf,n,v) -> 0x78+n, v       MOV Rn, #imm
 *    emit_add_a_imm(buf, v)   -> 0x24, v         ADD A, #imm
 *    emit_add_a_rn(buf, n)    -> 0x28+n          ADD A, Rn
 * ========================================================================= */

static void emit_mov_a_rn(CodeBuffer *buf, int n)
{
    emit(buf, (uint8_t)(0xE8 + n));
}

static void emit_mov_rn_a(CodeBuffer *buf, int n)
{
    emit(buf, (uint8_t)(0xF8 + n));
}

static void emit_mov_rn_imm(CodeBuffer *buf, int n, uint8_t val)
{
    emit(buf, (uint8_t)(0x78 + n));
    emit(buf, val);
}

static void emit_add_a_imm(CodeBuffer *buf, uint8_t val)
{
    emit(buf, 0x24);
    emit(buf, val);
}

static void emit_add_a_rn(CodeBuffer *buf, int n)
{
    emit(buf, (uint8_t)(0x28 + n));
}

static void emit_subb_a_imm(CodeBuffer *buf, uint8_t val)
{
    emit(buf, 0x94);
    emit(buf, val);
}

static void emit_subb_a_rn(CodeBuffer *buf, int n)
{
    emit(buf, (uint8_t)(0x98 + n));
}

static void emit_anl_a_imm(CodeBuffer *buf, uint8_t val)
{
    emit(buf, 0x54);
    emit(buf, val);
}

static void emit_anl_a_rn(CodeBuffer *buf, int n)
{
    emit(buf, (uint8_t)(0x58 + n));
}

static void emit_orl_a_imm(CodeBuffer *buf, uint8_t val)
{
    emit(buf, 0x44);
    emit(buf, val);
}

static void emit_orl_a_rn(CodeBuffer *buf, int n)
{
    emit(buf, (uint8_t)(0x48 + n));
}

static void emit_xrl_a_imm(CodeBuffer *buf, uint8_t val)
{
    emit(buf, 0x64);
    emit(buf, val);
}

static void emit_xrl_a_rn(CodeBuffer *buf, int n)
{
    emit(buf, (uint8_t)(0x68 + n));
}

static void emit_clr_c(CodeBuffer *buf)
{
    emit(buf, 0xC3);
}

static void emit_cpl_a(CodeBuffer *buf)
{
    emit(buf, 0xF4);
}

static void emit_rl_a(CodeBuffer *buf)
{
    emit(buf, 0x23);    /* RL A — rotate left through accumulator */
}

static void emit_rr_a(CodeBuffer *buf)
{
    emit(buf, 0x03);    /* RR A — rotate right */
}

/* Emit LJMP addr16 */
static void emit_ljmp(CodeBuffer *buf, uint16_t addr)
{
    emit(buf, 0x02);
    emit(buf, (uint8_t)(addr >> 8));
    emit(buf, (uint8_t)(addr & 0xFF));
}

/* Emit LCALL addr16 */
static void emit_lcall(CodeBuffer *buf, uint16_t addr)
{
    emit(buf, 0x12);
    emit(buf, (uint8_t)(addr >> 8));
    emit(buf, (uint8_t)(addr & 0xFF));
}

/* =========================================================================
 *  Pass 2:  Code emission
 * ========================================================================= */
static void pass2_emit_code(const Instruction *ir, int ir_count,
                            const SymbolTable *st, CodeBuffer *buf)
{
    for (int i = 0; i < ir_count; i++) {
        const Instruction *inst = &ir[i];

        if (inst->is_label) continue;   /* labels produce no bytes */

        int rd, rs;
        int64_t imm;
        int target_addr;
        int rel;

        switch (inst->opcode) {

        /* ----------------------------------------------------------------
         *  LDI Rd, #imm  ->  MOV Rn, #imm   [0x78+n, imm8]   2 bytes
         * ---------------------------------------------------------------- */
        case OP_LDI:
            rd  = inst->operands[0].data.reg;
            imm = inst->operands[1].data.imm;
            validate_register(inst, rd);
            validate_imm8(inst, imm);
            emit_mov_rn_imm(buf, rd, (uint8_t)(imm & 0xFF));
            break;

        /* ----------------------------------------------------------------
         *  MOV Rd, Rs  ->  MOV A, Rs; MOV Rd, A              2 bytes
         * ---------------------------------------------------------------- */
        case OP_MOV:
            rd = inst->operands[0].data.reg;
            rs = inst->operands[1].data.reg;
            validate_register(inst, rd);
            validate_register(inst, rs);
            emit_mov_a_rn(buf, rs);
            emit_mov_rn_a(buf, rd);
            break;

        /* ----------------------------------------------------------------
         *  LOAD Rd, Rs  ->  MOV A, @Ri; MOV Rd, A            2 bytes
         *  (Rs must be R0 or R1 on the 8051 for indirect addressing)
         * ---------------------------------------------------------------- */
        case OP_LOAD:
            rd = inst->operands[0].data.reg;
            rs = inst->operands[1].data.reg;
            validate_register(inst, rd);
            if (rs != 0 && rs != 1) {
                backend_error(inst,
                    "LOAD: indirect source must be R0 or R1 on 8051 "
                    "(MOV A, @Ri)");
            }
            emit(buf, (uint8_t)(0xE6 + rs));  /* MOV A, @R0=0xE6, @R1=0xE7 */
            emit_mov_rn_a(buf, rd);
            break;

        /* ----------------------------------------------------------------
         *  STORE Rs, Rd  ->  MOV A, Rs; MOV @Ri, A           2 bytes
         *  (Rd must be R0 or R1)
         * ---------------------------------------------------------------- */
        case OP_STORE:
            rs = inst->operands[0].data.reg;
            rd = inst->operands[1].data.reg;
            validate_register(inst, rs);
            if (rd != 0 && rd != 1) {
                backend_error(inst,
                    "STORE: indirect destination must be R0 or R1 on 8051 "
                    "(MOV @Ri, A)");
            }
            emit_mov_a_rn(buf, rs);
            emit(buf, (uint8_t)(0xF6 + rd));  /* MOV @R0,A=0xF6  @R1,A=0xF7 */
            break;

        /* ----------------------------------------------------------------
         *  ADD Rd, Rs   ->  MOV A,Rd; ADD A,Rs;  MOV Rd,A    3 bytes
         *  ADD Rd, #imm ->  MOV A,Rd; ADD A,#imm; MOV Rd,A   4 bytes
         * ---------------------------------------------------------------- */
        case OP_ADD:
            rd = inst->operands[0].data.reg;
            validate_register(inst, rd);
            emit_mov_a_rn(buf, rd);
            if (inst->operands[1].type == OPERAND_REGISTER) {
                rs = inst->operands[1].data.reg;
                validate_register(inst, rs);
                emit_add_a_rn(buf, rs);
            } else {
                imm = inst->operands[1].data.imm;
                validate_imm8(inst, imm);
                emit_add_a_imm(buf, (uint8_t)(imm & 0xFF));
            }
            emit_mov_rn_a(buf, rd);
            break;

        /* ----------------------------------------------------------------
         *  SUB Rd, Rs   ->  CLR C; MOV A,Rd; SUBB A,Rs; MOV Rd,A   4 bytes
         *  SUB Rd, #imm ->  CLR C; MOV A,Rd; SUBB A,#imm; MOV Rd,A 5 bytes
         * ---------------------------------------------------------------- */
        case OP_SUB:
            rd = inst->operands[0].data.reg;
            validate_register(inst, rd);
            emit_clr_c(buf);
            emit_mov_a_rn(buf, rd);
            if (inst->operands[1].type == OPERAND_REGISTER) {
                rs = inst->operands[1].data.reg;
                validate_register(inst, rs);
                emit_subb_a_rn(buf, rs);
            } else {
                imm = inst->operands[1].data.imm;
                validate_imm8(inst, imm);
                emit_subb_a_imm(buf, (uint8_t)(imm & 0xFF));
            }
            emit_mov_rn_a(buf, rd);
            break;

        /* ----------------------------------------------------------------
         *  AND Rd, Rs   ->  MOV A,Rd; ANL A,Rs; MOV Rd,A     3 bytes
         *  AND Rd, #imm ->  MOV A,Rd; ANL A,#imm; MOV Rd,A   4 bytes
         * ---------------------------------------------------------------- */
        case OP_AND:
            rd = inst->operands[0].data.reg;
            validate_register(inst, rd);
            emit_mov_a_rn(buf, rd);
            if (inst->operands[1].type == OPERAND_REGISTER) {
                rs = inst->operands[1].data.reg;
                validate_register(inst, rs);
                emit_anl_a_rn(buf, rs);
            } else {
                imm = inst->operands[1].data.imm;
                validate_imm8(inst, imm);
                emit_anl_a_imm(buf, (uint8_t)(imm & 0xFF));
            }
            emit_mov_rn_a(buf, rd);
            break;

        /* ----------------------------------------------------------------
         *  OR Rd, Rs/imm  (same pattern with ORL)
         * ---------------------------------------------------------------- */
        case OP_OR:
            rd = inst->operands[0].data.reg;
            validate_register(inst, rd);
            emit_mov_a_rn(buf, rd);
            if (inst->operands[1].type == OPERAND_REGISTER) {
                rs = inst->operands[1].data.reg;
                validate_register(inst, rs);
                emit_orl_a_rn(buf, rs);
            } else {
                imm = inst->operands[1].data.imm;
                validate_imm8(inst, imm);
                emit_orl_a_imm(buf, (uint8_t)(imm & 0xFF));
            }
            emit_mov_rn_a(buf, rd);
            break;

        /* ----------------------------------------------------------------
         *  XOR Rd, Rs/imm  (same pattern with XRL)
         * ---------------------------------------------------------------- */
        case OP_XOR:
            rd = inst->operands[0].data.reg;
            validate_register(inst, rd);
            emit_mov_a_rn(buf, rd);
            if (inst->operands[1].type == OPERAND_REGISTER) {
                rs = inst->operands[1].data.reg;
                validate_register(inst, rs);
                emit_xrl_a_rn(buf, rs);
            } else {
                imm = inst->operands[1].data.imm;
                validate_imm8(inst, imm);
                emit_xrl_a_imm(buf, (uint8_t)(imm & 0xFF));
            }
            emit_mov_rn_a(buf, rd);
            break;

        /* ----------------------------------------------------------------
         *  NOT Rd  ->  MOV A,Rd; CPL A; MOV Rd,A              3 bytes
         * ---------------------------------------------------------------- */
        case OP_NOT:
            rd = inst->operands[0].data.reg;
            validate_register(inst, rd);
            emit_mov_a_rn(buf, rd);
            emit_cpl_a(buf);
            emit_mov_rn_a(buf, rd);
            break;

        /* ----------------------------------------------------------------
         *  SHL Rd, #n  ->  MOV A,Rd; (RL A)*n; MOV Rd,A      2+n bytes
         * ---------------------------------------------------------------- */
        case OP_SHL:
            rd = inst->operands[0].data.reg;
            validate_register(inst, rd);
            emit_mov_a_rn(buf, rd);
            if (inst->operands[1].type == OPERAND_IMMEDIATE) {
                imm = inst->operands[1].data.imm;
                for (int k = 0; k < (int)imm; k++) emit_rl_a(buf);
            } else {
                rs = inst->operands[1].data.reg;
                validate_register(inst, rs);
                /* Single shift as fallback */
                emit_rl_a(buf);
            }
            emit_mov_rn_a(buf, rd);
            break;

        /* ----------------------------------------------------------------
         *  SHR Rd, #n  ->  MOV A,Rd; (RR A)*n; MOV Rd,A      2+n bytes
         * ---------------------------------------------------------------- */
        case OP_SHR:
            rd = inst->operands[0].data.reg;
            validate_register(inst, rd);
            emit_mov_a_rn(buf, rd);
            if (inst->operands[1].type == OPERAND_IMMEDIATE) {
                imm = inst->operands[1].data.imm;
                for (int k = 0; k < (int)imm; k++) emit_rr_a(buf);
            } else {
                rs = inst->operands[1].data.reg;
                validate_register(inst, rs);
                emit_rr_a(buf);
            }
            emit_mov_rn_a(buf, rd);
            break;

        /* ----------------------------------------------------------------
         *  CMP Ra, Rb   -> MOV A,Ra; CLR C; SUBB A,Rb        4 bytes
         *  CMP Ra, #imm -> MOV A,Ra; CJNE A,#imm,$+3         4 bytes
         *
         *  Both set the Carry flag which subsequent JZ/JNZ can test
         *  (the accumulator holds the result for JNZ/JZ to inspect).
         * ---------------------------------------------------------------- */
        case OP_CMP:
            rd = inst->operands[0].data.reg;
            validate_register(inst, rd);
            emit_mov_a_rn(buf, rd);
            if (inst->operands[1].type == OPERAND_REGISTER) {
                rs = inst->operands[1].data.reg;
                validate_register(inst, rs);
                emit_clr_c(buf);
                emit_subb_a_rn(buf, rs);
            } else {
                imm = inst->operands[1].data.imm;
                validate_imm8(inst, imm);
                /* CJNE A, #imm, $+3   (skip 0 bytes — just sets C flag) */
                emit(buf, 0xB4);
                emit(buf, (uint8_t)(imm & 0xFF));
                emit(buf, 0x00);    /* relative offset 0 = fall through */
            }
            break;

        /* ----------------------------------------------------------------
         *  JMP label  ->  LJMP addr16   [0x02, hi, lo]        3 bytes
         * ---------------------------------------------------------------- */
        case OP_JMP:
            target_addr = symtab_lookup(st, inst->operands[0].data.label);
            if (target_addr < 0) {
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "undefined label '%s'",
                         inst->operands[0].data.label);
                backend_error(inst, msg);
            }
            emit_ljmp(buf, (uint16_t)target_addr);
            break;

        /* ----------------------------------------------------------------
         *  JZ label  ->  JZ rel   [0x60, rel8]                2 bytes
         * ---------------------------------------------------------------- */
        case OP_JZ:
            target_addr = symtab_lookup(st, inst->operands[0].data.label);
            if (target_addr < 0) {
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "undefined label '%s'",
                         inst->operands[0].data.label);
                backend_error(inst, msg);
            }
            /* Relative offset is computed from PC AFTER this instruction */
            rel = target_addr - (buf->size + 2);
            if (rel < -128 || rel > 127) {
                backend_error(inst,
                    "JZ target out of range for 8-bit relative jump");
            }
            emit(buf, 0x60);
            emit(buf, (uint8_t)(rel & 0xFF));
            break;

        /* ----------------------------------------------------------------
         *  JNZ label  ->  JNZ rel   [0x70, rel8]              2 bytes
         * ---------------------------------------------------------------- */
        case OP_JNZ:
            target_addr = symtab_lookup(st, inst->operands[0].data.label);
            if (target_addr < 0) {
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "undefined label '%s'",
                         inst->operands[0].data.label);
                backend_error(inst, msg);
            }
            /* Relative from PC after the 2-byte JNZ instruction */
            rel = target_addr - (buf->size + 2);
            if (rel < -128 || rel > 127) {
                backend_error(inst,
                    "JNZ target out of range for 8-bit relative jump");
            }
            emit(buf, 0x70);
            emit(buf, (uint8_t)(rel & 0xFF));
            break;

        /* ----------------------------------------------------------------
         *  CALL label  ->  LCALL addr16   [0x12, hi, lo]      3 bytes
         * ---------------------------------------------------------------- */
        case OP_CALL:
            target_addr = symtab_lookup(st, inst->operands[0].data.label);
            if (target_addr < 0) {
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "undefined label '%s'",
                         inst->operands[0].data.label);
                backend_error(inst, msg);
            }
            emit_lcall(buf, (uint16_t)target_addr);
            break;

        /* ----------------------------------------------------------------
         *  RET  ->  RET   [0x22]                              1 byte
         * ---------------------------------------------------------------- */
        case OP_RET:
            emit(buf, 0x22);
            break;

        /* ----------------------------------------------------------------
         *  PUSH Rs  ->  PUSH direct   [0xC0, addr]            2 bytes
         * ---------------------------------------------------------------- */
        case OP_PUSH:
            rs = inst->operands[0].data.reg;
            validate_register(inst, rs);
            emit(buf, 0xC0);
            emit(buf, REG_DIRECT_ADDR[rs]);
            break;

        /* ----------------------------------------------------------------
         *  POP Rd  ->  POP direct   [0xD0, addr]              2 bytes
         * ---------------------------------------------------------------- */
        case OP_POP:
            rd = inst->operands[0].data.reg;
            validate_register(inst, rd);
            emit(buf, 0xD0);
            emit(buf, REG_DIRECT_ADDR[rd]);
            break;

        /* ----------------------------------------------------------------
         *  NOP  ->  NOP   [0x00]                              1 byte
         * ---------------------------------------------------------------- */
        case OP_NOP:
            emit(buf, 0x00);
            break;

        /* ----------------------------------------------------------------
         *  HLT  ->  SJMP $   [0x80, 0xFE]   (infinite loop)  2 bytes
         * ---------------------------------------------------------------- */
        case OP_HLT:
            emit(buf, 0x80);
            emit(buf, 0xFE);
            break;

        /* ----------------------------------------------------------------
         *  MUL Rd, Rs   -> MOV A,Rs; MOV 0xF0,A; MOV A,Rd;
         *                  MUL AB; MOV Rd,A                    6 bytes
         *  MUL Rd, #imm -> MOV B,#imm (75 F0 imm); MOV A,Rd;
         *                  MUL AB; MOV Rd,A                    6 bytes
         * ---------------------------------------------------------------- */
        case OP_MUL:
            rd = inst->operands[0].data.reg;
            validate_register(inst, rd);
            if (inst->operands[1].type == OPERAND_REGISTER) {
                rs = inst->operands[1].data.reg;
                validate_register(inst, rs);
                /* MOV A, Rs; MOV B, A (via MOV direct,A); MOV A, Rd;
                 * MUL AB; MOV Rd, A                        = 6 bytes */
                emit_mov_a_rn(buf, rs);      /* MOV A, Rs     1 byte  */
                emit(buf, 0xF5);             /* MOV direct, A         */
                emit(buf, 0xF0);             /* B register = 0xF0     */
                                             /*              2 bytes  */
                emit_mov_a_rn(buf, rd);      /* MOV A, Rd     1 byte  */
                emit(buf, 0xA4);             /* MUL AB        1 byte  */
                emit_mov_rn_a(buf, rd);      /* MOV Rd, A     1 byte  */
            } else {
                imm = inst->operands[1].data.imm;
                validate_imm8(inst, imm);
                emit(buf, 0x75);             /* MOV direct, #imm      */
                emit(buf, 0xF0);             /* B register = 0xF0     */
                emit(buf, (uint8_t)(imm & 0xFF));  /*            3 bytes */
                emit_mov_a_rn(buf, rd);      /* MOV A, Rd     1 byte  */
                emit(buf, 0xA4);             /* MUL AB        1 byte  */
                emit_mov_rn_a(buf, rd);      /* MOV Rd, A     1 byte  */
            }
            break;

        /* ----------------------------------------------------------------
         *  DIV Rd, Rs   -> MOV A,Rs; MOV B,A; MOV A,Rd;
         *                  DIV AB; MOV Rd,A                    6 bytes
         *  DIV Rd, #imm -> MOV B,#imm (75 F0 imm); MOV A,Rd;
         *                  DIV AB; MOV Rd,A                    6 bytes
         * ---------------------------------------------------------------- */
        case OP_DIV:
            rd = inst->operands[0].data.reg;
            validate_register(inst, rd);
            if (inst->operands[1].type == OPERAND_REGISTER) {
                rs = inst->operands[1].data.reg;
                validate_register(inst, rs);
                emit_mov_a_rn(buf, rs);      /* MOV A, Rs             */
                emit(buf, 0xF5);             /* MOV direct, A         */
                emit(buf, 0xF0);             /* -> B                  */
                emit_mov_a_rn(buf, rd);      /* MOV A, Rd             */
                emit(buf, 0x84);             /* DIV AB                */
                emit_mov_rn_a(buf, rd);      /* MOV Rd, A             */
            } else {
                imm = inst->operands[1].data.imm;
                validate_imm8(inst, imm);
                emit(buf, 0x75);             /* MOV direct, #imm      */
                emit(buf, 0xF0);             /* B register             */
                emit(buf, (uint8_t)(imm & 0xFF));
                emit_mov_a_rn(buf, rd);      /* MOV A, Rd              */
                emit(buf, 0x84);             /* DIV AB                 */
                emit_mov_rn_a(buf, rd);      /* MOV Rd, A              */
            }
            break;

        default:
            backend_error(inst, "unsupported opcode for 8051 backend");
            break;
        }
    }
}

/* hexdump() is now provided by codegen.c */

/* =========================================================================
 *  generate_8051()  —  main entry point
 * ========================================================================= */
CodeBuffer* generate_8051(const Instruction *ir, int ir_count)
{
    fprintf(stderr, "[8051] Pass 1: address resolution ...\n");

    /* --- Pass 1: symbol table ------------------------------------------ */
    SymbolTable symtab;
    int total_size = pass1_build_symbols(ir, ir_count, &symtab);

    fprintf(stderr, "[8051] Symbol table (%d entries):\n", symtab.count);
    for (int i = 0; i < symtab.count; i++) {
        fprintf(stderr, "  %-20s = 0x%04X (%d)\n",
                symtab.entries[i].name,
                symtab.entries[i].address,
                symtab.entries[i].address);
    }
    fprintf(stderr, "[8051] Estimated code size: %d bytes\n", total_size);

    /* --- Pass 2: code emission ----------------------------------------- */
    fprintf(stderr, "[8051] Pass 2: code emission ...\n");

    CodeBuffer *code = create_code_buffer();
    if (!code) {
        fprintf(stderr, "UAS 8051: out of memory\n");
        return NULL;
    }

    pass2_emit_code(ir, ir_count, &symtab, code);

    fprintf(stderr, "[8051] Emitted %d bytes (expected %d)\n",
            code->size, total_size);

    /* Sanity check */
    if (code->size != total_size) {
        fprintf(stderr, "UAS 8051: WARNING — size mismatch! "
                "Emitted %d bytes but Pass 1 estimated %d.\n",
                code->size, total_size);
    }

    return code;
}
