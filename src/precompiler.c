/*
 * =============================================================================
 *  UA - Unified Assembler
 *  Precompiler (Preprocessor) — Implementation
 *
 *  File:    precompiler.c
 *  Purpose: Evaluate @-directives before lexing.
 *
 *  ┌──────────────────────────────────────────────────────────────────────────┐
 *  │  Directive            Action                                           │
 *  │  ──────────────────── ──────────────────────────────────────────────    │
 *  │  @IF_ARCH <arch>      Push conditional: active when -arch matches      │
 *  │  @IF_SYS  <sys>       Push conditional: active when -sys  matches      │
 *  │  @ENDIF               Pop one conditional level                        │
 *  │  @IMPORT  <path>      Include file (skipped if already imported)       │
 *  │  @DUMMY   [message]   Emit a diagnostic stub marker to stderr          │
 *  │  @ARCH_ONLY <a>,<b>   Abort unless -arch matches at least one entry    │
 *  │  @SYS_ONLY  <s>,<t>   Abort unless -sys  matches at least one entry    │
 *  │  @DEFINE <NAME> <VAL> Define a text macro for token replacement        │
 *  │  @ORG <address>       Set origin address for subsequent code           │
 *  │                                                                        │
 *  │  Processing order:                                                     │
 *  │    1. Line-by-line scan of the source                                  │
 *  │    2. Conditional nesting tracked with active_depth / total_depth      │
 *  │    3. @IMPORT triggers recursive preprocessing of the imported file    │
 *  │    4. Blank lines emitted for directives to preserve line numbering    │
 *  └──────────────────────────────────────────────────────────────────────────┘
 *
 *  License: MIT
 * =============================================================================
 */

#include "precompiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 *  Compile-time limits
 * ========================================================================= */
#define PP_MAX_COND_DEPTH     64    /* Max nesting of @IF blocks             */
#define PP_MAX_IMPORT_DEPTH   16    /* Max recursive @IMPORT depth           */
#define PP_MAX_IMPORTS        256   /* Max unique imported files              */
#define PP_MAX_PATH_LEN       1024  /* Max file path length                  */
#define PP_MAX_DIRECTIVE_LEN  32    /* Max directive keyword length           */
#define PP_INITIAL_BUF_CAP    4096  /* Initial output-buffer capacity        */
#define PP_MAX_DEFINES        512   /* Max @DEFINE macro entries             */
#define PP_MAX_DEFINE_NAME    64    /* Max macro name length                 */
#define PP_MAX_DEFINE_VALUE   64    /* Max macro value length                */

/* =========================================================================
 *  Dynamic string buffer
 *
 *  Grows by doubling.  All append operations return 0 on success, -1 on
 *  out-of-memory.  The caller detaches the data pointer via strbuf_detach()
 *  and later free()s it.
 * ========================================================================= */
typedef struct {
    char *data;
    int   size;
    int   capacity;
} StrBuf;

static int strbuf_init(StrBuf *sb)
{
    sb->data = (char *)malloc(PP_INITIAL_BUF_CAP);
    if (!sb->data) return -1;
    sb->size     = 0;
    sb->capacity = PP_INITIAL_BUF_CAP;
    return 0;
}

static int strbuf_grow(StrBuf *sb, int needed)
{
    if (sb->size + needed <= sb->capacity) return 0;
    int new_cap = sb->capacity;
    while (new_cap < sb->size + needed) new_cap *= 2;
    char *p = (char *)realloc(sb->data, (size_t)new_cap);
    if (!p) return -1;
    sb->data     = p;
    sb->capacity = new_cap;
    return 0;
}

static int strbuf_append(StrBuf *sb, const char *s, int len)
{
    if (len <= 0) return 0;
    if (strbuf_grow(sb, len) != 0) return -1;
    memcpy(sb->data + sb->size, s, (size_t)len);
    sb->size += len;
    return 0;
}

static int strbuf_append_char(StrBuf *sb, char c)
{
    return strbuf_append(sb, &c, 1);
}

static void strbuf_free(StrBuf *sb)
{
    free(sb->data);
    sb->data     = NULL;
    sb->size     = 0;
    sb->capacity = 0;
}

/* =========================================================================
 *  Portable helpers (pure C99 — no POSIX dependency)
 * ========================================================================= */

/* Case-insensitive string compare */
static int pp_casecmp(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* Portable strdup (strdup is POSIX, not C99) */
static char* pp_strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char  *dup = (char *)malloc(len);
    if (dup) memcpy(dup, s, len);
    return dup;
}

/* =========================================================================
 *  Path utilities
 * ========================================================================= */

/* Replace every backslash with a forward slash (in place). */
static void pp_normalize_seps(char *path)
{
    for (char *p = path; *p; p++) {
        if (*p == '\\') *p = '/';
    }
}

