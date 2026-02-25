/*
 * =============================================================================
 *  UAS - Unified Assembler System
 *  Phase 5: x86-64 Back-End (Code Generation)
 *
 *  File:    backend_x86_64.c
 *  Purpose: Translate UAS IR into raw x86-64 machine code.
 *
 *  ┌──────────────────────────────────────────────────────────────────────┐
 *  │  x86-64 Instruction Encoding Reference (Phase 5 subset)            │
 *  │                                                                    │
 *  │  Register encoding (low 3 bits, no REX.B needed):                  │
 *  │    RAX = 0, RCX = 1, RDX = 2, RBX = 3                             │
 *  │                                                                    │
 *  │  MOV r64, imm32    REX.W + C7 /0 + imm32          7 bytes          │
 *  │    48 C7 C0+rd  imm32[LE]                                          │
 *  │                                                                    │
 *  │  ADD r/m64, r64    REX.W + 01 + ModR/M             3 bytes          │
 *  │    48 01 (C0 | rs<<3 | rd)                                          │
 *  │                                                                    │
 *  │  MOV r/m64, r64    REX.W + 89 + ModR/M             3 bytes          │
 *  │    48 89 (C0 | rs<<3 | rd)                                          │
 *  │                                                                    │
 *  │  RET               C3                              1 byte           │
 *  │  NOP               90                              1 byte           │
 *  └──────────────────────────────────────────────────────────────────────┘
 *
 *  UAS registers R0-R3 map to RAX, RCX, RDX, RBX.
 *  R4-R15 are rejected at code-generation time (Phase 5 limitation).
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
 *  Index = UAS register number,  Value = x86-64 register encoding.
 *  Only R0-R3 are mapped in Phase 5.
 * ========================================================================= */
static const uint8_t X64_REG_ENC[4] = {
    0,  /* R0 -> RAX */
    1,  /* R1 -> RCX */
    2,  /* R2 -> RDX */
    3   /* R3 -> RBX */
};

static const char* X64_REG_NAME[4] = {
    "RAX", "RCX", "RDX", "RBX"
};

/* =========================================================================
 *  Error helpers
 * ========================================================================= */
static void x64_error(const Instruction *inst, const char *msg)
{
    fprintf(stderr,
            "\n"
            "  UAS x86-64 Backend Error\n"
            "  ------------------------\n"
            "  Line %d, Column %d: %s\n\n",
            inst->line, inst->column, msg);
    exit(1);
}

static void x64_validate_register(const Instruction *inst, int reg)
{
    if (reg < 0 || reg > 3) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "register R%d is not mapped in the x86-64 backend "
                 "(Phase 5 supports R0-R3 only: RAX, RCX, RDX, RBX)",
                 reg);
        x64_error(inst, msg);
    }
}

/* =========================================================================
 *  Emit helpers  —  build x86-64 instruction bytes
 * ========================================================================= */

/*
 * emit_mov_r64_imm32()
 *   MOV r64, imm32 (sign-extended to 64-bit)
 *   Encoding: REX.W + C7 /0 + imm32
 *   Bytes:    48 C7 (C0|rd) imm32[LE]     = 7 bytes
 */
static void emit_mov_r64_imm32(CodeBuffer *buf, uint8_t rd, int32_t imm)
{
    emit_byte(buf, 0x48);                        /* REX.W               */
    emit_byte(buf, 0xC7);                        /* opcode C7 /0        */
    emit_byte(buf, (uint8_t)(0xC0 | rd));        /* ModR/M: mod=11, /0  */
    /* imm32 little-endian */
    emit_byte(buf, (uint8_t)( imm        & 0xFF));
    emit_byte(buf, (uint8_t)((imm >>  8) & 0xFF));
    emit_byte(buf, (uint8_t)((imm >> 16) & 0xFF));
    emit_byte(buf, (uint8_t)((imm >> 24) & 0xFF));
}

/*
 * emit_add_r64_r64()
 *   ADD r/m64, r64
 *   Encoding: REX.W + 01 + ModR/M
 *   Bytes:    48 01 (C0 | src<<3 | dst)    = 3 bytes
 */
static void emit_add_r64_r64(CodeBuffer *buf, uint8_t dst, uint8_t src)
{
    emit_byte(buf, 0x48);                                /* REX.W        */
    emit_byte(buf, 0x01);                                /* ADD opcode   */
    emit_byte(buf, (uint8_t)(0xC0 | (src << 3) | dst)); /* ModR/M       */
}

