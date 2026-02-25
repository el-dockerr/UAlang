/*
 * =============================================================================
 *  UAS - Unified Assembler System
 *  Phase 2: Parser & IR Generation
 *
 *  File:    parser.c
 *  Purpose: Implementation of the UAS parser.
 *
 *  The parser walks the token stream produced by the lexer and builds a
 *  flat array of Instruction structs (the IR).  It enforces the UAS grammar:
 *
 *    program     ::= { line } EOF
 *    line        ::= [ label_def | instruction ] { COMMENT } NEWLINE
 *    label_def   ::= LABEL
 *    instruction ::= opcode operand_list
 *    operand_list::= (depends on opcode — see grammar table below)
 *
 *  Grammar violations produce a formatted diagnostic on stderr and
 *  terminate the process with exit(1).
 *
 *  License: MIT
 * =============================================================================
 */

#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* =========================================================================
 *  Internal constants
 * ========================================================================= */

#define INITIAL_IR_CAPACITY  64

/* =========================================================================
 *  Mnemonic-to-Opcode mapping table
 * =========================================================================
 *  Keep this synchronised with the OPCODES[] table in lexer.c and the
 *  Opcode enum in parser.h.
 * ========================================================================= */
typedef struct {
    const char *mnemonic;
    Opcode      opcode;
} MnemonicEntry;

static const MnemonicEntry MNEMONIC_TABLE[] = {
    { "MOV",   OP_MOV   },
    { "ADD",   OP_ADD   },
    { "SUB",   OP_SUB   },
    { "LDI",   OP_LDI   },
    { "LOAD",  OP_LOAD  },
    { "STORE", OP_STORE },
    { "NOP",   OP_NOP   },
    { "HLT",   OP_HLT   },
    { "CMP",   OP_CMP   },
    { "JMP",   OP_JMP   },
    { "JZ",    OP_JZ    },
    { "JNZ",   OP_JNZ   },
    { "CALL",  OP_CALL  },
    { "RET",   OP_RET   },
    { "PUSH",  OP_PUSH  },
    { "POP",   OP_POP   },
    { "AND",   OP_AND   },
    { "OR",    OP_OR    },
    { "XOR",   OP_XOR   },
    { "NOT",   OP_NOT   },
    { "SHL",   OP_SHL   },
    { "SHR",   OP_SHR   },
    { "MUL",   OP_MUL   },
    { "DIV",   OP_DIV   },
    { NULL,    OP_COUNT }       /* sentinel */
};

/* =========================================================================
 *  Operand-shape descriptors
 * =========================================================================
 *  Each opcode expects a fixed sequence of operand types.  We encode these
 *  as small arrays of OperandType terminated by OPERAND_NONE.
 *
 *  OPERAND_REG_OR_IMM is a synthetic tag meaning "register OR immediate";
 *  we resolve it at parse time.
 * ========================================================================= */
#define OPERAND_REG_OR_IMM  ((OperandType)100)  /* pseudo-tag, never stored */

/*
 *  Grammar shapes — indexed by Opcode.
 *
 *  Convention:
 *    R    = OPERAND_REGISTER
 *    I    = OPERAND_IMMEDIATE
 *    L    = OPERAND_LABEL_REF
 *    R|I  = OPERAND_REG_OR_IMM
 *    -    = no operand (OPERAND_NONE)
 */

/* Max number of expected operands for any instruction */
#define MAX_SHAPE  3

typedef struct {
    int          count;                 /* Number of operands expected      */
    OperandType  shape[MAX_SHAPE];      /* Expected type per position       */
} OpcodeShape;

