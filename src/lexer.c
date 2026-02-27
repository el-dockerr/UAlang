/*
 * =============================================================================
 *  UA - Unified Assembler
 *  Phase 1: Lexer / Tokenizer
 *
 *  File:    lexer.c
 *  Purpose: Implementation of the UA lexical analyzer.
 *
 *  The lexer performs a single left-to-right scan of the source text and
 *  produces a flat array of Token structs.  It recognises:
 *
 *    - Opcodes       (MOV, ADD, SUB, LDI, LOAD, STORE)
 *    - Registers     (R0 .. R15)
 *    - Numeric literals  (decimal, 0x hex, 0b binary)
 *    - Labels        (identifier followed by ':')
 *    - Commas, colons, newlines
 *    - Comments      (';' to end-of-line)
 *
 *  The opcode and register tables are kept deliberately small for Phase 1;
 *  they will be extended once the parser and back-ends are in place.
 *
 *  License: MIT
 * =============================================================================
 */

#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* =========================================================================
 *  Internal constants
 * ========================================================================= */

/* Initial capacity of the token array (grows by doubling). */
#define INITIAL_TOKEN_CAPACITY  128

/* =========================================================================
 *  Opcode table
 * =========================================================================
 *  A simple linear table is perfectly adequate for the handful of mnemonics
 *  we support.  Comparison is case-insensitive.
 * ========================================================================= */
static const char *OPCODES[] = {
    "MOV",
    "ADD",
    "SUB",
    "LDI",
    "LOAD",
    "STORE",
    "NOP",
    "HLT",
    "CMP",
    "JMP",
    "JZ",
    "JNZ",
    "CALL",
    "RET",
    "PUSH",
    "POP",
    "AND",
    "OR",
    "XOR",
    "NOT",
    "SHL",
    "SHR",
    "MUL",
    "DIV",
    "INC",
    "DEC",
    "INT",
    "VAR",
    "SET",
    "GET",
    "LDS",
    "LOADB",
    "STOREB",
    "SYS",
    "JL",
    "JG",
    "BUFFER",
    /* Architecture-specific opcodes */
    "CPUID",
    "RDTSC",
    "BSWAP",
    "PUSHA",
    "POPA",
    "DJNZ",
    "CJNE",
    "SETB",
    "CLR",
    "RETI",
    "WFI",
    "DMB",
    "EBREAK",
    "FENCE",
    NULL                        /* sentinel */
};

/* =========================================================================
 *  Helper: case-insensitive string comparison
 * ========================================================================= */