/*
 * emit_mov_r64_r64()
 *   MOV r/m64, r64
 *   Encoding: REX.W + 89 + ModR/M
 *   Bytes:    48 89 (C0 | src<<3 | dst)    = 3 bytes
 */
static void emit_mov_r64_r64(CodeBuffer *buf, uint8_t dst, uint8_t src)
{
    emit_byte(buf, 0x48);                                /* REX.W        */
    emit_byte(buf, 0x89);                                /* MOV opcode   */
    emit_byte(buf, (uint8_t)(0xC0 | (src << 3) | dst)); /* ModR/M       */
}

/*
 * emit_ret()
 *   RET   = C3  (1 byte)
 */
static void emit_ret(CodeBuffer *buf)
{
    emit_byte(buf, 0xC3);
}

/*
 * emit_nop()
 *   NOP   = 90  (1 byte)
 */
static void emit_nop(CodeBuffer *buf)
{
    emit_byte(buf, 0x90);
}

/* =========================================================================
 *  generate_x86_64()  —  main entry point
 * ========================================================================= */
CodeBuffer* generate_x86_64(const Instruction *ir, int ir_count)
{
    fprintf(stderr, "[x86-64] Generating code for %d IR instructions ...\n",
            ir_count);

    CodeBuffer *code = create_code_buffer();
    if (!code) {
        fprintf(stderr, "UAS x86-64: out of memory\n");
        return NULL;
    }

    for (int i = 0; i < ir_count; i++) {
        const Instruction *inst = &ir[i];

        /* Labels don't emit code */
        if (inst->is_label)
            continue;

        switch (inst->opcode) {

        /* ---- LDI Rd, #imm  ->  MOV r64, imm32 ------------------------ */
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

        /* ---- ADD Rd, Rs  ->  ADD r64, r64 ----------------------------- */
        case OP_ADD: {
            int rd = inst->operands[0].data.reg;
            x64_validate_register(inst, rd);
            uint8_t enc_d = X64_REG_ENC[rd];

            if (inst->operands[1].type == OPERAND_REGISTER) {
                int rs = inst->operands[1].data.reg;
                x64_validate_register(inst, rs);
                uint8_t enc_s = X64_REG_ENC[rs];
                fprintf(stderr, "  ADD R%d, R%d -> ADD %s, %s\n",
                        rd, rs, X64_REG_NAME[rd], X64_REG_NAME[rs]);
                emit_add_r64_r64(code, enc_d, enc_s);
            } else {
                /* ADD Rd, #imm  ->  MOV RCX, imm; ADD Rd, RCX
                 * (use RCX as scratch unless Rd is RCX, then use RDX) */
                int32_t imm = (int32_t)inst->operands[1].data.imm;
                uint8_t scratch = (enc_d == 1) ? 2 : 1; /* avoid clobbering Rd */
                fprintf(stderr, "  ADD R%d, #%d -> MOV scratch, %d; ADD %s, scratch\n",
                        rd, imm, imm, X64_REG_NAME[rd]);
                emit_mov_r64_imm32(code, scratch, imm);
                emit_add_r64_r64(code, enc_d, scratch);
            }
            break;
        }

        /* ---- MOV Rd, Rs  ->  MOV r64, r64 ----------------------------- */
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

        /* ---- HLT  ->  RET  ------------------------------------------- */
        case OP_HLT:
            fprintf(stderr, "  HLT -> RET\n");
            emit_ret(code);
            break;

        /* ---- NOP  ->  NOP --------------------------------------------- */
        case OP_NOP:
            fprintf(stderr, "  NOP\n");
            emit_nop(code);
            break;

        /* ---- RET  ->  RET --------------------------------------------- */
        case OP_RET:
            fprintf(stderr, "  RET\n");
            emit_ret(code);
            break;

        /* ---- Unsupported ---------------------------------------------- */
        default: {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "opcode '%s' is not supported by the x86-64 backend "
                     "(Phase 5 supports: LDI, ADD, MOV, HLT, NOP, RET)",
                     opcode_name(inst->opcode));
            x64_error(inst, msg);
            break;
        }
        }
    }

    fprintf(stderr, "[x86-64] Emitted %d bytes\n", code->size);
    return code;
}