/*
 *  Table indexed by Opcode — must stay in sync with the enum.
 *
 *    OP_MOV   : Rd, Rs
 *    OP_LDI   : Rd, imm
 *    OP_LOAD  : Rd, Rs
 *    OP_STORE : Rs, Rd
 *    OP_ADD   : Rd, Rs|imm
 *    OP_SUB   : Rd, Rs|imm
 *    OP_MUL   : Rd, Rs|imm
 *    OP_DIV   : Rd, Rs|imm
 *    OP_AND   : Rd, Rs|imm
 *    OP_OR    : Rd, Rs|imm
 *    OP_XOR   : Rd, Rs|imm
 *    OP_NOT   : Rd
 *    OP_SHL   : Rd, Rs|imm
 *    OP_SHR   : Rd, Rs|imm
 *    OP_CMP   : Ra, Rb|imm
 *    OP_JMP   : label
 *    OP_JZ    : label
 *    OP_JNZ   : label
 *    OP_CALL  : label
 *    OP_RET   : (none)
 *    OP_PUSH  : Rs
 *    OP_POP   : Rd
 *    OP_NOP   : (none)
 *    OP_HLT   : (none)
 */
static const OpcodeShape OPCODE_SHAPES[OP_COUNT] = {
    /* OP_MOV   */ { 2, { OPERAND_REGISTER,  OPERAND_REGISTER,   OPERAND_NONE } },
    /* OP_LDI   */ { 2, { OPERAND_REGISTER,  OPERAND_IMMEDIATE,  OPERAND_NONE } },
    /* OP_LOAD  */ { 2, { OPERAND_REGISTER,  OPERAND_REGISTER,   OPERAND_NONE } },
    /* OP_STORE */ { 2, { OPERAND_REGISTER,  OPERAND_REGISTER,   OPERAND_NONE } },
    /* OP_ADD   */ { 2, { OPERAND_REGISTER,  OPERAND_REG_OR_IMM, OPERAND_NONE } },
    /* OP_SUB   */ { 2, { OPERAND_REGISTER,  OPERAND_REG_OR_IMM, OPERAND_NONE } },
    /* OP_MUL   */ { 2, { OPERAND_REGISTER,  OPERAND_REG_OR_IMM, OPERAND_NONE } },
    /* OP_DIV   */ { 2, { OPERAND_REGISTER,  OPERAND_REG_OR_IMM, OPERAND_NONE } },
    /* OP_AND   */ { 2, { OPERAND_REGISTER,  OPERAND_REG_OR_IMM, OPERAND_NONE } },
    /* OP_OR    */ { 2, { OPERAND_REGISTER,  OPERAND_REG_OR_IMM, OPERAND_NONE } },
    /* OP_XOR   */ { 2, { OPERAND_REGISTER,  OPERAND_REG_OR_IMM, OPERAND_NONE } },
    /* OP_NOT   */ { 1, { OPERAND_REGISTER,  OPERAND_NONE,       OPERAND_NONE } },
    /* OP_SHL   */ { 2, { OPERAND_REGISTER,  OPERAND_REG_OR_IMM, OPERAND_NONE } },
    /* OP_SHR   */ { 2, { OPERAND_REGISTER,  OPERAND_REG_OR_IMM, OPERAND_NONE } },
    /* OP_CMP   */ { 2, { OPERAND_REGISTER,  OPERAND_REG_OR_IMM, OPERAND_NONE } },
    /* OP_JMP   */ { 1, { OPERAND_LABEL_REF, OPERAND_NONE,       OPERAND_NONE } },
    /* OP_JZ    */ { 1, { OPERAND_LABEL_REF, OPERAND_NONE,       OPERAND_NONE } },
    /* OP_JNZ   */ { 1, { OPERAND_LABEL_REF, OPERAND_NONE,       OPERAND_NONE } },
    /* OP_CALL  */ { 1, { OPERAND_LABEL_REF, OPERAND_NONE,       OPERAND_NONE } },
    /* OP_RET   */ { 0, { OPERAND_NONE,      OPERAND_NONE,       OPERAND_NONE } },
    /* OP_PUSH  */ { 1, { OPERAND_REGISTER,  OPERAND_NONE,       OPERAND_NONE } },
    /* OP_POP   */ { 1, { OPERAND_REGISTER,  OPERAND_NONE,       OPERAND_NONE } },
    /* OP_NOP   */ { 0, { OPERAND_NONE,      OPERAND_NONE,       OPERAND_NONE } },
    /* OP_HLT   */ { 0, { OPERAND_NONE,      OPERAND_NONE,       OPERAND_NONE } },
};