/* Is the path absolute?  (Unix '/' or Windows drive letter 'C:\') */
static int pp_is_absolute(const char *path)
{
    if (!path || !*path) return 0;
    if (path[0] == '/' || path[0] == '\\') return 1;
    if (((path[0] >= 'A' && path[0] <= 'Z') ||
         (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':')
        return 1;
    return 0;
}

/* Resolve *relative* against *base_dir*.  Writes to *out*.  Returns 0. */
static int pp_resolve_path(const char *base_dir, const char *relative,
                           char *out, int out_size)
{
    if (pp_is_absolute(relative)) {
        if ((int)strlen(relative) >= out_size) return -1;
        strcpy(out, relative);
    } else {
        int blen = (int)strlen(base_dir);
        int rlen = (int)strlen(relative);
        if (blen + 1 + rlen + 1 > out_size) return -1;
        memcpy(out, base_dir, (size_t)blen);
        if (blen > 0 && base_dir[blen - 1] != '/' &&
            base_dir[blen - 1] != '\\') {
            out[blen] = '/';
            memcpy(out + blen + 1, relative, (size_t)rlen + 1);
        } else {
            memcpy(out + blen, relative, (size_t)rlen + 1);
        }
    }
    pp_normalize_seps(out);
    return 0;
}

/* Extract the directory part of a path into *dir*. */
static void pp_extract_dir(const char *path, char *dir, int dir_size)
{
    int len      = (int)strlen(path);
    int last_sep = -1;
    for (int i = 0; i < len; i++) {
        if (path[i] == '/' || path[i] == '\\') last_sep = i;
    }
    if (last_sep < 0) {
        /* No separator — current directory */
        if (dir_size >= 2) { dir[0] = '.'; dir[1] = '\0'; }
        else if (dir_size >= 1)            dir[0] = '\0';
    } else {
        int copy = last_sep + 1;               /* include the separator */
        if (copy >= dir_size) copy = dir_size - 1;
        memcpy(dir, path, (size_t)copy);
        dir[copy] = '\0';
    }
}

/* =========================================================================
 *  Read an entire file into a heap-allocated string.
 *  Returns NULL on failure (diagnostic printed to stderr).
 * ========================================================================= */
static char* pp_read_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "[Precompiler] Error: cannot open '%s': ", path);
        perror(NULL);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fprintf(stderr, "[Precompiler] Error: fseek failed on '%s'\n", path);
        fclose(fp);
        return NULL;
    }
    long length = ftell(fp);
    if (length < 0) {
        fprintf(stderr, "[Precompiler] Error: ftell failed on '%s'\n", path);
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    char *buf = (char *)malloc((size_t)length + 1);
    if (!buf) {
        fprintf(stderr, "[Precompiler] Error: out of memory reading '%s'\n",
                path);
        fclose(fp);
        return NULL;
    }
    size_t nread = fread(buf, 1, (size_t)length, fp);
    buf[nread]   = '\0';
    fclose(fp);
    return buf;
}

/* =========================================================================
 *  Macro definition table  (@DEFINE name value)
 *
 *  Shared across the PPState so that macros defined in imported files
 *  are visible to the importer.
 * ========================================================================= */
typedef struct {
    char name[PP_MAX_DEFINE_NAME];
    char value[PP_MAX_DEFINE_VALUE];
} PPMacro;

typedef struct {
    PPMacro entries[PP_MAX_DEFINES];
    int     count;
} PPMacroTable;

/* Forward declarations (defined later, needed by pp_expand_macros) */
static int pp_is_ident_char(char c);
static int pp_is_ident_start(char c);

static void pp_macro_init(PPMacroTable *mt) { mt->count = 0; }

static int pp_macro_add(PPMacroTable *mt, const char *name, int nlen,
                        const char *value, int vlen,
                        const char *filename, int line_num)
{
    if (mt->count >= PP_MAX_DEFINES) {
        fprintf(stderr,
                "[Precompiler] %s:%d: @DEFINE limit exceeded (max %d)\n",
                filename, line_num, PP_MAX_DEFINES);
        return -1;
    }
    if (nlen >= PP_MAX_DEFINE_NAME) nlen = PP_MAX_DEFINE_NAME - 1;
    if (vlen >= PP_MAX_DEFINE_VALUE) vlen = PP_MAX_DEFINE_VALUE - 1;
    memcpy(mt->entries[mt->count].name,  name,  (size_t)nlen);
    mt->entries[mt->count].name[nlen]   = '\0';
    memcpy(mt->entries[mt->count].value, value, (size_t)vlen);
    mt->entries[mt->count].value[vlen]  = '\0';
    mt->count++;
    return 0;
}

/* Perform token-boundary-aware macro expansion on a single line.
 * Writes the expanded line into *out*.  Returns 0 on success, -1 on OOM.
 *
 * A macro NAME is replaced only when it appears as a whole token:
 *   - preceded by start-of-string or a non-identifier character
 *   - followed by end-of-string or a non-identifier character
 * This prevents replacing partial words (e.g., "TMOD_VAL" when "TMOD" is defined).
 */
static int pp_expand_macros(const PPMacroTable *mt,
                            const char *line, int line_len,
                            StrBuf *out)
{
    if (mt->count == 0) {
        /* Fast path: no macros defined */
        return strbuf_append(out, line, line_len);
    }

    const char *p   = line;
    const char *end = line + line_len;

    while (p < end) {
        /* If current character can start an identifier, try matching macros */
        if (pp_is_ident_start(*p)) {
            const char *id_start = p;
            while (p < end && pp_is_ident_char(*p)) p++;
            int id_len = (int)(p - id_start);

            /* Search macro table for a match */
            int replaced = 0;
            for (int m = 0; m < mt->count; m++) {
                int nlen = (int)strlen(mt->entries[m].name);
                if (nlen == id_len &&
                    memcmp(mt->entries[m].name, id_start, (size_t)id_len) == 0) {
                    /* Match — emit the replacement value */
                    int vlen = (int)strlen(mt->entries[m].value);
                    if (strbuf_append(out, mt->entries[m].value, vlen) != 0)
                        return -1;
                    replaced = 1;
                    break;
                }
            }
            if (!replaced) {
                /* No match — emit the original identifier */
                if (strbuf_append(out, id_start, id_len) != 0)
                    return -1;
            }
        } else {
            /* Non-identifier character — emit as-is */
            if (strbuf_append_char(out, *p) != 0) return -1;
            p++;
        }
    }
    return 0;
}

/* =========================================================================
 *  Import de-duplication state  (shared across recursive calls)
 * ========================================================================= */
