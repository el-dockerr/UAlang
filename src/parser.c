/*
 * =============================================================================
 *  UA - Unified Assembler
 *  Phase 2: Parser & IR Generation
 *
 *  File:    parser.c
 *  Purpose: Implementation of the UA parser.
 *
 *  The parser walks the token stream produced by the lexer and builds a
 *  flat array of Instruction structs (the IR).  It enforces the UA grammar:
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
    { "INC",   OP_INC   },
    { "DEC",   OP_DEC   },
    { "INT",   OP_INT   },
    { "VAR",   OP_VAR   },
    { "SET",   OP_SET   },
    { "GET",   OP_GET   },
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
    /* OP_INC   */ { 1, { OPERAND_REGISTER,  OPERAND_NONE,       OPERAND_NONE } },
    /* OP_DEC   */ { 1, { OPERAND_REGISTER,  OPERAND_NONE,       OPERAND_NONE } },
    /* OP_INT   */ { 1, { OPERAND_IMMEDIATE, OPERAND_NONE,       OPERAND_NONE } },
    /* OP_VAR   */ { 0, { OPERAND_NONE,      OPERAND_NONE,       OPERAND_NONE } }, /* special */
    /* OP_SET   */ { 0, { OPERAND_NONE,      OPERAND_NONE,       OPERAND_NONE } }, /* special */
    /* OP_GET   */ { 0, { OPERAND_NONE,      OPERAND_NONE,       OPERAND_NONE } }, /* special */
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
            "  UA Syntax Error\n"
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
            "  UA Syntax Error\n"
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
        case OP_INC:   return "INC";
        case OP_DEC:   return "DEC";
        case OP_INT:   return "INT";
        case OP_VAR:   return "VAR";
        case OP_SET:   return "SET";
        case OP_GET:   return "GET";
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
        fprintf(stderr, "UA Parser: out of memory (realloc failed)\n");
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
            char msg[256];
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
            char msg[256];
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
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "for '%s': expected a register or immediate, "
                     "got label reference '%s'",
                     opcode_str, tok->text);
            syntax_error(tok, msg);
        }
        out->type = OPERAND_LABEL_REF;
        strncpy(out->data.label, tok->text, UA_MAX_LABEL_LEN - 1);
        out->data.label[UA_MAX_LABEL_LEN - 1] = '\0';
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
        char msg[256];
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
 *       if LABEL  -> check for '(' => function def, else label
 *       if OPCODE -> handle VAR/SET/GET specially; rest via shape table
 *       if IDENTIFIER -> check for call with namespace "file.func(args)"
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
        fprintf(stderr, "UA Parser: out of memory (initial malloc)\n");
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

        /* ---- Label or function definition ----------------------------- */
        if (cur->type == TOKEN_LABEL) {
            ir = ensure_ir_capacity(ir, count, &capacity);
            if (!ir) { *instruction_count = 0; return NULL; }

            Instruction inst = make_empty_instruction(cur->line, cur->column);
            inst.is_label = 1;
            strncpy(inst.label_name, cur->text, UA_MAX_LABEL_LEN - 1);
            inst.label_name[UA_MAX_LABEL_LEN - 1] = '\0';

            pos++;  /* consume the label token */

            /* Check if this is a function definition: label followed by '(' */
            const Token *next = peek(tokens, pos, token_count);
            if (next->type == TOKEN_LPAREN) {
                inst.is_function = 1;
                inst.param_count = 0;
                pos++;  /* consume '(' */

                /* Parse parameter list:  name1, name2, ... ) */
                const Token *t = peek(tokens, pos, token_count);
                if (t->type != TOKEN_RPAREN) {
                    /* At least one parameter */
                    while (1) {
                        t = peek(tokens, pos, token_count);
                        if (t->type != TOKEN_IDENTIFIER &&
                            t->type != TOKEN_REGISTER) {
                            char msg[256];
                            snprintf(msg, sizeof(msg),
                                     "in function '%s' parameter list: "
                                     "expected parameter name",
                                     inst.label_name);
                            syntax_error(t, msg);
                        }
                        if (inst.param_count >= MAX_FUNC_PARAMS) {
                            char msg[256];
                            snprintf(msg, sizeof(msg),
                                     "function '%s' exceeds maximum of %d "
                                     "parameters",
                                     inst.label_name, MAX_FUNC_PARAMS);
                            syntax_error(t, msg);
                        }
                        strncpy(inst.param_names[inst.param_count],
                                t->text, UA_MAX_LABEL_LEN - 1);
                        inst.param_names[inst.param_count]
                            [UA_MAX_LABEL_LEN - 1] = '\0';
                        inst.param_count++;
                        pos++;

                        t = peek(tokens, pos, token_count);
                        if (t->type == TOKEN_COMMA) {
                            pos++;  /* consume comma, expect more params */
                            continue;
                        }
                        break;
                    }
                }
                /* Expect closing ')' */
                t = peek(tokens, pos, token_count);
                if (t->type != TOKEN_RPAREN) {
                    char msg[256];
                    snprintf(msg, sizeof(msg),
                             "in function '%s': expected ')' after "
                             "parameter list", inst.label_name);
                    syntax_error(t, msg);
                }
                pos++;  /* consume ')' */
            }

            ir[count++] = inst;

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

            Instruction inst = make_empty_instruction(cur->line, cur->column);
            inst.is_label      = 0;
            inst.opcode        = op;

            pos++;  /* consume the opcode token */

            /* =============================================================
             *  VAR name [, initial_value]
             *  Declares a variable.  Operand 0 = label (name),
             *  optionally operand 1 = immediate (initial value).
             * ============================================================= */
            if (op == OP_VAR) {
                const Token *name_tok = peek(tokens, pos, token_count);
                if (name_tok->type != TOKEN_IDENTIFIER &&
                    name_tok->type != TOKEN_LABEL_REF) {
                    syntax_error_expected(name_tok, "variable name",
                                          "after 'VAR'");
                }
                inst.operands[0].type = OPERAND_LABEL_REF;
                strncpy(inst.operands[0].data.label,
                        name_tok->text, UA_MAX_LABEL_LEN - 1);
                inst.operands[0].data.label[UA_MAX_LABEL_LEN - 1] = '\0';
                inst.operand_count = 1;
                pos++;

                /* Optional initial value: VAR name, 42 */
                const Token *maybe_comma = peek(tokens, pos, token_count);
                if (maybe_comma->type == TOKEN_COMMA) {
                    pos++;
                    const Token *val_tok = peek(tokens, pos, token_count);
                    if (val_tok->type == TOKEN_NUMBER) {
                        inst.operands[1].type     = OPERAND_IMMEDIATE;
                        inst.operands[1].data.imm = val_tok->value;
                        inst.operand_count = 2;
                        pos++;
                    } else {
                        syntax_error_expected(val_tok,
                            "initial value (number)",
                            "after 'VAR name,'");
                    }
                }

                goto emit_instruction;
            }

            /* =============================================================
             *  SET name, Rs/imm
             *  Store a register or immediate into a named variable.
             *  Operand 0 = label (var name), operand 1 = reg or imm.
             * ============================================================= */
            if (op == OP_SET) {
                const Token *name_tok = peek(tokens, pos, token_count);
                if (name_tok->type != TOKEN_IDENTIFIER &&
                    name_tok->type != TOKEN_LABEL_REF) {
                    syntax_error_expected(name_tok, "variable name",
                                          "after 'SET'");
                }
                inst.operands[0].type = OPERAND_LABEL_REF;
                strncpy(inst.operands[0].data.label,
                        name_tok->text, UA_MAX_LABEL_LEN - 1);
                inst.operands[0].data.label[UA_MAX_LABEL_LEN - 1] = '\0';
                pos++;

                const Token *comma = peek(tokens, pos, token_count);
                if (comma->type != TOKEN_COMMA) {
                    syntax_error_expected(comma, "','", "after SET name");
                }
                pos++;

                const Token *val_tok = peek(tokens, pos, token_count);
                if (val_tok->type == TOKEN_REGISTER) {
                    inst.operands[1].type     = OPERAND_REGISTER;
                    inst.operands[1].data.reg = (int)val_tok->value;
                } else if (val_tok->type == TOKEN_NUMBER) {
                    inst.operands[1].type     = OPERAND_IMMEDIATE;
                    inst.operands[1].data.imm = val_tok->value;
                } else {
                    syntax_error_expected(val_tok,
                        "register or immediate",
                        "for 'SET name, value'");
                }
                inst.operand_count = 2;
                pos++;

                goto emit_instruction;
            }

            /* =============================================================
             *  GET Rd, name
             *  Load a named variable into a register.
             *  Operand 0 = register, operand 1 = label (var name).
             * ============================================================= */
            if (op == OP_GET) {
                const Token *reg_tok = peek(tokens, pos, token_count);
                if (reg_tok->type != TOKEN_REGISTER) {
                    syntax_error_expected(reg_tok, "register",
                                          "after 'GET'");
                }
                inst.operands[0].type     = OPERAND_REGISTER;
                inst.operands[0].data.reg = (int)reg_tok->value;
                pos++;

                const Token *comma = peek(tokens, pos, token_count);
                if (comma->type != TOKEN_COMMA) {
                    syntax_error_expected(comma, "','", "after GET Rd");
                }
                pos++;

                const Token *name_tok = peek(tokens, pos, token_count);
                if (name_tok->type != TOKEN_IDENTIFIER &&
                    name_tok->type != TOKEN_LABEL_REF) {
                    syntax_error_expected(name_tok, "variable name",
                                          "for 'GET Rd, name'");
                }
                inst.operands[1].type = OPERAND_LABEL_REF;
                strncpy(inst.operands[1].data.label,
                        name_tok->text, UA_MAX_LABEL_LEN - 1);
                inst.operands[1].data.label[UA_MAX_LABEL_LEN - 1] = '\0';
                inst.operand_count = 2;
                pos++;

                goto emit_instruction;
            }

            /* =============================================================
             *  CALL with arguments:  CALL func(arg1, arg2, ...)
             *  The label operand is already consumed by the shape table.
             *  But we need to detect '(' after the label to handle args.
             * ============================================================= */
            if (op == OP_CALL) {
                /* Consume the label/identifier operand */
                const Token *label_tok = peek(tokens, pos, token_count);
                if (is_line_terminator(label_tok)) {
                    syntax_error(label_tok,
                                 "'CALL' expects a label or function name");
                }
                if (label_tok->type != TOKEN_IDENTIFIER &&
                    label_tok->type != TOKEN_LABEL_REF) {
                    syntax_error_expected(label_tok,
                        "label or function name", "after 'CALL'");
                }
                inst.operands[0].type = OPERAND_LABEL_REF;
                strncpy(inst.operands[0].data.label,
                        label_tok->text, UA_MAX_LABEL_LEN - 1);
                inst.operands[0].data.label[UA_MAX_LABEL_LEN - 1] = '\0';
                inst.operand_count = 1;
                pos++;

                /* Check for '(' — function call with arguments */
                const Token *paren = peek(tokens, pos, token_count);
                if (paren->type == TOKEN_LPAREN) {
                    pos++;  /* consume '(' */

                    /* Parse argument list: reg/imm, reg/imm, ... ) */
                    /* Arguments are stored as operands[1], operands[2] via
                     * the function definition's parameter mapping in the
                     * backend.  We encode the argument count using
                     * is_function + param_count on the CALL instruction. */
                    inst.is_function = 1;   /* flag: has args */
                    inst.param_count = 0;

                    const Token *t = peek(tokens, pos, token_count);
                    if (t->type != TOKEN_RPAREN) {
                        while (1) {
                            t = peek(tokens, pos, token_count);
                            if (inst.param_count >= MAX_FUNC_PARAMS) {
                                char msg[256];
                                snprintf(msg, sizeof(msg),
                                    "CALL '%s': too many arguments (max %d)",
                                    inst.operands[0].data.label,
                                    MAX_FUNC_PARAMS);
                                syntax_error(t, msg);
                            }
                            /* Store arg name so backends can handle it */
                            if (t->type == TOKEN_REGISTER) {
                                /* Encode register as "Rn" string */
                                snprintf(
                                    inst.param_names[inst.param_count],
                                    UA_MAX_LABEL_LEN, "R%d",
                                    (int)t->value);
                            } else if (t->type == TOKEN_NUMBER) {
                                /* Encode immediate as "#nnn" string */
                                snprintf(
                                    inst.param_names[inst.param_count],
                                    UA_MAX_LABEL_LEN, "#%lld",
                                    (long long)t->value);
                            } else if (t->type == TOKEN_IDENTIFIER) {
                                /* Variable reference as argument */
                                strncpy(
                                    inst.param_names[inst.param_count],
                                    t->text, UA_MAX_LABEL_LEN - 1);
                                inst.param_names[inst.param_count]
                                    [UA_MAX_LABEL_LEN - 1] = '\0';
                            } else {
                                syntax_error_expected(t,
                                    "register, number, or variable",
                                    "in function argument list");
                            }
                            inst.param_count++;
                            pos++;

                            t = peek(tokens, pos, token_count);
                            if (t->type == TOKEN_COMMA) {
                                pos++;
                                continue;
                            }
                            break;
                        }
                    }
                    t = peek(tokens, pos, token_count);
                    if (t->type != TOKEN_RPAREN) {
                        char msg[256];
                        snprintf(msg, sizeof(msg),
                            "CALL '%s': expected ')' after argument list",
                            inst.operands[0].data.label);
                        syntax_error(t, msg);
                    }
                    pos++;  /* consume ')' */
                }

                goto emit_instruction;
            }

            /* =============================================================
             *  Standard opcode — shape-table driven parsing
             * ============================================================= */
            {
                const OpcodeShape *shape = &OPCODE_SHAPES[op];
                inst.operand_count = shape->count;

                for (int i = 0; i < shape->count; i++) {
                    if (i > 0) {
                        const Token *comma = peek(tokens, pos, token_count);
                        if (comma->type != TOKEN_COMMA) {
                            char ctx[128];
                            snprintf(ctx, sizeof(ctx),
                                     "after operand %d of '%s'",
                                     i, opcode_name(op));
                            syntax_error_expected(comma, "','", ctx);
                        }
                        pos++;
                    }

                    const Token *operand_tok = peek(tokens, pos, token_count);
                    if (is_line_terminator(operand_tok)) {
                        char msg[256];
                        snprintf(msg, sizeof(msg),
                                 "'%s' expects %d operand(s), but only "
                                 "%d given",
                                 opcode_name(op), shape->count, i);
                        syntax_error(operand_tok, msg);
                    }

                    build_operand(operand_tok, shape->shape[i],
                                  opcode_name(op), &inst.operands[i]);
                    pos++;
                }
            }

            /* ------- Emit the instruction ------------------------------- */
            emit_instruction:
            {
                const Token *after = peek(tokens, pos, token_count);
                if (!is_line_terminator(after) && after->type != TOKEN_EOF) {
                    char msg[256];
                    snprintf(msg, sizeof(msg),
                             "unexpected token after '%s' instruction "
                             "(expected end of line)",
                             opcode_name(op));
                    syntax_error(after, msg);
                }
            }

            ir = ensure_ir_capacity(ir, count, &capacity);
            if (!ir) { *instruction_count = 0; return NULL; }
            ir[count++] = inst;

            while (pos < token_count &&
                   (tokens[pos].type == TOKEN_COMMENT ||
                    tokens[pos].type == TOKEN_NEWLINE)) {
                pos++;
            }
            continue;
        }

        /* ---- Identifier — potential func definition or call ------------- */
        if (cur->type == TOKEN_IDENTIFIER) {
            /* Check if this is a function call: identifier followed by '(' */
            const Token *next = peek(tokens, pos + 1, token_count);
            if (next->type == TOKEN_LPAREN) {
                /* Look ahead past matching ')' to see if ':' follows
                 * (function definition) or not (function call).           */
                int is_def = 0;
                {
                    int depth = 1;
                    int scan  = pos + 2;
                    while (scan < token_count && depth > 0) {
                        if (tokens[scan].type == TOKEN_LPAREN)  depth++;
                        if (tokens[scan].type == TOKEN_RPAREN)  depth--;
                        scan++;
                    }
                    if (scan < token_count &&
                        tokens[scan].type == TOKEN_COLON) {
                        is_def = 1;
                    }
                }

                if (is_def) {
                    /* ---- Function definition: ident(params): ---------- */
                    ir = ensure_ir_capacity(ir, count, &capacity);
                    if (!ir) { *instruction_count = 0; return NULL; }

                    Instruction inst = make_empty_instruction(
                        cur->line, cur->column);
                    inst.is_label    = 1;
                    inst.is_function = 1;
                    inst.param_count = 0;
                    strncpy(inst.label_name, cur->text,
                            UA_MAX_LABEL_LEN - 1);
                    inst.label_name[UA_MAX_LABEL_LEN - 1] = '\0';
                    pos += 2;  /* consume identifier + '(' */

                    const Token *t = peek(tokens, pos, token_count);
                    if (t->type != TOKEN_RPAREN) {
                        while (1) {
                            t = peek(tokens, pos, token_count);
                            if (t->type != TOKEN_IDENTIFIER &&
                                t->type != TOKEN_REGISTER) {
                                char msg[256];
                                snprintf(msg, sizeof(msg),
                                         "in function '%s' parameter list: "
                                         "expected parameter name",
                                         inst.label_name);
                                syntax_error(t, msg);
                            }
                            if (inst.param_count >= MAX_FUNC_PARAMS) {
                                char msg[256];
                                snprintf(msg, sizeof(msg),
                                         "function '%s' exceeds maximum "
                                         "of %d parameters",
                                         inst.label_name, MAX_FUNC_PARAMS);
                                syntax_error(t, msg);
                            }
                            strncpy(inst.param_names[inst.param_count],
                                    t->text, UA_MAX_LABEL_LEN - 1);
                            inst.param_names[inst.param_count]
                                [UA_MAX_LABEL_LEN - 1] = '\0';
                            inst.param_count++;
                            pos++;

                            t = peek(tokens, pos, token_count);
                            if (t->type == TOKEN_COMMA) {
                                pos++;
                                continue;
                            }
                            break;
                        }
                    }
                    t = peek(tokens, pos, token_count);
                    if (t->type != TOKEN_RPAREN) {
                        char msg[256];
                        snprintf(msg, sizeof(msg),
                                 "in function '%s': expected ')' after "
                                 "parameter list", inst.label_name);
                        syntax_error(t, msg);
                    }
                    pos++;  /* consume ')' */

                    /* Consume the trailing ':' */
                    t = peek(tokens, pos, token_count);
                    if (t->type == TOKEN_COLON) {
                        pos++;
                    }

                    ir[count++] = inst;

                    while (pos < token_count &&
                           (tokens[pos].type == TOKEN_COMMENT ||
                            tokens[pos].type == TOKEN_NEWLINE)) {
                        pos++;
                    }
                    continue;
                }

                /* ---- Function call: identifier(args) ---- */
                Instruction inst = make_empty_instruction(
                    cur->line, cur->column);
                inst.is_label = 0;
                inst.opcode   = OP_CALL;
                inst.operands[0].type = OPERAND_LABEL_REF;
                strncpy(inst.operands[0].data.label,
                        cur->text, UA_MAX_LABEL_LEN - 1);
                inst.operands[0].data.label[UA_MAX_LABEL_LEN - 1] = '\0';
                inst.operand_count = 1;
                inst.is_function = 1;
                inst.param_count = 0;
                pos += 2;  /* consume identifier + '(' */

                const Token *t = peek(tokens, pos, token_count);
                if (t->type != TOKEN_RPAREN) {
                    while (1) {
                        t = peek(tokens, pos, token_count);
                        if (inst.param_count >= MAX_FUNC_PARAMS) {
                            char msg[256];
                            snprintf(msg, sizeof(msg),
                                "Call '%s': too many arguments (max %d)",
                                inst.operands[0].data.label,
                                MAX_FUNC_PARAMS);
                            syntax_error(t, msg);
                        }
                        if (t->type == TOKEN_REGISTER) {
                            snprintf(inst.param_names[inst.param_count],
                                     UA_MAX_LABEL_LEN, "R%d",
                                     (int)t->value);
                        } else if (t->type == TOKEN_NUMBER) {
                            snprintf(inst.param_names[inst.param_count],
                                     UA_MAX_LABEL_LEN, "#%lld",
                                     (long long)t->value);
                        } else if (t->type == TOKEN_IDENTIFIER) {
                            strncpy(inst.param_names[inst.param_count],
                                    t->text, UA_MAX_LABEL_LEN - 1);
                            inst.param_names[inst.param_count]
                                [UA_MAX_LABEL_LEN - 1] = '\0';
                        } else {
                            syntax_error_expected(t,
                                "register, number, or variable",
                                "in function argument list");
                        }
                        inst.param_count++;
                        pos++;

                        t = peek(tokens, pos, token_count);
                        if (t->type == TOKEN_COMMA) {
                            pos++;
                            continue;
                        }
                        break;
                    }
                }
                t = peek(tokens, pos, token_count);
                if (t->type != TOKEN_RPAREN) {
                    char msg[256];
                    snprintf(msg, sizeof(msg),
                        "Call '%s': expected ')'",
                        inst.operands[0].data.label);
                    syntax_error(t, msg);
                }
                pos++;

                ir = ensure_ir_capacity(ir, count, &capacity);
                if (!ir) { *instruction_count = 0; return NULL; }
                ir[count++] = inst;

                while (pos < token_count &&
                       (tokens[pos].type == TOKEN_COMMENT ||
                        tokens[pos].type == TOKEN_NEWLINE)) {
                    pos++;
                }
                continue;
            }
        }

        /* ---- Anything else is a syntax error -------------------------- */
        syntax_error(cur, "expected an opcode, label, or function call");
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