/* =========================================================================
 *  Error reporting
 * =========================================================================
 *  All syntax errors print a GCC-style diagnostic, then abort.
 * ========================================================================= */

/* Variadic-like error using a fixed format for consistency. */
static void syntax_error(const Token *tok, const char *msg)
{
    fprintf(stderr,
            "\n"
            "  UAS Syntax Error\n"
            "  -----------------\n"
            "  Line %d, Column %d: %s\n"
            "  Near token: '%s' (%s)\n\n",
            tok->line, tok->column, msg,
            tok->text, token_type_name(tok->type));
    exit(1);
}

static void syntax_error_expected(const Token *tok,
                                  const char *expected,
                                  const char *context)
{
    fprintf(stderr,
            "\n"
            "  UAS Syntax Error\n"
            "  -----------------\n"
            "  Line %d, Column %d: expected %s %s\n"
            "  Got: '%s' (%s)\n\n",
            tok->line, tok->column, expected, context,
            tok->text, token_type_name(tok->type));
    exit(1);
}

/* =========================================================================
 *  opcode_name()  —  human-readable opcode strings
 * ========================================================================= */
const char* opcode_name(Opcode op)
{
    switch (op) {
        case OP_MOV:   return "MOV";
        case OP_LDI:   return "LDI";
        case OP_LOAD:  return "LOAD";
        case OP_STORE: return "STORE";
        case OP_ADD:   return "ADD";
        case OP_SUB:   return "SUB";
        case OP_MUL:   return "MUL";
        case OP_DIV:   return "DIV";
        case OP_AND:   return "AND";
        case OP_OR:    return "OR";
        case OP_XOR:   return "XOR";
        case OP_NOT:   return "NOT";
        case OP_SHL:   return "SHL";
        case OP_SHR:   return "SHR";
        case OP_CMP:   return "CMP";
        case OP_JMP:   return "JMP";
        case OP_JZ:    return "JZ";
        case OP_JNZ:   return "JNZ";
        case OP_CALL:  return "CALL";
        case OP_RET:   return "RET";
        case OP_PUSH:  return "PUSH";
        case OP_POP:   return "POP";
        case OP_NOP:   return "NOP";
        case OP_HLT:   return "HLT";
        default:       return "???";
    }
}

/* =========================================================================
 *  operand_type_name()  —  human-readable operand type strings
 * ========================================================================= */
const char* operand_type_name(OperandType type)
{
    switch (type) {
        case OPERAND_NONE:      return "NONE";
        case OPERAND_REGISTER:  return "REG";
        case OPERAND_IMMEDIATE: return "IMM";
        case OPERAND_LABEL_REF: return "LABEL";
        default:                return "???";
    }
}

/* =========================================================================
 *  Helper: look up mnemonic string -> Opcode enum
 * ========================================================================= */
static int lookup_opcode(const char *mnemonic, Opcode *out)
{
    for (int i = 0; MNEMONIC_TABLE[i].mnemonic != NULL; i++) {
        if (strcmp(mnemonic, MNEMONIC_TABLE[i].mnemonic) == 0) {
            *out = MNEMONIC_TABLE[i].opcode;
            return 1;
        }
    }
    return 0;
}

/* =========================================================================
 *  Helper: peek at current token (bounds-checked)
 * ========================================================================= */
static const Token* peek(const Token *tokens, int pos, int token_count)
{
    if (pos >= token_count) return &tokens[token_count - 1]; /* EOF */
    return &tokens[pos];
}

/* =========================================================================
 *  Helper: check if a token terminates an instruction's operand list
 * ========================================================================= */
static int is_line_terminator(const Token *t)
{
    return t->type == TOKEN_NEWLINE ||
           t->type == TOKEN_COMMENT ||
           t->type == TOKEN_EOF;
}

/* =========================================================================
 *  Helper: ensure the IR array has room for one more instruction
 * ========================================================================= */