typedef struct {
    const char *arch;                           /* -arch value              */
    const char *sys;                            /* -sys  value  (or NULL)   */
    const char *exe_dir;                        /* compiler executable dir  */
    char       *imported[PP_MAX_IMPORTS];       /* normalised import paths  */
    int         import_count;
    PPMacroTable macros;                        /* @DEFINE table            */
} PPState;

static void pp_state_init(PPState *st, const char *arch, const char *sys,
                          const char *exe_dir)
{
    st->arch         = arch;
    st->sys          = sys;
    st->exe_dir      = exe_dir;
    st->import_count = 0;
    pp_macro_init(&st->macros);
}

static void pp_state_free(PPState *st)
{
    for (int i = 0; i < st->import_count; i++)
        free(st->imported[i]);
    st->import_count = 0;
}

static int pp_was_imported(const PPState *st, const char *path)
{
    for (int i = 0; i < st->import_count; i++) {
        if (strcmp(st->imported[i], path) == 0) return 1;
    }
    return 0;
}

static int pp_mark_imported(PPState *st, const char *path)
{
    if (st->import_count >= PP_MAX_IMPORTS) {
        fprintf(stderr,
                "[Precompiler] Error: import limit exceeded (max %d files)\n",
                PP_MAX_IMPORTS);
        return -1;
    }
    st->imported[st->import_count] = pp_strdup(path);
    if (!st->imported[st->import_count]) {
        fprintf(stderr, "[Precompiler] Error: out of memory\n");
        return -1;
    }
    st->import_count++;
    return 0;
}

/* =========================================================================
 *  Namespace prefixing for @IMPORT
 *
 *  When a file is imported, all of its labels (and variable definitions)
 *  are scoped under a namespace derived from the import filename.
 *  E.g., importing "math.ua" makes its label "add:" accessible as
 *  "math.add" and its function "sum(x,y):" become "math.sum(x,y)".
 *
 *  Algorithm:
 *    Pass 1: Collect all label definitions from the preprocessed text.
 *    Pass 2: Replace every standalone occurrence of a collected label
 *            name with "prefix.name" (unless already qualified with .).
 * ========================================================================= */

#define PP_MAX_NS_LABELS 256    /* Max labels per imported file              */
#define PP_MAX_NS_NAME   128    /* Max identifier length in namespace logic  */

typedef struct {
    char names[PP_MAX_NS_LABELS][PP_MAX_NS_NAME];
    int  count;
} NSLabelTable;

/* Is 'c' a valid identifier character?  (letter, digit, underscore) */
static int pp_is_ident_char(char c)
{
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '_';
}

/* Can 'c' start an identifier?  (letter, underscore) */
static int pp_is_ident_start(char c)
{
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           c == '_';
}

/* Extract basename from a file path (without extension).
 * E.g., "lib/math.ua" -> "math", "helpers" -> "helpers" */
static void pp_extract_basename(const char *path,
                                char *basename, int size)
{
    /* Find the last path separator */
    const char *p        = path;
    const char *last_sep = NULL;
    while (*p) {
        if (*p == '/' || *p == '\\') last_sep = p;
        p++;
    }
    const char *name = last_sep ? last_sep + 1 : path;

    /* Find the last dot (extension separator) */
    const char *dot = NULL;
    p = name;
    while (*p) {
        if (*p == '.') dot = p;
        p++;
    }

    int len = dot ? (int)(dot - name) : (int)strlen(name);
    if (len >= size) len = size - 1;
    if (len <= 0)    len = 0;
    memcpy(basename, name, (size_t)len);
    basename[len] = '\0';
}

/* Add a label name to the namespace table (de-duplicated). */
static void ns_add_label(NSLabelTable *tbl, const char *name, int len)
{
    if (tbl->count >= PP_MAX_NS_LABELS) return;
    if (len <= 0 || len >= PP_MAX_NS_NAME) return;

    /* Check for duplicates */
    for (int i = 0; i < tbl->count; i++) {
        if ((int)strlen(tbl->names[i]) == len &&
            memcmp(tbl->names[i], name, (size_t)len) == 0)
            return;
    }
    memcpy(tbl->names[tbl->count], name, (size_t)len);
    tbl->names[tbl->count][len] = '\0';
    tbl->count++;
}

/* Check if an identifier (given as pointer + length) is in the table. */
static int ns_is_label(const NSLabelTable *tbl, const char *name, int len)
{
    for (int i = 0; i < tbl->count; i++) {
        if ((int)strlen(tbl->names[i]) == len &&
            memcmp(tbl->names[i], name, (size_t)len) == 0)
            return 1;
    }
    return 0;
}

/* Pass 1: Collect all label definitions from preprocessed text.
 *
 * A label definition is a line whose first non-whitespace content is
 * an identifier followed (possibly after whitespace) by ':' or '('.
 * This catches both plain labels (start:) and function labels (add(x,y):).
 */
static void ns_collect_labels(const char *text, NSLabelTable *tbl)
{
    tbl->count = 0;
    const char *p = text;

    while (*p) {
        const char *line = p;
        while (*p && *p != '\n') p++;
        int line_len = (int)(p - line);
        if (*p == '\n') p++;

        /* Skip leading whitespace */
        const char *s = line;
        while (s < line + line_len && (*s == ' ' || *s == '\t')) s++;

        /* Check: identifier at start of trimmed line */
        if (s < line + line_len && pp_is_ident_start(*s)) {
            const char *id_start = s;
            while (s < line + line_len && pp_is_ident_char(*s)) s++;
            int id_len = (int)(s - id_start);

            /* Skip optional whitespace after identifier */
            while (s < line + line_len && (*s == ' ' || *s == '\t')) s++;

            /* If followed by ':' or '(' → label definition */
            if (s < line + line_len && (*s == ':' || *s == '(')) {
                ns_add_label(tbl, id_start, id_len);
            }
            /* Check for VAR declaration: "VAR name" -> collect name */
            if (id_len == 3 &&
                (id_start[0] == 'V' || id_start[0] == 'v') &&
                (id_start[1] == 'A' || id_start[1] == 'a') &&
                (id_start[2] == 'R' || id_start[2] == 'r')) {
                /* Skip whitespace after VAR */
                while (s < line + line_len &&
                       (*s == ' ' || *s == '\t')) s++;
                if (s < line + line_len && pp_is_ident_start(*s)) {
                    const char *var_start = s;
                    while (s < line + line_len &&
                           pp_is_ident_char(*s)) s++;
                    int var_len = (int)(s - var_start);
                    ns_add_label(tbl, var_start, var_len);
                }
            }        }
    }
}

