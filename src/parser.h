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
    OP_JL,              /* JL    label              jump if less (signed)    */
    OP_JG,              /* JG    label              jump if greater (signed) */
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

    /* --- Variables ------------------------------------------------------- */
    OP_VAR,             /* VAR   name [, init]       declare variable         */
    OP_SET,             /* SET   name, Rs/imm        store to variable        */
    OP_GET,             /* GET   Rd, name            load from variable       */

    /* --- String / Byte / Syscall (Phase 8) ----------------------------- */
    OP_LDS,             /* LDS   Rd, "string"         load string address      */
    OP_LOADB,           /* LOADB Rd, Rs              byte load  [Rs] -> Rd    */
    OP_STOREB,          /* STOREB Rs, Rd             byte store Rs  -> [Rd]   */
    OP_SYS,             /* SYS                       native syscall           */

    /* --- Buffer allocation ---------------------------------------------- */
    OP_BUFFER,          /* BUFFER name, size         allocate N bytes         */

    /* --- Miscellaneous -------------------------------------------------- */
    OP_NOP,             /* NOP                      no operation             */
    OP_HLT,             /* HLT                      halt execution           */

    /* --- x86 Family (x86_32 & x86_64) ---------------------------------- */
    OP_CPUID,           /* CPUID                    CPU identification       */
    OP_RDTSC,           /* RDTSC                    read time-stamp counter  */
    OP_BSWAP,           /* BSWAP  Rd                byte-swap (endian)       */
    OP_PUSHA,           /* PUSHA                    push all GP regs (32)    */
    OP_POPA,            /* POPA                     pop all GP regs (32)     */

    /* --- 8051 Exclusive ------------------------------------------------- */
    OP_DJNZ,            /* DJNZ   Rd, label         dec & jump if not zero   */
    OP_CJNE,            /* CJNE   Rd, #imm, label   cmp & jump if not equal  */
    OP_SETB,            /* SETB   Rd                set bit                  */
    OP_CLR,             /* CLR    Rd                clear bit/register       */
    OP_RETI,            /* RETI                     return from interrupt    */

    /* --- ARM & ARM64 Exclusive ------------------------------------------ */
    OP_WFI,             /* WFI                      wait for interrupt       */
    OP_DMB,             /* DMB                      data memory barrier      */

    /* --- RISC-V Exclusive ----------------------------------------------- */
    OP_EBREAK,          /* EBREAK                   environment breakpoint   */
    OP_FENCE,           /* FENCE                    memory ordering fence    */

    /* --- Assembler Directives ------------------------------------------- */
    OP_ORG,             /* ORG   #addr              set origin address       */

    OP_COUNT            /* Sentinel: total number of opcodes                 */
} Opcode;

/* =========================================================================
 *  Operand Types
 * ========================================================================= */
typedef enum {
    OPERAND_NONE,       /* Unused operand slot                               */
    OPERAND_REGISTER,   /* Virtual register R0-R15                           */
    OPERAND_IMMEDIATE,  /* Numeric literal                                   */
    OPERAND_LABEL_REF,  /* Symbolic reference to a label                     */
    OPERAND_STRING      /* String literal (for LDS)                          */
} OperandType;

/* =========================================================================
 *  Operand Structure
 * =========================================================================
 *  A tagged union: `type` selects which field of `data` is active.
 * ========================================================================= */
#define UA_MAX_LABEL_LEN  128  /* Max label name length */

typedef struct {
    OperandType type;
    union {
        int      reg;                       /* Register number (0-15)      */
        int64_t  imm;                       /* Immediate value             */
        char     label[UA_MAX_LABEL_LEN];  /* Label name                  */
        char     string[UA_MAX_LABEL_LEN]; /* String literal (LDS)        */
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
#define MAX_FUNC_PARAMS  8    /* Max parameters per function definition */

typedef struct {
    /* --- Label-only entry ----------------------------------------------- */
    int     is_label;                       /* 1 = label def, 0 = instr    */
    char    label_name[UA_MAX_LABEL_LEN];  /* Label text (if is_label)    */

    /* --- Function definition -------------------------------------------- */
    int     is_function;                    /* 1 = func def with params    */
    int     param_count;                    /* Number of parameters        */
    char    param_names[MAX_FUNC_PARAMS][UA_MAX_LABEL_LEN];

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