static Instruction* ensure_ir_capacity(Instruction *ir, int count,
                                       int *capacity)
{
    if (count < *capacity) return ir;

    int new_cap = (*capacity) * 2;
    Instruction *tmp = (Instruction *)realloc(
        ir, sizeof(Instruction) * (size_t)new_cap);
    if (!tmp) {
        fprintf(stderr, "UAS Parser: out of memory (realloc failed)\n");
        free(ir);
        return NULL;
    }
    *capacity = new_cap;
    return tmp;
}

/* =========================================================================
 *  Helper: initialise an Instruction to a clean default state
 * ========================================================================= */
static Instruction make_empty_instruction(int line, int col)
{
    Instruction inst;
    memset(&inst, 0, sizeof(inst));
    inst.line   = line;
    inst.column = col;
    return inst;
}

/* =========================================================================
 *  Helper: build an Operand from the current token
 *
 *  `expected` is the shape entry (OPERAND_REGISTER, OPERAND_IMMEDIATE,
 *  OPERAND_LABEL_REF, or OPERAND_REG_OR_IMM).  The function validates the
 *  actual token against the expectation and fills `out`.
 * ========================================================================= */
static void build_operand(const Token *tok, OperandType expected,
                          const char *opcode_str, Operand *out)
{
    /* --- Register ------------------------------------------------------- */
    if (tok->type == TOKEN_REGISTER) {
        if (expected != OPERAND_REGISTER && expected != OPERAND_REG_OR_IMM) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "for '%s': expected an immediate or label, got register",
                     opcode_str);
            syntax_error(tok, msg);
        }
        out->type     = OPERAND_REGISTER;
        out->data.reg = (int)tok->value;
        return;
    }

    /* --- Immediate number ----------------------------------------------- */
    if (tok->type == TOKEN_NUMBER) {
        if (expected != OPERAND_IMMEDIATE && expected != OPERAND_REG_OR_IMM) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "for '%s': expected a register, got immediate value",
                     opcode_str);
            syntax_error(tok, msg);
        }
        out->type     = OPERAND_IMMEDIATE;
        out->data.imm = tok->value;
        return;
    }

    /* --- Label reference (identifier used as operand) -------------------- */
    if (tok->type == TOKEN_IDENTIFIER || tok->type == TOKEN_LABEL_REF) {
        if (expected != OPERAND_LABEL_REF) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "for '%s': expected a register or immediate, "
                     "got label reference '%s'",
                     opcode_str, tok->text);
            syntax_error(tok, msg);
        }
        out->type = OPERAND_LABEL_REF;
        strncpy(out->data.label, tok->text, UAS_MAX_LABEL_LEN - 1);
        out->data.label[UAS_MAX_LABEL_LEN - 1] = '\0';
        return;
    }

    /* --- Type mismatch -------------------------------------------------- */
    {
        const char *wanted = "operand";
        switch (expected) {
            case OPERAND_REGISTER:  wanted = "register (R0-R15)";     break;
            case OPERAND_IMMEDIATE: wanted = "immediate value";       break;
            case OPERAND_LABEL_REF: wanted = "label reference";       break;
            default:                wanted = "register or immediate"; break;
        }
        char msg[128];
        snprintf(msg, sizeof(msg), "for '%s': expected %s", opcode_str, wanted);
        syntax_error_expected(tok, wanted, msg);
    }
}

/* =========================================================================
 *  parse()  —  main parser entry point
 *
 *  Algorithm:
 *    pos = 0
 *    while tokens[pos] != EOF:
 *       skip NEWLINE / COMMENT
 *       if LABEL  -> emit label-def instruction
 *       if OPCODE -> look up shape; consume operands; emit instruction
 *       otherwise -> syntax error
 * ========================================================================= */