/* Pass 2: Re-emit text with all label references prefixed.
 *
 * For every identifier in the text that matches a collected label name
 * and is NOT already namespace-qualified (preceded by '.') and NOT
 * part of a numeric literal (preceded by a digit), emit "prefix.name"
 * instead of "name".
 *
 * Returns 0 on success, -1 on memory error. */
static int ns_apply_prefix(const char *text, const char *prefix,
                           const NSLabelTable *tbl, StrBuf *out)
{
    int prefix_len = (int)strlen(prefix);
    const char *p  = text;

    while (*p) {
        /* If current char cannot start an identifier, emit it as-is */
        if (!pp_is_ident_start(*p)) {
            if (strbuf_append_char(out, *p) != 0) return -1;
            p++;
            continue;
        }

        /* Collect the full identifier */
        const char *id_start = p;
        while (*p && pp_is_ident_char(*p)) p++;
        int id_len = (int)(p - id_start);

        /* Determine if the identifier is already qualified or is part
         * of a hex/numeric literal (e.g., 0xFF → 'x' starts ident). */
        int skip_prefix = 0;
        if (id_start > text) {
            char prev = *(id_start - 1);
            if (prev == '.' || (prev >= '0' && prev <= '9'))
                skip_prefix = 1;
        }

        /* Check if the identifier matches a known label */
        if (!skip_prefix && ns_is_label(tbl, id_start, id_len)) {
            if (strbuf_append(out, prefix, prefix_len) != 0) return -1;
            if (strbuf_append_char(out, '.') != 0) return -1;
        }

        /* Emit the identifier itself */
        if (strbuf_append(out, id_start, id_len) != 0) return -1;
    }

    return 0;
}

/* =========================================================================
 *  Internal preprocessing worker  (recursive for @IMPORT)
 *
 *  Walks the source line-by-line, evaluates directives, and appends
 *  surviving lines to *output*.
 *
 *  Conditional nesting uses a two-counter scheme:
 *    total_depth   — incremented on every @IF_*, decremented on @ENDIF
 *    active_depth  — tracks how many nested levels are *active*
 *  A line is in an active region iff  active_depth == total_depth.
 *
 *  Returns 0 on success, -1 on error (diagnostic already printed).
 * ========================================================================= */
