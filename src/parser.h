/*
 * =============================================================================
 *  UA - Unified Assembler
 *  Phase 2: Parser & IR Generation
 *
 *  File:    parser.h
 *  Purpose: Public interface for the UA parser.
 *           Defines the Intermediate Representation (IR) structures:
 *           Opcode enum, operand types, operand data, and instruction layout.
 *
 *  Design:  The parser consumes a flat token stream from the lexer and
 *           produces a linear array of Instruction structs.  Each instruction
 *           is fully validated against the grammar before it is emitted.
 *           The IR is architecture-neutral — back-ends for x86, ARM, RISC-V,
 *           and 8051/Harvard will consume it in Phase 3.
 *
 *  License: MIT
 * =============================================================================
 */

#ifndef UA_PARSER_H
#define UA_PARSER_H

#include "lexer.h"      /* Token, UaTokenType */
#include <stdint.h>

/* =========================================================================
 *  Opcode Enum
 * =========================================================================
 *  One-to-one mapping with the mnemonic strings recognised by the lexer.
 *  Kept in the same order for fast table look-ups.
 * ========================================================================= */
typedef enum {
    /* --- Data movement -------------------------------------------------- */
    OP_MOV,             /* MOV   Rd, Rs            register-to-register      */
    OP_LDI,             /* LDI   Rd, imm           load immediate            */
    OP_LOAD,            /* LOAD  Rd, Rs            memory read  [Rs] -> Rd   */
    OP_STORE,           /* STORE Rs, Rd            memory write Rs -> [Rd]   */

    /* --- Arithmetic ----------------------------------------------------- */
    OP_ADD,             /* ADD   Rd, Rs/imm                                  */
    OP_SUB,             /* SUB   Rd, Rs/imm                                  */
    OP_MUL,             /* MUL   Rd, Rs/imm                                  */
    OP_DIV,             /* DIV   Rd, Rs/imm                                  */

    /* --- Bitwise -------------------------------------------------------- */
    OP_AND,             /* AND   Rd, Rs/imm                                  */
    OP_OR,              /* OR    Rd, Rs/imm                                  */
    OP_XOR,             /* XOR   Rd, Rs/imm                                  */
    OP_NOT,             /* NOT   Rd                 bitwise complement       */
    OP_SHL,             /* SHL   Rd, Rs/imm         shift left               */
    OP_SHR,             /* SHR   Rd, Rs/imm         shift right              */

    /* --- Comparison / control flow -------------------------------------- */
    OP_CMP,             /* CMP   Ra, Rb/imm         set flags                */
    OP_JMP,             /* JMP   label              unconditional jump       */
    OP_JZ,              /* JZ    label              jump if zero             */
    OP_JNZ,             /* JNZ   label              jump if not zero         */
    OP_CALL,            /* CALL  label              subroutine call          */
    OP_RET,             /* RET                      return from call         */

    /* --- Stack ---------------------------------------------------------- */
    OP_PUSH,            /* PUSH  Rs                 push register            */
    OP_POP,             /* POP   Rd                 pop to register          */

    /* --- Increment / Decrement ------------------------------------------- */
    OP_INC,             /* INC   Rd                 increment by 1           */
    OP_DEC,             /* DEC   Rd                 decrement by 1           */

    /* --- Software Interrupt --------------------------------------------- */
    OP_INT,             /* INT   #imm               software interrupt       */

    /* --- Miscellaneous -------------------------------------------------- */
    OP_NOP,             /* NOP                      no operation             */
    OP_HLT,             /* HLT                      halt execution           */

    OP_COUNT            /* Sentinel: total number of opcodes                 */
} Opcode;

/* =========================================================================
 *  Operand Types
 * ========================================================================= */
typedef enum {
    OPERAND_NONE,       /* Unused operand slot                               */
    OPERAND_REGISTER,   /* Virtual register R0-R15                           */
    OPERAND_IMMEDIATE,  /* Numeric literal                                   */
    OPERAND_LABEL_REF   /* Symbolic reference to a label                     */
} OperandType;

/* =========================================================================
 *  Operand Structure
 * =========================================================================
 *  A tagged union: `type` selects which field of `data` is active.
 * ========================================================================= */
#define UA_MAX_LABEL_LEN  64   /* Max label name length */

typedef struct {
    OperandType type;
    union {
        int      reg;                       /* Register number (0-15)      */
        int64_t  imm;                       /* Immediate value             */
        char     label[UA_MAX_LABEL_LEN];  /* Label name                  */
    } data;
} Operand;

/* =========================================================================
 *  Instruction Structure
 * =========================================================================
 *  Represents one fully-parsed assembly instruction (or a label definition).
 *
 *  - `is_label`:  if non-zero, this "instruction" is actually a label
 *                 definition and `label_name` holds its name.
 *                 `opcode` and `operands` are unused in that case.
 *
 *  - Otherwise:   `opcode` + up to MAX_OPERANDS operands describe
 *                 the operation.
 * ========================================================================= */
#define MAX_OPERANDS  3

typedef struct {
    /* --- Label-only entry ----------------------------------------------- */
    int     is_label;                       /* 1 = label def, 0 = instr    */
    char    label_name[UA_MAX_LABEL_LEN];  /* Label text (if is_label)    */

    /* --- Instruction data ----------------------------------------------- */
    Opcode  opcode;                         /* Which operation             */
    Operand operands[MAX_OPERANDS];         /* Operand slots               */
    int     operand_count;                  /* How many are used (0-3)     */

    /* --- Source location (for diagnostics) ------------------------------ */
    int     line;
    int     column;
} Instruction;

/* =========================================================================
 *  Public API
 * =========================================================================
 *
 * parse()
 *   Consumes the token array produced by tokenize() and returns a
 *   heap-allocated array of Instruction structs.  Grammar violations
 *   cause a formatted error message on stderr followed by exit(1).
 *
 *   Parameters:
 *     tokens            – token array from the lexer.
 *     token_count       – number of tokens (including EOF).
 *     instruction_count – [out] receives the number of IR instructions.
 *
 *   Returns:
 *     Pointer to a heap-allocated Instruction array, or NULL on alloc
 *     failure.  The caller must free it with free_instructions().
 * ------------------------------------------------------------------------- */
Instruction* parse(const Token *tokens, int token_count,
                   int *instruction_count);

/* -------------------------------------------------------------------------
 * free_instructions()
 *   Frees the array returned by parse().  Safe to call with NULL.
 * ------------------------------------------------------------------------- */
void free_instructions(Instruction *instructions);

/* -------------------------------------------------------------------------
 * opcode_name()
 *   Returns a human-readable string for a given Opcode value.
 * ------------------------------------------------------------------------- */
const char* opcode_name(Opcode op);

/* -------------------------------------------------------------------------
 * operand_type_name()
 *   Returns a human-readable string for a given OperandType.
 * ------------------------------------------------------------------------- */
const char* operand_type_name(OperandType type);

#endif /* UA_PARSER_H */