Instruction* parse(const Token *tokens, int token_count,
                   int *instruction_count)
{
    if (!tokens || !instruction_count) return NULL;

    int capacity = INITIAL_IR_CAPACITY;
    int count    = 0;

    Instruction *ir = (Instruction *)malloc(
        sizeof(Instruction) * (size_t)capacity);
    if (!ir) {
        fprintf(stderr, "UAS Parser: out of memory (initial malloc)\n");
        *instruction_count = 0;
        return NULL;
    }

    int pos = 0;

    while (pos < token_count) {
        const Token *cur = &tokens[pos];

        /* ---- Skip blank lines and comments ---------------------------- */
        if (cur->type == TOKEN_NEWLINE || cur->type == TOKEN_COMMENT) {
            pos++;
            continue;
        }

        /* ---- EOF ------------------------------------------------------ */
        if (cur->type == TOKEN_EOF) break;

        /* ---- Label definition ----------------------------------------- */
        if (cur->type == TOKEN_LABEL) {
            ir = ensure_ir_capacity(ir, count, &capacity);
            if (!ir) { *instruction_count = 0; return NULL; }

            Instruction inst = make_empty_instruction(cur->line, cur->column);
            inst.is_label = 1;
            strncpy(inst.label_name, cur->text, UAS_MAX_LABEL_LEN - 1);
            inst.label_name[UAS_MAX_LABEL_LEN - 1] = '\0';

            ir[count++] = inst;
            pos++;

            /* Skip trailing comments / newline on the same line */
            while (pos < token_count &&
                   (tokens[pos].type == TOKEN_COMMENT ||
                    tokens[pos].type == TOKEN_NEWLINE)) {
                pos++;
            }
            continue;
        }

        /* ---- Opcode (instruction) ------------------------------------- */
        if (cur->type == TOKEN_OPCODE) {
            Opcode op;
            if (!lookup_opcode(cur->text, &op)) {
                syntax_error(cur, "unknown opcode (internal error)");
            }

            const OpcodeShape *shape = &OPCODE_SHAPES[op];

            Instruction inst = make_empty_instruction(cur->line, cur->column);
            inst.is_label      = 0;
            inst.opcode        = op;
            inst.operand_count = shape->count;

            pos++;  /* consume the opcode token */

            /* --- Parse operands according to the shape table ------------ */
            for (int i = 0; i < shape->count; i++) {

                /* Between operands (i > 0) we require a comma */
                if (i > 0) {
                    const Token *comma = peek(tokens, pos, token_count);
                    if (comma->type != TOKEN_COMMA) {
                        char ctx[128];
                        snprintf(ctx, sizeof(ctx),
                                 "after operand %d of '%s'",
                                 i, opcode_name(op));
                        syntax_error_expected(comma, "','", ctx);
                    }
                    pos++;  /* consume comma */
                }

                /* The actual operand */
                const Token *operand_tok = peek(tokens, pos, token_count);

                /* Check we haven't hit end-of-line prematurely */
                if (is_line_terminator(operand_tok)) {
                    char msg[128];
                    snprintf(msg, sizeof(msg),
                             "'%s' expects %d operand(s), but only %d given",
                             opcode_name(op), shape->count, i);
                    syntax_error(operand_tok, msg);
                }

                build_operand(operand_tok, shape->shape[i],
                              opcode_name(op), &inst.operands[i]);
                pos++;  /* consume operand token */
            }

            /* --- After all operands: must hit line terminator ----------- */
            {
                const Token *after = peek(tokens, pos, token_count);
                if (!is_line_terminator(after) && after->type != TOKEN_EOF) {
                    char msg[128];
                    snprintf(msg, sizeof(msg),
                             "unexpected token after '%s' instruction "
                             "(expected end of line)",
                             opcode_name(op));
                    syntax_error(after, msg);
                }
            }

            /* Emit instruction */
            ir = ensure_ir_capacity(ir, count, &capacity);
            if (!ir) { *instruction_count = 0; return NULL; }
            ir[count++] = inst;

            /* Consume rest of line (comments, newline) */
            while (pos < token_count &&
                   (tokens[pos].type == TOKEN_COMMENT ||
                    tokens[pos].type == TOKEN_NEWLINE)) {
                pos++;
            }
            continue;
        }

        /* ---- Anything else is a syntax error -------------------------- */
        syntax_error(cur, "expected an opcode or label");
    }

    *instruction_count = count;
    return ir;
}

/* =========================================================================
 *  free_instructions()
 * ========================================================================= */
void free_instructions(Instruction *instructions)
{
    free(instructions);     /* safe even if NULL */
}