static int pp_process(const char *source,
                      const char *filename,
                      PPState    *state,
                      const char *base_dir,
                      int         depth,
                      StrBuf     *output,
                      StrBuf     *deferred)
{
    if (depth > PP_MAX_IMPORT_DEPTH) {
        fprintf(stderr,
                "[Precompiler] Error: @IMPORT nesting exceeds %d levels "
                "(circular import?)\n", PP_MAX_IMPORT_DEPTH);
        return -1;
    }

    int total_depth  = 0;   /* total @IF nesting          */
    int active_depth = 0;   /* nesting levels with TRUE   */
    int line_num     = 1;
    const char *p    = source;

    while (*p) {
        /* ---- Extract one source line ---------------------------------- */
        const char *line_start = p;
        while (*p && *p != '\n') p++;
        int line_len = (int)(p - line_start);
        /* Strip trailing \r (CRLF line endings from binary-mode read) */
        if (line_len > 0 && line_start[line_len - 1] == '\r')
            line_len--;
        if (*p == '\n') p++;                     /* consume the newline */

        /* ---- Skip leading whitespace ---------------------------------- */
        const char *trimmed = line_start;
        while (trimmed < line_start + line_len &&
               (*trimmed == ' ' || *trimmed == '\t'))
            trimmed++;
        int trimmed_len = (int)(line_start + line_len - trimmed);

        int is_active = (active_depth == total_depth);

        /* ================================================================
         *  @-directive?
         * ============================================================== */
        if (trimmed_len > 0 && *trimmed == '@') {

            /* ---- Parse directive keyword ------------------------------ */
            const char *dname     = trimmed + 1;          /* skip '@' */
            const char *dname_end = dname;
            while (dname_end < line_start + line_len &&
                   *dname_end != ' ' && *dname_end != '\t')
                dname_end++;
            int dlen = (int)(dname_end - dname);

            char directive[PP_MAX_DIRECTIVE_LEN];
            if (dlen >= PP_MAX_DIRECTIVE_LEN) dlen = PP_MAX_DIRECTIVE_LEN - 1;
            memcpy(directive, dname, (size_t)dlen);
            directive[dlen] = '\0';

            /* ---- Skip whitespace after keyword → start of argument ---- */
            const char *arg = dname_end;
            while (arg < line_start + line_len &&
                   (*arg == ' ' || *arg == '\t'))
                arg++;
            const char *line_end = line_start + line_len;

            /* ============================================================
             *  @IF_ARCH <arch>
             * ========================================================== */
            if (pp_casecmp(directive, "IF_ARCH") == 0) {

                /* Extract architecture token */
                const char *a = arg;
                while (a < line_end && *a != ' ' && *a != '\t' && *a != ';')
                    a++;
                int alen = (int)(a - arg);
                if (alen <= 0) {
                    fprintf(stderr,
                            "[Precompiler] %s:%d: @IF_ARCH requires "
                            "an architecture name\n", filename, line_num);
                    return -1;
                }
                char arch_tok[64];
                if (alen >= 64) alen = 63;
                memcpy(arch_tok, arg, (size_t)alen);
                arch_tok[alen] = '\0';

                total_depth++;
                if (total_depth > PP_MAX_COND_DEPTH) {
                    fprintf(stderr,
                            "[Precompiler] %s:%d: conditional nesting "
                            "exceeds %d levels\n",
                            filename, line_num, PP_MAX_COND_DEPTH);
                    return -1;
                }
                if (is_active && pp_casecmp(arch_tok, state->arch) == 0)
                    active_depth++;

                /* Emit blank line to preserve line numbering */
                if (strbuf_append_char(output, '\n') != 0) return -1;
            }
            /* ============================================================
             *  @IF_SYS <system>
             * ========================================================== */
            else if (pp_casecmp(directive, "IF_SYS") == 0) {

                const char *a = arg;
                while (a < line_end && *a != ' ' && *a != '\t' && *a != ';')
                    a++;
                int slen = (int)(a - arg);
                if (slen <= 0) {
                    fprintf(stderr,
                            "[Precompiler] %s:%d: @IF_SYS requires "
                            "a system name\n", filename, line_num);
                    return -1;
                }
                char sys_tok[64];
                if (slen >= 64) slen = 63;
                memcpy(sys_tok, arg, (size_t)slen);
                sys_tok[slen] = '\0';

                total_depth++;
                if (total_depth > PP_MAX_COND_DEPTH) {
                    fprintf(stderr,
                            "[Precompiler] %s:%d: conditional nesting "
                            "exceeds %d levels\n",
                            filename, line_num, PP_MAX_COND_DEPTH);
                    return -1;
                }
                if (is_active &&
                    state->sys != NULL &&
                    pp_casecmp(sys_tok, state->sys) == 0)
                    active_depth++;

                if (strbuf_append_char(output, '\n') != 0) return -1;
            }
            /* ============================================================
             *  @ENDIF
             * ========================================================== */
            else if (pp_casecmp(directive, "ENDIF") == 0) {

                if (total_depth == 0) {
                    fprintf(stderr,
                            "[Precompiler] %s:%d: @ENDIF without matching "
                            "@IF_ARCH or @IF_SYS\n", filename, line_num);
                    return -1;
                }
                if (active_depth == total_depth)
                    active_depth--;
                total_depth--;

                if (strbuf_append_char(output, '\n') != 0) return -1;
            }
            /* ============================================================
             *  Directives processed only in ACTIVE regions
             * ========================================================== */
            else if (is_active) {

                /* ---- @IMPORT <path> ----------------------------------- */
                if (pp_casecmp(directive, "IMPORT") == 0) {

                    /* Parse path — support quoted and unquoted */
                    char import_path[PP_MAX_PATH_LEN];
                    int  plen = 0;

                    if (arg < line_end && *arg == '"') {
                        /* Quoted: @IMPORT "some/path.ua" */
                        const char *qs = arg + 1;
                        const char *qe = qs;
                        while (qe < line_end && *qe != '"') qe++;
                        if (qe >= line_end) {
                            fprintf(stderr,
                                    "[Precompiler] %s:%d: unterminated "
                                    "quote in @IMPORT\n",
                                    filename, line_num);
                            return -1;
                        }
                        plen = (int)(qe - qs);
                        if (plen >= PP_MAX_PATH_LEN)
                            plen = PP_MAX_PATH_LEN - 1;
                        memcpy(import_path, qs, (size_t)plen);
                    } else {
                        /* Unquoted: @IMPORT some/path.ua */
                        const char *pe = arg;
                        while (pe < line_end &&
                               *pe != ' ' && *pe != '\t' && *pe != ';')
                            pe++;
                        plen = (int)(pe - arg);
                        if (plen >= PP_MAX_PATH_LEN)
                            plen = PP_MAX_PATH_LEN - 1;
                        memcpy(import_path, arg, (size_t)plen);
                    }
                    import_path[plen] = '\0';

                    if (plen == 0) {
                        fprintf(stderr,
                                "[Precompiler] %s:%d: @IMPORT requires "
                                "a file path\n", filename, line_num);
                        return -1;
                    }

                    /* ---- std_* / hw_* library resolution -----
                     * If the import name starts with "std_" or "hw_"
                     * and has no path separator, resolve it as a
                     * standard library from <exe_dir>/lib/<name>.ua  */
                    int is_std_lib = 0;
                    char std_path[PP_MAX_PATH_LEN];
                    if (plen >= 3 &&
                        ((import_path[0] == 's' &&
                          import_path[1] == 't' &&
                          import_path[2] == 'd' &&
                          import_path[3] == '_') ||
                         (import_path[0] == 'h' &&
                          import_path[1] == 'w' &&
                          import_path[2] == '_'))) {
                        /* Check no path separators in name */
                        int has_sep = 0;
                        for (int ci = 0; ci < plen; ci++) {
                            if (import_path[ci] == '/' ||
                                import_path[ci] == '\\') {
                                has_sep = 1;
                                break;
                            }
                        }
                        if (!has_sep && state->exe_dir != NULL) {
                            /* Build <exe_dir>/lib/<name>.ua */
                            int elen = (int)strlen(state->exe_dir);
                            /* Append .ua if not already present */
                            const char *ext = "";
                            if (plen < 3 ||
                                import_path[plen-3] != '.' ||
                                import_path[plen-2] != 'u' ||
                                import_path[plen-1] != 'a') {
                                ext = ".ua";
                            }
                            int needed = elen + 5 + plen + (int)strlen(ext) + 1;
                            if (needed < PP_MAX_PATH_LEN) {
                                snprintf(std_path, (size_t)(PP_MAX_PATH_LEN - 1),
                                         "%s/lib/%.*s%s",
                                         state->exe_dir,
                                         (int)(PP_MAX_PATH_LEN - elen - 6),
                                         import_path, ext);
                                std_path[PP_MAX_PATH_LEN - 1] = '\0';
                                pp_normalize_seps(std_path);
                                is_std_lib = 1;
                            }
                        }
                    }

                    /* Resolve to a normalised full path */
                    char resolved[PP_MAX_PATH_LEN];
                    if (is_std_lib) {
                        /* Use the std library path directly */
                        strncpy(resolved, std_path, PP_MAX_PATH_LEN - 1);
                        resolved[PP_MAX_PATH_LEN - 1] = '\0';
                    } else if (pp_resolve_path(base_dir, import_path,
                                        resolved, PP_MAX_PATH_LEN) != 0) {
                        fprintf(stderr,
                                "[Precompiler] %s:%d: import path "
                                "too long\n", filename, line_num);
                        return -1;
                    }

                    /* Import the file (only if not already imported) */
                    if (!pp_was_imported(state, resolved)) {
                        if (pp_mark_imported(state, resolved) != 0)
                            return -1;

                        char *imp_src = pp_read_file(resolved);
                        if (!imp_src) {
                            fprintf(stderr,
                                    "[Precompiler] %s:%d: failed to import "
                                    "'%s'\n", filename, line_num, import_path);
                            return -1;
                        }

                        /* Compute base dir for the imported file */
                        char imp_dir[PP_MAX_PATH_LEN];
                        pp_extract_dir(resolved, imp_dir, PP_MAX_PATH_LEN);

                        fprintf(stderr,
                                "[Precompiler] Importing '%s'\n", resolved);

                        /* ---- Namespace prefixing ----------------------
                         * Process the imported file into a temporary
                         * buffer, then apply namespace prefix to all
                         * label definitions and references. */
                        StrBuf imp_out;
                        if (strbuf_init(&imp_out) != 0) {
                            free(imp_src);
                            return -1;
                        }

                        int rc = pp_process(imp_src, resolved, state,
                                            imp_dir, depth + 1, &imp_out,
                                            NULL);
                        free(imp_src);
                        if (rc != 0) {
                            strbuf_free(&imp_out);
                            return rc;
                        }

                        /* Null-terminate the temp buffer for scanning */
                        if (strbuf_append_char(&imp_out, '\0') != 0) {
                            strbuf_free(&imp_out);
                            return -1;
                        }

                        /* Extract basename for namespace prefix */
                        char ns_prefix[PP_MAX_NS_NAME];
                        pp_extract_basename(import_path,
                                            ns_prefix,
                                            (int)sizeof(ns_prefix));

                        /* Collect label definitions and apply prefix */
                        NSLabelTable labels;
                        ns_collect_labels(imp_out.data, &labels);

                        /* At depth 0, imported code is deferred to run
                         * after the main program so that the PE/ELF
                         * entry point lands on the first user instruction. */
                        StrBuf *import_target =
                            (depth == 0 && deferred) ? deferred : output;

                        if (labels.count > 0) {
                            /* Apply namespace prefix to all refs */
                            if (ns_apply_prefix(imp_out.data,
                                                ns_prefix,
                                                &labels, import_target) != 0) {
                                strbuf_free(&imp_out);
                                return -1;
                            }
                        } else {
                            /* No labels — append as-is (exclude NUL) */
                            if (strbuf_append(import_target, imp_out.data,
                                              imp_out.size - 1) != 0) {
                                strbuf_free(&imp_out);
                                return -1;
                            }
                        }
                        strbuf_free(&imp_out);
                    } else {
                        fprintf(stderr,
                                "[Precompiler] %s:%d: '%s' already imported "
                                "— skipped\n", filename, line_num,
                                import_path);
                    }

                    /* Blank line for the @IMPORT directive itself */
                    if (strbuf_append_char(output, '\n') != 0) return -1;
                }
                /* ---- @ARCH_ONLY <arch1>,<arch2>,... ------------------- */
                else if (pp_casecmp(directive, "ARCH_ONLY") == 0) {

                    if (arg >= line_end || *arg == ';') {
                        fprintf(stderr,
                                "[Precompiler] %s:%d: @ARCH_ONLY requires "
                                "at least one architecture name\n",
                                filename, line_num);
                        return -1;
                    }

                    /* Walk a comma-separated list of arch names */
                    int found = 0;
                    const char *cur = arg;
                    while (cur < line_end && *cur != ';') {
                        /* Skip leading whitespace */
                        while (cur < line_end &&
                               (*cur == ' ' || *cur == '\t')) cur++;
                        if (cur >= line_end || *cur == ';') break;

                        /* Extract one token (until comma, space, or end) */
                        const char *tok_start = cur;
                        while (cur < line_end &&
                               *cur != ',' && *cur != ' ' &&
                               *cur != '\t' && *cur != ';') cur++;
                        int tok_len = (int)(cur - tok_start);

                        if (tok_len > 0) {
                            char arch_tok[64];
                            if (tok_len >= 64) tok_len = 63;
                            memcpy(arch_tok, tok_start, (size_t)tok_len);
                            arch_tok[tok_len] = '\0';

                            if (pp_casecmp(arch_tok, state->arch) == 0) {
                                found = 1;
                                break;
                            }
                        }

                        /* Advance past comma */
                        while (cur < line_end &&
                               (*cur == ',' || *cur == ' ' ||
                                *cur == '\t')) cur++;
                    }

                    if (!found) {
                        /* Build the allowed list for the error message */
                        char allowed[256];
                        int  alen = 0;
                        const char *ac = arg;
                        while (ac < line_end && *ac != ';' &&
                               alen < (int)sizeof(allowed) - 1) {
                            allowed[alen++] = *ac++;
                        }
                        /* Trim trailing whitespace from allowed list */
                        while (alen > 0 &&
                               (allowed[alen-1] == ' '  ||
                                allowed[alen-1] == '\t' ||
                                allowed[alen-1] == '\r'))
                            alen--;
                        allowed[alen] = '\0';

                        fprintf(stderr,
                                "[Precompiler] %s:%d: @ARCH_ONLY — "
                                "current architecture '%s' is not in the "
                                "supported set [%s]\n",
                                filename, line_num, state->arch, allowed);
                        return -1;
                    }

                    if (strbuf_append_char(output, '\n') != 0) return -1;
                }
                /* ---- @SYS_ONLY <sys1>,<sys2>,... ---------------------- */
                else if (pp_casecmp(directive, "SYS_ONLY") == 0) {

                    if (arg >= line_end || *arg == ';') {
                        fprintf(stderr,
                                "[Precompiler] %s:%d: @SYS_ONLY requires "
                                "at least one system name\n",
                                filename, line_num);
                        return -1;
                    }

                    if (state->sys == NULL) {
                        /* No -sys specified but file requires one */
                        char allowed[256];
                        int  alen = 0;
                        const char *ac = arg;
                        while (ac < line_end && *ac != ';' &&
                               alen < (int)sizeof(allowed) - 1) {
                            allowed[alen++] = *ac++;
                        }
                        while (alen > 0 &&
                               (allowed[alen-1] == ' '  ||
                                allowed[alen-1] == '\t' ||
                                allowed[alen-1] == '\r'))
                            alen--;
                        allowed[alen] = '\0';

                        fprintf(stderr,
                                "[Precompiler] %s:%d: @SYS_ONLY — "
                                "no -sys specified, but file requires "
                                "one of [%s]\n",
                                filename, line_num, allowed);
                        return -1;
                    }

                    /* Walk a comma-separated list of system names */
                    int found = 0;
                    const char *cur = arg;
                    while (cur < line_end && *cur != ';') {
                        while (cur < line_end &&
                               (*cur == ' ' || *cur == '\t')) cur++;
                        if (cur >= line_end || *cur == ';') break;

                        const char *tok_start = cur;
                        while (cur < line_end &&
                               *cur != ',' && *cur != ' ' &&
                               *cur != '\t' && *cur != ';') cur++;
                        int tok_len = (int)(cur - tok_start);

                        if (tok_len > 0) {
                            char sys_tok[64];
                            if (tok_len >= 64) tok_len = 63;
                            memcpy(sys_tok, tok_start, (size_t)tok_len);
                            sys_tok[tok_len] = '\0';

                            if (pp_casecmp(sys_tok, state->sys) == 0) {
                                found = 1;
                                break;
                            }
                        }

                        while (cur < line_end &&
                               (*cur == ',' || *cur == ' ' ||
                                *cur == '\t')) cur++;
                    }

                    if (!found) {
                        char allowed[256];
                        int  alen = 0;
                        const char *ac = arg;
                        while (ac < line_end && *ac != ';' &&
                               alen < (int)sizeof(allowed) - 1) {
                            allowed[alen++] = *ac++;
                        }
                        while (alen > 0 &&
                               (allowed[alen-1] == ' '  ||
                                allowed[alen-1] == '\t' ||
                                allowed[alen-1] == '\r'))
                            alen--;
                        allowed[alen] = '\0';

                        fprintf(stderr,
                                "[Precompiler] %s:%d: @SYS_ONLY — "
                                "current system '%s' is not in the "
                                "supported set [%s]\n",
                                filename, line_num, state->sys, allowed);
                        return -1;
                    }

                    if (strbuf_append_char(output, '\n') != 0) return -1;
                }
                /* ---- @DEFINE <NAME> <VALUE> --------------------------- */
                else if (pp_casecmp(directive, "DEFINE") == 0) {

                    /* Parse: NAME (identifier) */
                    const char *name_start = arg;
                    while (name_start < line_end &&
                           (*name_start == ' ' || *name_start == '\t'))
                        name_start++;
                    if (name_start >= line_end ||
                        !pp_is_ident_start(*name_start)) {
                        fprintf(stderr,
                                "[Precompiler] %s:%d: @DEFINE requires "
                                "a name\n", filename, line_num);
                        return -1;
                    }
                    const char *name_end = name_start;
                    while (name_end < line_end &&
                           pp_is_ident_char(*name_end))
                        name_end++;
                    int nlen = (int)(name_end - name_start);

                    /* Parse: VALUE (rest of line, trimmed) */
                    const char *val_start = name_end;
                    while (val_start < line_end &&
                           (*val_start == ' ' || *val_start == '\t'))
                        val_start++;
                    const char *val_end = line_end;
                    /* Trim trailing whitespace & comments */
                    while (val_end > val_start &&
                           (*(val_end - 1) == ' '  ||
                            *(val_end - 1) == '\t' ||
                            *(val_end - 1) == '\r'))
                        val_end--;
                    int vlen = (int)(val_end - val_start);
                    if (vlen <= 0) {
                        fprintf(stderr,
                                "[Precompiler] %s:%d: @DEFINE requires "
                                "a value\n", filename, line_num);
                        return -1;
                    }

                    if (pp_macro_add(&state->macros,
                                     name_start, nlen,
                                     val_start, vlen,
                                     filename, line_num) != 0)
                        return -1;

                    /* No code emitted — just a blank line */
                    if (strbuf_append_char(output, '\n') != 0) return -1;
                }
                /* ---- @DUMMY [message] --------------------------------- */
                else if (pp_casecmp(directive, "DUMMY") == 0) {

                    /* The rest of the line is the optional message */
                    const char *msg     = arg;
                    const char *msg_end = line_end;
                    /* Trim trailing whitespace */
                    while (msg_end > msg &&
                           (*(msg_end - 1) == ' '  ||
                            *(msg_end - 1) == '\t' ||
                            *(msg_end - 1) == '\r'))
                        msg_end--;
                    int msg_len = (int)(msg_end - msg);

                    if (msg_len > 0) {
                        fprintf(stderr,
                                "[Precompiler] DUMMY %s:%d: %.*s\n",
                                filename, line_num, msg_len, msg);
                    } else {
                        fprintf(stderr,
                                "[Precompiler] DUMMY %s:%d: "
                                "(no implementation)\n",
                                filename, line_num);
                    }

                    /* No code emitted — just a blank line */
                    if (strbuf_append_char(output, '\n') != 0) return -1;
                }
                /* ---- @ORG <address> ----------------------------------- */
                else if (pp_casecmp(directive, "ORG") == 0) {

                    /* arg must contain the address (hex 0x... or decimal) */
                    const char *addr_start = arg;
                    while (addr_start < line_end &&
                           (*addr_start == ' ' || *addr_start == '\t'))
                        addr_start++;
                    const char *addr_end = addr_start;
                    while (addr_end < line_end &&
                           *addr_end != ' ' && *addr_end != '\t' &&
                           *addr_end != '\r' && *addr_end != '\n')
                        addr_end++;
                    if (addr_start == addr_end) {
                        fprintf(stderr,
                                "[Precompiler] %s:%d: @ORG requires an "
                                "address argument\n",
                                filename, line_num);
                        return -1;
                    }

                    /* Emit as: ORG <address>   (for the parser) */
                    if (strbuf_append(output, "ORG ", 4) != 0) return -1;
                    if (strbuf_append(output, addr_start,
                                      (int)(addr_end - addr_start)) != 0)
                        return -1;
                    if (strbuf_append_char(output, '\n') != 0) return -1;
                }
                /* ---- Unknown @-directive ------------------------------ */
                else {
                    fprintf(stderr,
                            "[Precompiler] %s:%d: unknown directive '@%s'\n",
                            filename, line_num, directive);
                    return -1;
                }
            }
            /* ---- Inactive region — skip silently ---------------------- */
            else {
                if (strbuf_append_char(output, '\n') != 0) return -1;
            }
        }
        /* ================================================================
         *  Regular (non-directive) line
         * ============================================================== */
        else if (is_active) {
            /* Expand @DEFINE macros, then emit */
            if (state->macros.count > 0) {
                if (pp_expand_macros(&state->macros,
                                     line_start, line_len, output) != 0)
                    return -1;
            } else {
                if (strbuf_append(output, line_start, line_len) != 0)
                    return -1;
            }
            if (strbuf_append_char(output, '\n') != 0) return -1;
        }
        else {
            /* Inactive — blank line placeholder */
            if (strbuf_append_char(output, '\n') != 0) return -1;
        }

        line_num++;
    }

    /* ---- Check for unterminated @IF blocks ---------------------------- */
    if (total_depth != 0) {
        fprintf(stderr,
                "[Precompiler] %s: %d unterminated @IF block(s) — "
                "missing @ENDIF\n", filename, total_depth);
        return -1;
    }

    return 0;
}