static int str_casecmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = toupper((unsigned char)*a);
        int cb = toupper((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* =========================================================================
 *  Helper: check if a word is a known opcode
 * ========================================================================= */
static int is_opcode(const char *word)
{
    for (int i = 0; OPCODES[i] != NULL; i++) {
        if (str_casecmp(word, OPCODES[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* =========================================================================
 *  Helper: check if a word is a register (R0-R15)
 *
 *  Returns the register number (0-15) on success, or -1 if not a register.
 * ========================================================================= */
static int parse_register(const char *word)
{
    if (word[0] != 'R' && word[0] != 'r') return -1;
    if (word[1] == '\0')                   return -1;

    /* Rest must be all digits */
    for (int i = 1; word[i] != '\0'; i++) {
        if (!isdigit((unsigned char)word[i])) return -1;
    }

    int num = atoi(word + 1);
    if (num < 0 || num > 15) return -1;

    return num;
}

/* =========================================================================
 *  Helper: parse a numeric literal
 *
 *  Supports:
 *    - Decimal:      123, -42
 *    - Hexadecimal:  0xFF, 0XAB
 *    - Binary:       0b1010, 0B1100
 *
 *  Returns 1 on success (value written to *out), 0 on failure.
 * ========================================================================= */
static int parse_number(const char *text, int64_t *out)
{
    char *end = NULL;

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        /* Hexadecimal */
        *out = (int64_t)strtoll(text, &end, 16);
    } else if (text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
        /* Binary — skip the "0b" prefix manually */
        *out = (int64_t)strtoll(text + 2, &end, 2);
    } else {
        /* Decimal (may have leading '-') */
        *out = (int64_t)strtoll(text, &end, 10);
    }

    /* Success only if the entire string was consumed */
    return (end != NULL && *end == '\0' && end != text) ? 1 : 0;
}

/* =========================================================================
 *  Helper: ensure the token array has room for at least one more entry
 * ========================================================================= */
static Token* ensure_capacity(Token *tokens, int count, int *capacity)
{
    if (count < *capacity) return tokens;

    int new_cap = (*capacity) * 2;
    Token *tmp = (Token *)realloc(tokens, sizeof(Token) * (size_t)new_cap);
    if (!tmp) {
        fprintf(stderr, "UA Lexer: out of memory (realloc failed)\n");
        free(tokens);
        return NULL;
    }
    *capacity = new_cap;
    return tmp;
}

/* =========================================================================
 *  Helper: create and append a token
 * ========================================================================= */
static Token make_token(UaTokenType type, const char *text, int64_t value,
                        int line, int column)
{
    Token t;
    t.type   = type;
    t.value  = value;
    t.line   = line;
    t.column = column;

    /* Safe copy of lexeme */
    size_t len = strlen(text);
    if (len >= UA_MAX_TOKEN_LEN) len = UA_MAX_TOKEN_LEN - 1;
    memcpy(t.text, text, len);
    t.text[len] = '\0';

    return t;
}

/* =========================================================================
 *  token_type_name()  —  human-readable token type strings
 * ========================================================================= */
const char* token_type_name(UaTokenType type)
{
    switch (type) {
        case TOKEN_OPCODE:     return "OPCODE";
        case TOKEN_REGISTER:   return "REGISTER";
        case TOKEN_NUMBER:     return "NUMBER";
        case TOKEN_LABEL:      return "LABEL";
        case TOKEN_LABEL_REF:  return "LABEL_REF";
        case TOKEN_IDENTIFIER: return "IDENTIFIER";
        case TOKEN_STRING:     return "STRING";
        case TOKEN_COMMA:      return "COMMA";
        case TOKEN_COLON:      return "COLON";
        case TOKEN_LPAREN:     return "LPAREN";
        case TOKEN_RPAREN:     return "RPAREN";
        case TOKEN_NEWLINE:    return "NEWLINE";
        case TOKEN_COMMENT:    return "COMMENT";
        case TOKEN_EOF:        return "EOF";
        case TOKEN_UNKNOWN:    return "UNKNOWN";
        default:               return "???";
    }
}

/* =========================================================================
 *  tokenize()  —  main lexer entry point
 *
 *  Strategy:
 *    1. Walk through `source_code` character by character.
 *    2. Skip whitespace (except newlines, which are significant).
 *    3. Dispatch on the current character class:
 *         - ';'          → comment (consume to end-of-line)
 *         - ','          → comma
 *         - '\n'         → newline
 *         - digit / '-'  → numeric literal
 *         - alpha / '_'  → word (opcode, register, label, or identifier)
 *         - otherwise    → unknown
 *    4. After scanning, append TOKEN_EOF.
 * ========================================================================= */
Token* tokenize(const char *source_code, int *token_count)
{
    if (!source_code || !token_count) return NULL;

    int capacity = INITIAL_TOKEN_CAPACITY;
    int count    = 0;

    Token *tokens = (Token *)malloc(sizeof(Token) * (size_t)capacity);
    if (!tokens) {
        fprintf(stderr, "UA Lexer: out of memory (initial malloc)\n");
        *token_count = 0;
        return NULL;
    }

    const char *p   = source_code;
    int line        = 1;
    int col         = 1;

    while (*p != '\0') {

        /* ---- Skip horizontal whitespace (spaces, tabs) ---------------- */
        if (*p == ' ' || *p == '\t' || *p == '\r') {
            if (*p == '\t') col += 4 - ((col - 1) % 4);  /* tab stop */
            else            col++;
            p++;
            continue;
        }

        /* ---- Newline -------------------------------------------------- */
        if (*p == '\n') {
            tokens = ensure_capacity(tokens, count, &capacity);
            if (!tokens) { *token_count = 0; return NULL; }

            tokens[count++] = make_token(TOKEN_NEWLINE, "\\n", 0, line, col);
            p++;
            line++;
            col = 1;
            continue;
        }

        /* ---- Comment (';' to end-of-line) ----------------------------- */
        if (*p == ';') {
            int start_col = col;
            const char *start = p;
            while (*p != '\0' && *p != '\n') {
                p++;
                col++;
            }
            /* Build comment text */
            size_t len = (size_t)(p - start);
            char buf[UA_MAX_TOKEN_LEN];
            if (len >= UA_MAX_TOKEN_LEN) len = UA_MAX_TOKEN_LEN - 1;
            memcpy(buf, start, len);
            buf[len] = '\0';

            tokens = ensure_capacity(tokens, count, &capacity);
            if (!tokens) { *token_count = 0; return NULL; }

            tokens[count++] = make_token(TOKEN_COMMENT, buf, 0,
                                         line, start_col);
            continue;           /* don't consume the '\n' here */
        }

        /* ---- Comma ---------------------------------------------------- */
        if (*p == ',') {
            tokens = ensure_capacity(tokens, count, &capacity);
            if (!tokens) { *token_count = 0; return NULL; }

            tokens[count++] = make_token(TOKEN_COMMA, ",", 0, line, col);
            p++;
            col++;
            continue;
        }

        /* ---- String literal ("...") ---------------------------------- */
        if (*p == '"') {
            int start_col = col;
            p++;  col++;   /* consume opening quote */

            char buf[UA_MAX_TOKEN_LEN];
            size_t bi = 0;

            while (*p != '\0' && *p != '"' && *p != '\n') {
                if (*p == '\\' && *(p + 1) != '\0') {
                    p++;  col++;
                    char esc;
                    switch (*p) {
                        case 'n':  esc = '\n'; break;
                        case 't':  esc = '\t'; break;
                        case 'r':  esc = '\r'; break;
                        case '0':  esc = '\0'; break;
                        case '\\': esc = '\\'; break;
                        case '"':  esc = '"';  break;
                        default:   esc = *p;   break;
                    }
                    if (bi < UA_MAX_TOKEN_LEN - 1) buf[bi++] = esc;
                } else {
                    if (bi < UA_MAX_TOKEN_LEN - 1) buf[bi++] = *p;
                }
                p++;  col++;
            }
            buf[bi] = '\0';

            if (*p == '"') {
                p++;  col++;   /* consume closing quote */
            } else {
                fprintf(stderr, "UA Lexer: warning: unterminated string "
                        "literal at line %d, col %d\n", line, start_col);
            }

            tokens = ensure_capacity(tokens, count, &capacity);
            if (!tokens) { *token_count = 0; return NULL; }

            tokens[count++] = make_token(TOKEN_STRING, buf, 0,
                                         line, start_col);
            continue;
        }

        /* ---- Colon (standalone — labels handled below) ---------------- */
        if (*p == ':') {
            tokens = ensure_capacity(tokens, count, &capacity);
            if (!tokens) { *token_count = 0; return NULL; }

            tokens[count++] = make_token(TOKEN_COLON, ":", 0, line, col);
            p++;
            col++;
            continue;
        }

        /* ---- Left parenthesis ----------------------------------------- */
        if (*p == '(') {
            tokens = ensure_capacity(tokens, count, &capacity);
            if (!tokens) { *token_count = 0; return NULL; }

            tokens[count++] = make_token(TOKEN_LPAREN, "(", 0, line, col);
            p++;
            col++;
            continue;
        }

        /* ---- Right parenthesis ---------------------------------------- */
        if (*p == ')') {
            tokens = ensure_capacity(tokens, count, &capacity);
            if (!tokens) { *token_count = 0; return NULL; }

            tokens[count++] = make_token(TOKEN_RPAREN, ")", 0, line, col);
            p++;
            col++;
            continue;
        }

        /* ---- Numeric literal ------------------------------------------ */
        if (isdigit((unsigned char)*p) ||
            (*p == '-' && isdigit((unsigned char)*(p + 1)))) {

            int start_col = col;
            const char *start = p;

            if (*p == '-') { p++; col++; }      /* consume optional sign */

            /* Hex or binary prefix */
            if (*p == '0' && (*(p+1) == 'x' || *(p+1) == 'X' ||
                              *(p+1) == 'b' || *(p+1) == 'B')) {
                p += 2; col += 2;
            }

            while (isalnum((unsigned char)*p)) { p++; col++; }

            size_t len = (size_t)(p - start);
            char buf[UA_MAX_TOKEN_LEN];
            if (len >= UA_MAX_TOKEN_LEN) len = UA_MAX_TOKEN_LEN - 1;
            memcpy(buf, start, len);
            buf[len] = '\0';

            int64_t val = 0;
            UaTokenType ttype = TOKEN_NUMBER;
            if (!parse_number(buf, &val)) {
                fprintf(stderr, "UA Lexer: warning: invalid number '%s' "
                        "at line %d, col %d\n", buf, line, start_col);
                ttype = TOKEN_UNKNOWN;
            }

            tokens = ensure_capacity(tokens, count, &capacity);
            if (!tokens) { *token_count = 0; return NULL; }

            tokens[count++] = make_token(ttype, buf, val, line, start_col);
            continue;
        }

        /* ---- Word: opcode, register, label, or identifier ------------- */
        if (isalpha((unsigned char)*p) || *p == '_' || *p == '.') {
            int start_col = col;
            const char *start = p;

            while (isalnum((unsigned char)*p) || *p == '_' || *p == '.') {
                p++;
                col++;
            }

            size_t len = (size_t)(p - start);
            char buf[UA_MAX_TOKEN_LEN];
            if (len >= UA_MAX_TOKEN_LEN) len = UA_MAX_TOKEN_LEN - 1;
            memcpy(buf, start, len);
            buf[len] = '\0';

            /* Peek ahead: if the next non-space character is ':', this is
             * a label definition.  We consume the colon as well.
             * BUT: if it's '(' instead, it's a function definition — do NOT
             * consume it here; let the parser handle the parentheses.       */
            const char *peek = p;
            while (*peek == ' ' || *peek == '\t') peek++;

            UaTokenType ttype;
            int64_t   val = 0;

            if (*peek == ':') {
                ttype = TOKEN_LABEL;
                /* Consume any whitespace + the colon */
                while (*p == ' ' || *p == '\t') { p++; col++; }
                p++;  col++;   /* consume ':' */
            } else if (is_opcode(buf)) {
                ttype = TOKEN_OPCODE;
                /* Normalize to uppercase for uniform representation */
                for (size_t i = 0; buf[i]; i++)
                    buf[i] = (char)toupper((unsigned char)buf[i]);
            } else {
                int reg = parse_register(buf);
                if (reg >= 0) {
                    ttype = TOKEN_REGISTER;
                    val   = reg;
                    /* Normalize: R0, R1, ... */
                    buf[0] = 'R';
                } else {
                    /* Could be a label reference or a future directive */
                    ttype = TOKEN_IDENTIFIER;
                }
            }

            tokens = ensure_capacity(tokens, count, &capacity);
            if (!tokens) { *token_count = 0; return NULL; }

            tokens[count++] = make_token(ttype, buf, val, line, start_col);
            continue;
        }

        /* ---- Unknown character ---------------------------------------- */
        {
            char buf[2] = { *p, '\0' };
            fprintf(stderr, "UA Lexer: warning: unknown character '%c' "
                    "(0x%02X) at line %d, col %d\n",
                    *p, (unsigned char)*p, line, col);

            tokens = ensure_capacity(tokens, count, &capacity);
            if (!tokens) { *token_count = 0; return NULL; }

            tokens[count++] = make_token(TOKEN_UNKNOWN, buf, 0, line, col);
            p++;
            col++;
        }
    }

    /* ---- Append EOF sentinel ------------------------------------------ */
    tokens = ensure_capacity(tokens, count, &capacity);
    if (!tokens) { *token_count = 0; return NULL; }

    tokens[count++] = make_token(TOKEN_EOF, "<EOF>", 0, line, col);

    *token_count = count;
    return tokens;
}
