/**
* @authors Hana Liškařová (xliskah00)
 *
 * @file scanner.h
 *
 * Lexical analyzer implemented as a streaming FSM.
 * Line/column are 1-based and refer to the position of the last read character.
 * BUT FIT
 */

#ifndef IFJ_SCANNER_H
#define IFJ_SCANNER_H

#include <stdio.h>
#include "token.h"

/**
 * Initialize the scanner over the given input stream (e.g., stdin).
 * Resets internal state (position counters, pushback, normalization flags).
 */
void scanner_init(FILE *source);

/**
 * Finalize and clean up scanner resources (kept for hygiene/future-proofing).
 */
void scanner_destroy(void);

/**
 * Get current line number (1-based) for diagnostics.
 */
int scanner_get_line(void);

/**
 * Get current column number (1-based) for diagnostics.
 */
int scanner_get_col(void);

/**
 * Produce the next token from input according to the FSM rules.
 * @param out Pointer to a token structure to fill.
 * @return SUCCESS on success, ERR_LEX on lexical error, ERR_INTERNAL on internal error.
 */
int get_next_token(tokenPtr out);

/**
 * Read exactly one token from the current scanner input and append it to the list.
 * The list must be initialized by the caller.
 * Returns SUCCESS, ERR_LEX, or ERR_INTERNAL.
 */
int scanner_append_next_token(DLListTokens *list);

/**
 * Run the lexical analysis on `source` and return the full token list in `out_list`.
 * The function initializes `out_list` internally; caller must later call DLLTokens_Dispose(out_list).
 * Returns SUCCESS, ERR_LEX, or ERR_INTERNAL.
 */
int scanner(FILE *source, DLListTokens *out_list);



#endif // IFJ_SCANNER_H