/* =========================================================================
 *  Public API
 * ========================================================================= */
char* preprocess(const char *source,
                 const char *arch,
                 const char *sys,
                 const char *base_dir,
                 const char *filename,
                 const char *exe_dir)
{
    if (!source || !arch) {
        fprintf(stderr, "[Precompiler] Error: NULL source or arch\n");
        return NULL;
    }

    PPState state;
    pp_state_init(&state, arch, sys, exe_dir);

    StrBuf output;
    if (strbuf_init(&output) != 0) {
        fprintf(stderr, "[Precompiler] Error: out of memory\n");
        return NULL;
    }

    /* Deferred buffer: imported code is placed after main code so that
     * the entry point in the resulting binary starts at the first user
     * instruction rather than inside a library function. */
    StrBuf deferred;
    if (strbuf_init(&deferred) != 0) {
        fprintf(stderr, "[Precompiler] Error: out of memory\n");
        strbuf_free(&output);
        return NULL;
    }

    const char *dir  = (base_dir && *base_dir) ? base_dir : ".";
    const char *file = (filename && *filename) ? filename : "<input>";

    /* Also mark the main file as imported so it can't import itself */
    {
        char main_resolved[PP_MAX_PATH_LEN];
        if (pp_resolve_path(dir, file,
                            main_resolved, PP_MAX_PATH_LEN) == 0) {
            pp_mark_imported(&state, main_resolved);
        }
    }

    int rc = pp_process(source, file, &state, dir, 0, &output, &deferred);

    pp_state_free(&state);

    if (rc != 0) {
        strbuf_free(&output);
        strbuf_free(&deferred);
        return NULL;
    }

    /* Append deferred (imported) code after main code */
    if (deferred.size > 0) {
        if (strbuf_append(&output, deferred.data, deferred.size) != 0) {
            strbuf_free(&output);
            strbuf_free(&deferred);
            return NULL;
        }
    }
    strbuf_free(&deferred);

    /* Null-terminate the output */
    if (strbuf_append_char(&output, '\0') != 0) {
        strbuf_free(&output);
        return NULL;
    }

    /* Detach — caller owns the memory now */
    return output.data;
}
