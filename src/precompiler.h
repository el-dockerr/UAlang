/*
 * =============================================================================
 *  UA - Unified Assembler
 *  Precompiler (Preprocessor)
 *
 *  File:    precompiler.h
 *  Purpose: Evaluate @-directives before lexing — conditional compilation,
 *           file imports, and stub markers.
 *
 *  Directives:
 *    @IF_ARCH <arch>     Include block only when -arch matches
 *    @IF_SYS  <system>   Include block only when -sys matches
 *    @ENDIF              Close an @IF_ARCH / @IF_SYS block
 *    @IMPORT  <path>     Include another .ua file (imported at most once)
 *    @DUMMY   [message]  Mark a stub; print diagnostic to stderr
 *
 *  License: MIT
 * =============================================================================
 */

#ifndef UA_PRECOMPILER_H
#define UA_PRECOMPILER_H

/*
 *  preprocess()
 *
 *  Run the UA precompiler on raw source text.  All @-directives are
 *  evaluated and removed; the returned string contains only assembly
 *  instructions and comments ready for the lexer.
 *
 *  Parameters:
 *    source    Null-terminated source text.
 *    arch      Target architecture string  ("x86", "x86_32", "arm", "mcs51").
 *    sys       Target system string        ("win32", "linux") or NULL.
 *    base_dir  Directory of the input file (for resolving @IMPORT paths).
 *    filename  Name/path of the input file (used in diagnostic messages).
 *    exe_dir   Directory of the compiler executable (for resolving std_*
 *              library imports).  May be NULL if unknown.
 *
 *  Returns:
 *    Heap-allocated preprocessed source text — caller must free().
 *    Returns NULL on error (diagnostics are printed to stderr).
 */
char* preprocess(const char *source,
                 const char *arch,
                 const char *sys,
                 const char *base_dir,
                 const char *filename,
                 const char *exe_dir);

#endif /* UA_PRECOMPILER_H */
