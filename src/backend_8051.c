/*
 * =============================================================================
 *  UA - Unified Assembler
 *  Phase 3: 8051 Back-End (Code Generation)
 *
 *  File:    backend_8051.c
 *  Purpose: Two-pass assembler that translates the architecture-neutral UA
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
 *  UA register R0-R7 map directly to 8051 R0-R7 (bank 0).
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
            "  UA 8051 Backend Error\n"
            "  ----------------------\n"
            "  Line %d, Column %d: %s\n\n",
            inst->line, inst->column, msg);
    exit(1);
}

static void validate_register(const Instruction *inst, int reg)
{
    if (reg < 0 || reg > 7) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "register R%d is not available on the 8051 "
                 "(only R0-R7 supported)", reg);
        backend_error(inst, msg);
    }
}

static void validate_imm8(const Instruction *inst, int64_t val)
{
    if (val < -128 || val > 255) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "immediate value %lld out of 8-bit range (-128..255)",
                 (long long)val);
        backend_error(inst, msg);
    }
}

/* =========================================================================
 *  8051 Variable Table — variables map to internal RAM direct addresses
 *
 *  8051 internal RAM layout (bank 0):
 *    0x00-0x07  Register bank 0 (R0-R7)
 *    0x08-0x7F  General purpose (bit-addressable 0x20-0x2F)
 *    0x80-0xFF  SFR space (not usable for variables)
 *
 *  We allocate variables starting at 0x08, one byte each.
 * ========================================================================= */
#define I8051_VAR_BASE    0x08   /* first usable direct address             */
#define I8051_VAR_LIMIT   0x80   /* exclusive upper bound                   */
#define I8051_MAX_VARS    ((I8051_VAR_LIMIT) - (I8051_VAR_BASE))  /* 120    */

typedef struct {
    char    name[UA_MAX_LABEL_LEN];
    uint8_t address;           /* direct address in internal RAM            */
    int64_t init_value;        /* initial value (0 if unspecified)          */
    int     has_init;          /* 1 if an initialiser was supplied          */
} I8051VarEntry;

typedef struct {
    I8051VarEntry vars[I8051_MAX_VARS];
    int           count;
} I8051VarTable;

/* =========================================================================
 *  Buffer name table  —  tracks which names are buffers (not variables)
 * ========================================================================= */
#define I8051_MAX_BUFFERS  32

typedef struct {
    char names[I8051_MAX_BUFFERS][UA_MAX_LABEL_LEN];
    int  count;
} I8051BufTable;

static void i8051_buftab_init(I8051BufTable *bt) {
    bt->count = 0;
}

static void i8051_buftab_add(I8051BufTable *bt, const char *name) {
    if (bt->count >= I8051_MAX_BUFFERS) return;
    strncpy(bt->names[bt->count], name, UA_MAX_LABEL_LEN - 1);
    bt->names[bt->count][UA_MAX_LABEL_LEN - 1] = '\0';
    bt->count++;
}

static int i8051_buftab_has(const I8051BufTable *bt, const char *name) {
    for (int i = 0; i < bt->count; i++) {
        if (strcmp(bt->names[i], name) == 0) return 1;
    }
    return 0;
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
        fprintf(stderr, "UA 8051: symbol table overflow (max %d)\n",
                MAX_SYMBOLS);
        exit(1);
    }
    strncpy(st->entries[st->count].name, name, UA_MAX_LABEL_LEN - 1);
    st->entries[st->count].name[UA_MAX_LABEL_LEN - 1] = '\0';
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
                return 3;   /* MOV A,Ra; CLR C; SUBB A,Rb  (3 bytes) */
            else
                return 4;   /* MOV A,Ra; CJNE A,#imm,$+3 */

        case OP_JMP:   /* LJMP addr16 */
            return 3;

        case OP_JZ:    /* JZ rel */
            return 2;

        case OP_JNZ:   /* JNZ rel */
            return 2;

        case OP_JL:    /* JC rel */
            return 2;

        case OP_JG:    /* JC skip + JZ skip + SJMP target */
            return 6;

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

        case OP_INC:   /* INC Rn */
            return 1;

        case OP_DEC:   /* DEC Rn */
            return 1;

        case OP_INT:   /* LCALL vector_addr  (polyfill) */
            return 3;

        case OP_VAR:
            /* Declaration only — 0 bytes unless init value supplied */
            if (inst->operand_count > 1 &&
                inst->operands[1].type == OPERAND_IMMEDIATE) {
                return 3;   /* MOV direct, #imm  (0x75, addr, imm) */
            }
            return 0;

        case OP_BUFFER:
            return 0;   /* declaration only — no bytes emitted */

        case OP_SET:
            if (inst->operands[1].type == OPERAND_REGISTER)
                return 2;   /* MOV direct, Rn    (0x88+n, addr) */
            else
                return 3;   /* MOV direct, #imm  (0x75, addr, imm) */

        case OP_GET:
            return 2;       /* MOV Rn, direct    (0xA8+n, addr) */

        /* ---- New Phase-8 instructions --------------------------------- */
        case OP_LDS:
            return 3;       /* MOV DPTR, #imm16  (0x90, high, low) */
        case OP_LOADB:
            return 2;       /* MOV A, @Ri; MOV Rd, A  (same as LOAD) */
        case OP_STOREB:
            return 2;       /* MOV A, Rs; MOV @Ri, A  (same as STORE) */
        case OP_SYS:
            backend_error(inst,
                "SYS is not supported on the 8051 (baremetal, no OS)");
            return 0;

        /* ---- Architecture-specific opcodes (8051) ---------------------- */
        case OP_DJNZ:   return 2;   /* D8+n, rel8 */
        case OP_CJNE:
            /* CJNE A, #imm, rel8 = 3 bytes; if reg!=A, MOV A,Rn (1) + 3 = 4 */
            if (inst->operands[0].data.reg == 0) return 3;
            return 4;
        case OP_SETB:   return 2;   /* D2, bit_addr */
        case OP_CLR:
            /* CLR A = 1 byte (E4); CLR bit = 2 bytes (C2, addr) */
            if (inst->operands[0].data.reg == 0) return 1;
            return 2;
        case OP_RETI:   return 1;   /* 32 */

        /* ---- Assembler directives ------------------------------------- */
        case OP_ORG:    return 0;   /* handled specially in pass 1 */

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
                               SymbolTable *st, I8051VarTable *vtab,
                               I8051BufTable *buftab)
{
    symtab_init(st);
    vtab->count = 0;
    i8051_buftab_init(buftab);
    int pc = 0;    /* program counter (byte offset) */

    for (int i = 0; i < ir_count; i++) {
        const Instruction *inst = &ir[i];

        if (inst->is_label) {
            /* Check for duplicate */
            if (symtab_lookup(st, inst->label_name) >= 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "duplicate label '%s'", inst->label_name);
                backend_error(inst, msg);
            }
            symtab_add(st, inst->label_name, pc);
        } else if (inst->opcode == OP_VAR) {
            /* Allocate a direct-address slot in internal RAM */
            const char *vname = inst->operands[0].data.label;
            if (vtab->count >= I8051_MAX_VARS) {
                backend_error(inst,
                              "too many variables (8051 internal RAM full)");
            }
            if (symtab_lookup(st, vname) >= 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "duplicate variable '%s'", vname);
                backend_error(inst, msg);
            }
            int addr = I8051_VAR_BASE + vtab->count;
            I8051VarEntry *v = &vtab->vars[vtab->count];
            strncpy(v->name, vname, UA_MAX_LABEL_LEN - 1);
            v->name[UA_MAX_LABEL_LEN - 1] = '\0';
            v->address = (uint8_t)addr;
            if (inst->operand_count > 1 &&
                inst->operands[1].type == OPERAND_IMMEDIATE) {
                v->has_init   = 1;
                v->init_value = inst->operands[1].data.imm;
            } else {
                v->has_init   = 0;
                v->init_value = 0;
            }
            vtab->count++;

            /* Register variable name in symbol table so SET/GET can find it.
             * The "address" here is the direct RAM address (not a PC offset). */
            symtab_add(st, vname, addr);

            pc += instruction_size_8051(inst);
        } else if (inst->opcode == OP_BUFFER) {
            /* Allocate consecutive bytes in internal RAM for a buffer */
            const char *bname = inst->operands[0].data.label;
            int bsize = (int)inst->operands[1].data.imm;
            int addr = I8051_VAR_BASE + vtab->count;
            if (addr + bsize > I8051_VAR_LIMIT) {
                backend_error(inst,
                              "BUFFER too large for 8051 internal RAM");
            }
            if (symtab_lookup(st, bname) >= 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "duplicate buffer '%s'", bname);
                backend_error(inst, msg);
            }
            symtab_add(st, bname, addr);
            i8051_buftab_add(buftab, bname);
            vtab->count += bsize;  /* reserve bsize bytes */

            pc += instruction_size_8051(inst);
        } else if (inst->opcode == OP_ORG) {
            /* @ORG <address> — advance PC to the given address */
            uint32_t target = (uint32_t)inst->operands[0].data.imm;
            if ((int)target < pc) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "@ORG 0x%04X would move address backwards "
                         "(current PC = 0x%04X)", target, (unsigned)pc);
                backend_error(inst, msg);
            }
            pc = (int)target;
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
                            const SymbolTable *st,
                            const I8051BufTable *buftab,
                            CodeBuffer *buf)
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
                char msg[256];
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
                char msg[256];
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
                char msg[256];
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
         *  JL label  ->  JC rel   [0x40, rel8]                2 bytes
         *  After CMP, Carry is set if Ra < Rb (unsigned).
         * ---------------------------------------------------------------- */
        case OP_JL:
            target_addr = symtab_lookup(st, inst->operands[0].data.label);
            if (target_addr < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "undefined label '%s'",
                         inst->operands[0].data.label);
                backend_error(inst, msg);
            }
            rel = target_addr - (buf->size + 2);
            if (rel < -128 || rel > 127) {
                backend_error(inst,
                    "JL target out of range for 8-bit relative jump");
            }
            emit(buf, 0x40);  /* JC rel */
            emit(buf, (uint8_t)(rel & 0xFF));
            break;

        /* ----------------------------------------------------------------
         *  JG label  ->  polyfill (6 bytes)
         *  After CMP, Ra > Rb means C==0 AND A!=0.
         *    JC  $+4   [0x40, 0x04]  — skip if less
         *    JZ  $+2   [0x60, 0x02]  — skip if equal
         *    SJMP target [0x80, rel8] — take jump (greater)
         * ---------------------------------------------------------------- */
        case OP_JG:
            target_addr = symtab_lookup(st, inst->operands[0].data.label);
            if (target_addr < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "undefined label '%s'",
                         inst->operands[0].data.label);
                backend_error(inst, msg);
            }
            /* JC $+4 (skip SJMP if carry set = less than) */
            emit(buf, 0x40);       /* JC */
            emit(buf, 0x04);       /* skip 4 bytes ahead to after SJMP */
            /* JZ $+2 (skip SJMP if zero = equal) */
            emit(buf, 0x60);       /* JZ */
            emit(buf, 0x02);       /* skip 2 bytes ahead to after SJMP */
            /* SJMP target (greater than) */
            rel = target_addr - (buf->size + 2);
            if (rel < -128 || rel > 127) {
                backend_error(inst,
                    "JG target out of range for 8-bit relative jump");
            }
            emit(buf, 0x80);       /* SJMP */
            emit(buf, (uint8_t)(rel & 0xFF));
            break;

        /* ----------------------------------------------------------------
         *  CALL label  ->  LCALL addr16   [0x12, hi, lo]      3 bytes
         * ---------------------------------------------------------------- */
        case OP_CALL:
            target_addr = symtab_lookup(st, inst->operands[0].data.label);
            if (target_addr < 0) {
                char msg[256];
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

        /* ----------------------------------------------------------------
         *  INC Rd  ->  INC Rn   [0x08+n]                      1 byte
         * ---------------------------------------------------------------- */
        case OP_INC:
            rd = inst->operands[0].data.reg;
            validate_register(inst, rd);
            emit(buf, (uint8_t)(0x08 + rd));
            break;

        /* ----------------------------------------------------------------
         *  DEC Rd  ->  DEC Rn   [0x18+n]                      1 byte
         * ---------------------------------------------------------------- */
        case OP_DEC:
            rd = inst->operands[0].data.reg;
            validate_register(inst, rd);
            emit(buf, (uint8_t)(0x18 + rd));
            break;

        /* ----------------------------------------------------------------
         *  INT #val  ->  LCALL vector_addr   (polyfill)        3 bytes
         *  8051 has no software interrupt instruction.
         *  Interrupt vector address = (val * 8) + 3.
         * ---------------------------------------------------------------- */
        case OP_INT: {
            imm = inst->operands[0].data.imm;
            uint16_t vector = (uint16_t)((imm * 8) + 3);
            emit_lcall(buf, vector);
            break;
        }

        /* ----------------------------------------------------------------
         *  VAR name [, #imm]  —  allocate direct-address variable
         *  If an initialiser is present: MOV direct, #imm  (3 bytes)
         *  Otherwise: no bytes emitted.
         * ---------------------------------------------------------------- */
        case OP_VAR: {
            const char *vname = inst->operands[0].data.label;
            int addr = symtab_lookup(st, vname);
            if (addr < 0) {
                backend_error(inst, "internal: VAR address not found");
            }
            if (inst->operand_count > 1 &&
                inst->operands[1].type == OPERAND_IMMEDIATE) {
                int64_t val = inst->operands[1].data.imm;
                validate_imm8(inst, val);
                /* MOV direct, #data : 0x75, direct, data */
                emit(buf, 0x75);
                emit(buf, (uint8_t)addr);
                emit(buf, (uint8_t)(val & 0xFF));
            }
            break;
        }

        /* ----------------------------------------------------------------
         *  BUFFER name, size  —  declaration only, no bytes emitted
         * ---------------------------------------------------------------- */
        case OP_BUFFER:
            /* Allocation handled in pass 1 — nothing to emit */
            break;

        /* ----------------------------------------------------------------
         *  ORG addr  — pad with zeros until target address is reached
         * ---------------------------------------------------------------- */
        case OP_ORG: {
            uint32_t target = (uint32_t)inst->operands[0].data.imm;
            while (buf->size < (int)target) {
                emit(buf, 0x00);
            }
            break;
        }

        /* ----------------------------------------------------------------
         *  SET name, Rs    ->  MOV direct, Rn  [0x88+n, addr]  2 bytes
         *  SET name, #imm  ->  MOV direct,#imm [0x75, addr, imm] 3 bytes
         * ---------------------------------------------------------------- */
        case OP_SET: {
            const char *vname = inst->operands[0].data.label;
            int addr = symtab_lookup(st, vname);
            if (addr < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "undefined variable '%s'", vname);
                backend_error(inst, msg);
            }
            if (inst->operands[1].type == OPERAND_REGISTER) {
                rs = inst->operands[1].data.reg;
                validate_register(inst, rs);
                /* MOV direct, Rn : 0x88+n, direct */
                emit(buf, (uint8_t)(0x88 + rs));
                emit(buf, (uint8_t)addr);
            } else {
                imm = inst->operands[1].data.imm;
                validate_imm8(inst, imm);
                /* MOV direct, #data : 0x75, direct, data */
                emit(buf, 0x75);
                emit(buf, (uint8_t)addr);
                emit(buf, (uint8_t)(imm & 0xFF));
            }
            break;
        }

        /* ----------------------------------------------------------------
         *  GET Rd, name  ->  MOV Rn, direct  [0xA8+n, addr]   2 bytes
         *  For buffers:  MOV Rn, #addr     [0x78+n, addr]   2 bytes
         * ---------------------------------------------------------------- */
        case OP_GET: {
            rd = inst->operands[0].data.reg;
            const char *vname = inst->operands[1].data.label;
            validate_register(inst, rd);
            int addr = symtab_lookup(st, vname);
            if (addr < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "undefined variable '%s'", vname);
                backend_error(inst, msg);
            }
            if (i8051_buftab_has(buftab, vname)) {
                /* Load buffer ADDRESS as immediate */
                emit(buf, (uint8_t)(0x78 + rd));  /* MOV Rn, #imm */
                emit(buf, (uint8_t)addr);
            } else {
                /* Load variable VALUE from direct address */
                emit(buf, (uint8_t)(0xA8 + rd));  /* MOV Rn, direct */
                emit(buf, (uint8_t)addr);
            }
            break;
        }

        /* ----------------------------------------------------------------
         *  LDS Rd, "str"  ->  MOV DPTR, #addr16  [0x90, hi, lo]  3 bytes
         *  Note: On 8051, string address is loaded into DPTR (16-bit).
         *  The destination register Rd is ignored; use MOVC A,@A+DPTR
         *  to read individual string bytes from code memory.
         * ---------------------------------------------------------------- */
        case OP_LDS: {
            (void)inst->operands[0].data.reg;  /* Rd ignored on 8051 */
            /* String address not yet computed — emit placeholder.
             * For 8051, strings are not separately stored; this is a
             * stub implementation. */
            emit(buf, 0x90);  /* MOV DPTR, #imm16 */
            emit(buf, 0x00);  /* high byte   */
            emit(buf, 0x00);  /* low byte    */
            break;
        }

        /* ----------------------------------------------------------------
         *  LOADB Rd, Rs  ->  MOV A, @Ri; MOV Rd, A           2 bytes
         *  Same as LOAD — 8051 is natively 8-bit.
         *  Rs must be R0 or R1 (indirect addressing constraint).
         * ---------------------------------------------------------------- */
        case OP_LOADB:
            rd = inst->operands[0].data.reg;
            rs = inst->operands[1].data.reg;
            validate_register(inst, rd);
            if (rs != 0 && rs != 1) {
                backend_error(inst,
                    "LOADB: indirect source must be R0 or R1 on 8051 "
                    "(MOV A, @Ri)");
            }
            emit(buf, (uint8_t)(0xE6 + rs));
            emit_mov_rn_a(buf, rd);
            break;

        /* ----------------------------------------------------------------
         *  STOREB Rs, Rd  ->  MOV A, Rs; MOV @Ri, A          2 bytes
         *  Same as STORE — 8051 is natively 8-bit.
         *  Rd must be R0 or R1 (indirect addressing constraint).
         * ---------------------------------------------------------------- */
        case OP_STOREB:
            rs = inst->operands[0].data.reg;
            rd = inst->operands[1].data.reg;
            validate_register(inst, rs);
            if (rd != 0 && rd != 1) {
                backend_error(inst,
                    "STOREB: indirect destination must be R0 or R1 on 8051 "
                    "(MOV @Ri, A)");
            }
            emit_mov_a_rn(buf, rs);
            emit(buf, (uint8_t)(0xF6 + rd));
            break;

        /* ----------------------------------------------------------------
         *  SYS  — not supported on baremetal 8051
         * ---------------------------------------------------------------- */
        case OP_SYS:
            backend_error(inst,
                "SYS is not supported on the 8051 (baremetal, no OS)");
            break;

        /* ----------------------------------------------------------------
         *  DJNZ Rn, label  ->  [0xD8+n, rel8]                2 bytes
         *  Decrement Rn and jump if not zero.
         * ---------------------------------------------------------------- */
        case OP_DJNZ:
            rd = inst->operands[0].data.reg;
            validate_register(inst, rd);
            target_addr = symtab_lookup(st, inst->operands[1].data.label);
            if (target_addr < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "undefined label '%s'",
                         inst->operands[1].data.label);
                backend_error(inst, msg);
            }
            rel = target_addr - (buf->size + 2);
            if (rel < -128 || rel > 127) {
                backend_error(inst,
                    "DJNZ target out of range for 8-bit relative jump");
            }
            emit(buf, (uint8_t)(0xD8 + rd));
            emit(buf, (uint8_t)(rel & 0xFF));
            break;

        /* ----------------------------------------------------------------
         *  CJNE Rn, #imm, label  ->  CJNE A, #imm, rel8      3-4 bytes
         *  If Rn != A, polyfill via MOV A, Rn first.
         * ---------------------------------------------------------------- */
        case OP_CJNE: {
            rd  = inst->operands[0].data.reg;
            imm = inst->operands[1].data.imm;
            validate_register(inst, rd);
            int cjne_size = (rd == 0) ? 3 : 4;
            target_addr = symtab_lookup(st, inst->operands[2].data.label);
            if (target_addr < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "undefined label '%s'",
                         inst->operands[2].data.label);
                backend_error(inst, msg);
            }
            rel = target_addr - (buf->size + cjne_size);
            if (rel < -128 || rel > 127) {
                backend_error(inst,
                    "CJNE target out of range for 8-bit relative jump");
            }
            if (rd != 0) {
                emit_mov_a_rn(buf, rd);  /* MOV A, Rn  (1 byte) */
            }
            emit(buf, 0xB4);             /* CJNE A, #imm, rel8 */
            emit(buf, (uint8_t)(imm & 0xFF));
            emit(buf, (uint8_t)(rel & 0xFF));
            break;
        }

        /* ----------------------------------------------------------------
         *  SETB bit_addr  ->  [0xD2, bit_addr]               2 bytes
         *  Register number is used as direct bit address.
         * ---------------------------------------------------------------- */
        case OP_SETB:
            rd = inst->operands[0].data.reg;
            emit(buf, 0xD2);
            emit(buf, (uint8_t)(rd & 0xFF));
            break;

        /* ----------------------------------------------------------------
         *  CLR reg  ->  0xE4 (CLR A) or 0xC2 (CLR bit)       1-2 bytes
         * ---------------------------------------------------------------- */
        case OP_CLR:
            rd = inst->operands[0].data.reg;
            if (rd == 0) {
                emit(buf, 0xE4);         /* CLR A */
            } else {
                emit(buf, 0xC2);         /* CLR bit_addr */
                emit(buf, (uint8_t)(rd & 0xFF));
            }
            break;

        /* ----------------------------------------------------------------
         *  RETI  ->  [0x32]                                   1 byte
         * ---------------------------------------------------------------- */
        case OP_RETI:
            emit(buf, 0x32);
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

    /* --- Pass 1: symbol table + variable table ------------------------- */
    SymbolTable    symtab;
    I8051VarTable  vtab;
    I8051BufTable  buftab;
    int total_size = pass1_build_symbols(ir, ir_count, &symtab, &vtab, &buftab);

    fprintf(stderr, "[8051] Symbol table (%d entries):\n", symtab.count);
    for (int i = 0; i < symtab.count; i++) {
        fprintf(stderr, "  %-20s = 0x%04X (%d)\n",
                symtab.entries[i].name,
                symtab.entries[i].address,
                symtab.entries[i].address);
    }
    if (vtab.count > 0) {
        fprintf(stderr, "[8051] Variables (%d, direct RAM 0x%02X-0x%02X):\n",
                vtab.count, I8051_VAR_BASE,
                I8051_VAR_BASE + vtab.count - 1);
        for (int v = 0; v < vtab.count; v++) {
            fprintf(stderr, "  %-20s @ 0x%02X", vtab.vars[v].name,
                    vtab.vars[v].address);
            if (vtab.vars[v].has_init)
                fprintf(stderr, " = %d", (int)vtab.vars[v].init_value);
            fprintf(stderr, "\n");
        }
    }
    fprintf(stderr, "[8051] Estimated code size: %d bytes\n", total_size);

    /* --- Pass 2: code emission ----------------------------------------- */
    fprintf(stderr, "[8051] Pass 2: code emission ...\n");

    CodeBuffer *code = create_code_buffer();
    if (!code) {
        fprintf(stderr, "UA 8051: out of memory\n");
        return NULL;
    }

    pass2_emit_code(ir, ir_count, &symtab, &buftab, code);

    fprintf(stderr, "[8051] Emitted %d bytes (expected %d)\n",
            code->size, total_size);

    /* Sanity check */
    if (code->size != total_size) {
        fprintf(stderr, "UA 8051: WARNING — size mismatch! "
                "Emitted %d bytes but Pass 1 estimated %d.\n",
                code->size, total_size);
    }

    return code;
}
