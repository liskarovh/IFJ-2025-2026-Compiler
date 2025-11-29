/**
* @authors Hana Liškařová (xliskah00)
 *
 * @file scanner.c
 *
 * Lexical analyzer implemented as a streaming FSM.
 * Line/column are 1-based and refer to the position of the last read character.
 * BUT FIT
 */

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "scanner.h"

#include "token.h"
#include "error.h"
#include "string.h"

// Character constants
static const int LF = 10; // '\n'
static const int CR = 13; // '\r'
static const int TAB = 9; // '\t'
static const int SPACE = 32; // ' '

// Input stream
static FILE *in = NULL;

/* Current source position - increments on each visible character.
 * On '\n': line++, col=0.
 */
static int cur_line = 1;
static int cur_col = 0;

/* Single pushback
 * has_pb - is there a pushed-back character
 * pb_char - the pushed-back character
 * pb_line/pb_col - position of the pushed-back character
 */
static int has_pb = 0;
static int pb_char = 0;
static int pb_line = 1;
static int pb_col = 0;

/* Previous position - for unget_char() */
static int prev_line = 1;
static int prev_col = 0;

/* Advance the current source position counters by character 'c'.
 * On LF, increments line and resets column; otherwise increments column.
 */
static  void advance_position(int c) {
    if (c == LF) {
        cur_line++;
        cur_col = 0;
    } else {
        cur_col++;
    }
}

/* Get current position (1-based). */
int scanner_get_line(void) { return cur_line; }
int scanner_get_col(void) { return cur_col; }

/* Read one character with CR→LF normalization and single-character pushback.
 * Updates cur_line/cur_col. Returns EOF on end of stream.
 */
static int get_char(void) {
    // Serve the pushback character
    if (has_pb) {
        has_pb = 0;

        // Snapshot current position BEFORE advancing for the re-read character
        prev_line = cur_line;
        prev_col = cur_col;

        // Advance for the re-read character and return it
        advance_position(pb_char);
        return pb_char;
    }

    if (!in) return EOF;

    prev_line = cur_line;
    prev_col = cur_col;

    int c = fgetc(in);
    if (c == EOF) return EOF;

    // CRLF normalization
    if (c == CR) {
        int d = fgetc(in);
        if (d != LF) {
            if (d != EOF) {
                ungetc(d, in);
            }
        }
        c = LF; // normalize CR or CRLF to a single LF
    }

    // Advance position for the resulting character
    advance_position(c);
    return c;
}

/* Push back one character so the next get_char() will return it again.
 * Restores cur_line/cur_col to what they were before reading this character.
 */
static void unget_char(int c) {
    if (c == EOF) return;
    if (has_pb) {
        error(ERR_INTERNAL, "Scanner pushback overflow at L%d C%d", cur_line, cur_col);
        return;
    }

    has_pb = 1;
    pb_char = c;

    pb_line = prev_line;
    pb_col = prev_col;
    cur_line = prev_line;
    cur_col = prev_col;
}

/* Non-consuming one-character preview.
 * Reads one character and immediately pushes it back, leaving input/position unchanged.
 */
static int look_ahead(void) {
    int c = get_char();
    unget_char(c);
    return c;
}

/* Initialize the scanner over the given input stream (stdin).
 * Resets internal state (position counters, pushback).
 */
void scanner_init(FILE *source) {
    in = source;
    cur_line = 1;
    cur_col = 0;

    has_pb = 0;
    pb_char = 0;
    pb_line = 1;
    pb_col = 0;

    prev_line = 1;
    prev_col = 0;
}

/* Finalize and clean up scanner resources. */
void scanner_destroy(void) {
    in = NULL;
    has_pb = 0;
}

/* Returns true if the slice [text, text_len] equals the null-terminated literal. */
static  bool is_kw(const char *text, size_t text_len, const char *literal) {
    size_t literal_len = strlen(literal);
    if (text_len != literal_len) return false;
    return memcmp(text, literal, literal_len) == 0;
}

/* Keyword / boolean lookup. */
static bool keyword_lookup(const char *text, size_t text_len, int *out_type) {
    // KEYWORDS
    if (is_kw(text, text_len, "class")) {
        *out_type = T_KW_CLASS;
        return true;
    }
    if (is_kw(text, text_len, "if")) {
        *out_type = T_KW_IF;
        return true;
    }
    if (is_kw(text, text_len, "else")) {
        *out_type = T_KW_ELSE;
        return true;
    }
    if (is_kw(text, text_len, "is")) {
        *out_type = T_KW_IS;
        return true;
    }
    if (is_kw(text, text_len, "null")) {
        *out_type = T_KW_NULL;
        return true;
    }
    if (is_kw(text, text_len, "return")) {
        *out_type = T_KW_RETURN;
        return true;
    }
    if (is_kw(text, text_len, "var")) {
        *out_type = T_KW_VAR;
        return true;
    }
    if (is_kw(text, text_len, "while")) {
        *out_type = T_KW_WHILE;
        return true;
    }
    if (is_kw(text, text_len, "static")) {
        *out_type = T_KW_STATIC;
        return true;
    }
    if (is_kw(text, text_len, "import")) {
        *out_type = T_KW_IMPORT;
        return true;
    }
    if (is_kw(text, text_len, "for")) {
        *out_type = T_KW_FOR;
        return true;
    }
    if (is_kw(text, text_len, "Num")) {
        *out_type = T_KW_NUM;
        return true;
    }
    if (is_kw(text, text_len, "string")) {
        *out_type = T_KW_STRING;
        return true;
    }
    if (is_kw(text, text_len, "nulltype")) {
        *out_type = T_KW_NULLTYPE;
        return true;
    }
    if (is_kw(text, text_len, "ifj")) {
        *out_type = T_KW_IFJ;
        return true;
    }

    // CYCLES
    if (is_kw(text, text_len, "in")) {
        *out_type = T_KW_IN;
        return true;
    }
    if (is_kw(text, text_len, "break")) {
        *out_type = T_KW_BREAK;
        return true;
    }
    if (is_kw(text, text_len, "continue")) {
        *out_type = T_KW_CONTINUE;
        return true;
    }

    // BOOLEAN
    if (is_kw(text, text_len, "true")) {
        *out_type = T_BOOL_TRUE;
        return true;
    }
    if (is_kw(text, text_len, "false")) {
        *out_type = T_BOOL_FALSE;
        return true;
    }

    return false;
}

/* Convert a numeric lexeme stored in `string` to either integer or double.
 * Returns SUCCESS on success, ERR_LEX on lexical error (invalid format or out of range),
 * ERR_INTERNAL on internal error.
 */
static int str_to_number(const struct string *src, bool as_float, long long *out_int, double *out_float) {
    if (!src)
        return error(ERR_INTERNAL, "str_to_number: null source string");
    if (src->length == 0)
        return error(ERR_LEX, "str_to_number: empty numeric lexeme");

    // Copy to a NUL-terminated buffer for strtod/strtoll
    size_t n = src->length;
    char *buf = (char *) malloc(n + 1);
    if (!buf)
        return error(ERR_INTERNAL, "str_to_number: out of memory");
    memcpy(buf, src->data, n);
    buf[n] = '\0';

    int ret = SUCCESS;
    char *endp = NULL;
    errno = 0;

    if (as_float) {
        double val = strtod(buf, &endp);
        if (endp == buf || *endp != '\0')
            ret = error(ERR_LEX, "Invalid floating literal");
        else if (errno == ERANGE)
            ret = error(ERR_LEX, "Floating literal out of range");
        *out_float = val;
    } else {
        int base = 10;
        if (buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X'))
            base = 16;

        long long val = strtoll(buf, &endp, base);
        if (endp == buf || *endp != '\0')
            ret = error(ERR_LEX, "Invalid integer literal");
        else if (errno == ERANGE)
            ret = error(ERR_LEX, "Integer literal out of range");
        *out_int = val;
    }

    free(buf);
    return ret;
}

/* ========== Whitespace & EOL ========== */

static  bool is_eol(int c) { return c == LF; }
static  bool is_space_or_tab(int c) { return c == SPACE || c == TAB; }

/* ========== Identifier character classes ========== */

static  bool is_letter(int c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static  bool is_digit(int c) { return (c >= '0' && c <= '9'); }
static  bool is_nonzero_digit(int c) { return (c >= '1' && c <= '9'); }
static  bool is_zero(int c) { return (c == '0'); }
static  bool is_underscore(int c) { return c == '_'; }
static  bool is_ident_cont(int c) { return is_letter(c) || is_digit(c) || is_underscore(c); }

/* ========== Numbers ========== */

static  bool is_hex_digit(int c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static  int hex_value(int c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

static  bool is_exponent_marker(int c) { return (c == 'e' || c == 'E'); }
static  bool is_sign(int c) { return (c == '+' || c == '-'); }
static  bool is_dot(int c) { return c == '.'; }

/* ========== Strings ========== */

static  bool is_quote(int c) { return c == '"'; }
static  bool is_escape_lead(int c) { return c == '\\'; }
static  bool is_simple_escape_letter(int c) { return c == '"' || c == '\\' || c == 'n' || c == 'r' || c == 't'; }
static  bool is_hex_lead(int c) { return c == 'x' || c == 'X'; }

/* ========== Operators, comments, delimiters ========== */

static  bool is_slash(int c) { return c == '/'; }
static  bool is_operator_starter(int c) { return c == '=' || c == '!' || c == '<' || c == '>'; }
static  bool is_basic_operator(int c) { return c == '+' || c == '-' || c == '*'; }
static  bool is_paren_or_brace(int c) { return c == '(' || c == ')' || c == '{' || c == '}'; }
static  bool is_punct(int c) { return c == ',' || c == ':' || c == '?'; }

/* ========== Bool operators ========== */
static  bool is_bool_and_or(int c) { return c == '&' || c == '|'; }

/* ========== ASCII allowances ========== */

// non-ASCII characters (128..255)
static  bool is_non_ascii(int c) { return c < 0 || c > 127; }

// Printable ASCII (32..126)
static  bool is_printable_ascii(int c) { return c >= 32 && c <= 126; }

/* Allowed outside of strings:
 *  - TAB, SPACE, LF
 *  - Printable ASCII (33..126)
 */
static  bool is_allowed_ascii(int c) {
    if (is_non_ascii(c)) return false;
    if (c == TAB || c == LF || c == SPACE) return true;
    if (is_printable_ascii(c)) return true;
    return false;
}

static  bool is_allowed_ascii_single_line_literal(int c) {
    if (c == LF) return false; // no EOL
    if (is_non_ascii(c)) return false; // only ASCII
    if (!is_printable_ascii(c)) return false; // 32..126 only
    if (c == '"' || c == '\\') return false; // must be escaped
    return true;
}

static  bool is_allowed_ascii_multi_line_literal(int c) {
    if (is_non_ascii(c)) return false;
    if (c == TAB || c == LF) return true; // allowed controls
    if (is_printable_ascii(c)) return true; // 32..126
    return false;
}

/* Main tokenization function.
 * Reads the next token from the input stream and writes it to 'out'.
 */
int get_next_token(tokenPtr out) {
    if (!out)
        return error(ERR_INTERNAL, "Null token pointer passed to get_next_token()");
    token_clear(out);

    while (true) {
        int c = look_ahead();

        /** =========================
         *  EOF
         *  ========================= */
        if (c == EOF) {
            out->type = T_EOF;
            return SUCCESS;
        }

        /** =========================
         *  WHITESPACE (SPACE/TAB)
         *  ========================= */
        if (is_space_or_tab(c)) {
            get_char();
            while (true) {
                c = look_ahead();
                if (!is_space_or_tab(c)) break;
                get_char();
            }
            continue;
        }

        /** =========================
         *  EOL COLLAPSE (LF)
         *  Collapse one or more LFs into a single T_EOL.
         *  ========================= */
        if (is_eol(c)) {
            get_char();
            while (true) {
                c = look_ahead();
                if (!is_eol(c)) break;
                get_char();
            }
            out->type = T_EOL;
            return SUCCESS;
        }

        /** =========================
         *  GLOBAL IDENTIFIER: __[A-Za-z0-9_]+
         *  Two leading underscores required; at least one ident char after.
         *  Emits T_GLOB_IDENT and stores lexeme to token->value.
         *  ========================= */
        if (is_underscore(c)) {
            get_char(); // consume first '_'
            int la1 = look_ahead(); // peek next

            if (!is_underscore(la1)) {
                // Only one '_' -> lexical error (no standalone '_')
                return error(ERR_LEX, "Only one underscore as standalone token at L%d C%d", scanner_get_line(), scanner_get_col());
            }

            // "__"
            get_char(); // consume second '_'

            // At least one valid identifier
            int la2 = look_ahead();
            if (!is_ident_cont(la2)) {
                return error(ERR_LEX, "Empty global identifier after '__' at L%d C%d", scanner_get_line(), scanner_get_col());
            }

            // Ensure token->value exists and is empty
            if (!out->value) {
                out->value = string_create(DEFAULT_SIZE);
                if (!out->value)
                    return error(ERR_INTERNAL, "Out of memory in scanner");
            } else {
                out->value->length = 0;
                if (out->value->data) out->value->data[0] = '\0';
            }

            // Append "__"
            if (!string_append_char(out->value, '_'))
                return error(ERR_INTERNAL, "Out of memory in scanner");
            if (!string_append_char(out->value, '_'))
                return error(ERR_INTERNAL, "Out of memory in scanner");

            // Read the rest of the identifier
            while (true) {
                int ident_c = look_ahead();
                if (!is_ident_cont(ident_c)) break;
                get_char(); // consume
                if (!string_append_char(out->value, (char) ident_c)) {
                    return error(ERR_INTERNAL, "Out of memory in scanner");
                }
            }

            out->type = T_GLOB_IDENT;
            return SUCCESS;
        }

        /** =========================
         *  IDENTIFIER / KEYWORD: [A-Za-z][A-Za-z0-9_]*
         *  Collect identifier: if matches a keyword/boolean -> emit T_KW_* or T_BOOL_*,
         *  otherwise emit T_IDENT.
         *  ========================= */
        if (is_letter(c)) {
            // Ensure token->value exists and is empty
            if (!out->value) {
                out->value = string_create(DEFAULT_SIZE);
                if (!out->value)
                    return error(ERR_INTERNAL, "Out of memory in scanner");
            } else {
                out->value->length = 0;
                if (out->value->data) out->value->data[0] = '\0';
            }

            // Collect the identifier into string
            while (true) {
                int ident_char = look_ahead();
                if (!is_ident_cont(ident_char)) break;
                get_char(); // consume
                if (!string_append_char(out->value, (char) ident_char)) {
                    return error(ERR_INTERNAL, "Out of memory in scanner");
                }
            }

            int kw_type = 0;
            if (keyword_lookup(out->value->data, out->value->length, &kw_type)) {
                out->type = kw_type; // keyword or boolean
                return SUCCESS;
            }

            // Not a keyword - regular identifier
            out->type = T_IDENT;
            return SUCCESS;
        }

        /** =========================
         *  NUMBERS (DEC/HEX/FLOAT/EXP)
         *  - Leading '0' handles hex (0x..) and decimal '0' special case.
         *  - Non-zero-leading: decimal digits, optional fraction, optional exponent.
         *  ========================= */
        if (is_zero(c) || is_nonzero_digit(c)) {
            string num = string_create(DEFAULT_SIZE);
            if (!num)
                return error(ERR_INTERNAL, "Out of memory in scanner");

            if (is_zero(c)) {
                // Numbers starting with '0'
                get_char(); // consume '0'
                if (!string_append_char(num, '0')) {
                    string_destroy(num);
                    return error(ERR_INTERNAL, "Out of memory in scanner");
                }

                int la = look_ahead();

                // Hex integer: 0x / 0X
                if (is_hex_lead(la)) {
                    get_char(); // consume 'x'/'X'
                    if (!string_append_char(num, (char) la)) {
                        string_destroy(num);
                        return error(ERR_INTERNAL, "Out of memory in scanner");
                    }

                    // At least one hex digit required
                    int la_hex = look_ahead();
                    if (!is_hex_digit(la_hex)) {
                        string_destroy(num);
                        return error(ERR_LEX, "Hex literal requires at least one hex digit after 0x/0X");
                    }
                    while (is_hex_digit(look_ahead())) {
                        int hex_digit = get_char();
                        if (!string_append_char(num, (char) hex_digit)) {
                            string_destroy(num);
                            return error(ERR_INTERNAL, "Out of memory in scanner");
                        }
                    }

                    // Convert to integer
                    long long integer = 0;
                    int conv = str_to_number(num, /*as_float=*/false, &integer, NULL);
                    if (conv != SUCCESS) {
                        string_destroy(num);
                        return conv;
                    }

                    out->type = T_INT;
                    out->value_int = integer;
                    string_destroy(num);
                    return SUCCESS;
                }

                // '.' or 'e'/'E' handled in shared decimal/exponent branch
                if (is_dot(la) || is_exponent_marker(la)) {
                    goto DEC_FRACTION_OR_EXP;
                }

                // Decimal digit after leading 0 is not allowed
                if (is_digit(la)) {
                    string_destroy(num);
                    return error(ERR_LEX, "Decimal literal with a leading zero is not allowed");
                }

                // Standalone '0'
                out->type = T_INT;
                out->value_int = 0;
                string_destroy(num);
                return SUCCESS;
            } else {
                // Numbers starting with [1-9]
                int first_digit = get_char(); // consume first digit
                if (!string_append_char(num, (char) first_digit)) {
                    string_destroy(num);
                    return error(ERR_INTERNAL, "Out of memory in scanner");
                }

                // Subsequent digits
                while (is_digit(look_ahead())) {
                    int digit_ch = get_char();
                    if (!string_append_char(num, (char) digit_ch)) {
                        string_destroy(num);
                        return error(ERR_INTERNAL, "Out of memory in scanner");
                    }
                }
            }

            // ===== DECIMAL AND EXPONENTS =====
        DEC_FRACTION_OR_EXP: {
                bool is_float_number = false;

                // Optional fractional part: '.' DIGIT+
                int la1 = look_ahead();
                if (is_dot(la1)) {
                    // Temporarily consume the first '.'
                    get_char();
                    int la2 = look_ahead();

                    if (la2 == '.') {
                        // Pattern ".." or "..." is a range operator,
                        unget_char('.');
                    } else {
                        // Real decimal point: a digit must follow
                        if (!is_digit(la2)) {
                            string_destroy(num);
                            return error(ERR_LEX, "Digit required after decimal point");
                        }

                        // Append '.' to buffer
                        if (!string_append_char(num, '.')) {
                            string_destroy(num);
                            return error(ERR_INTERNAL, "Out of memory in scanner");
                        }

                        // Consume digits after '.'
                        while (is_digit(look_ahead())) {
                            int digit_char = get_char();
                            if (!string_append_char(num, (char) digit_char)) {
                                string_destroy(num);
                                return error(ERR_INTERNAL, "Out of memory in scanner");
                            }
                        }

                        is_float_number = true;
                    }
                }

                // Optional exponent: 'e'/'E' ['+'|'-'] DIGIT+
                if (is_exponent_marker(look_ahead())) {
                    int exp = get_char(); // consume 'e'/'E'
                    if (!string_append_char(num, (char) exp)) {
                        string_destroy(num);
                        return error(ERR_INTERNAL, "Out of memory in scanner");
                    }

                    // Optional sign
                    if (is_sign(look_ahead())) {
                        int sign = get_char();
                        if (!string_append_char(num, (char) sign)) {
                            string_destroy(num);
                            return error(ERR_INTERNAL, "Out of memory in scanner");
                        }
                        // At least one digit after the sign
                        if (!is_digit(look_ahead())) {
                            string_destroy(num);
                            return error(ERR_LEX, "Exponent requires at least one digit");
                        }
                    } else {
                        // No sign - a digit must follow immediately
                        if (!is_digit(look_ahead())) {
                            string_destroy(num);
                            return error(ERR_LEX, "Exponent requires at least one digit");
                        }
                    }

                    // Subsequent exponent digits
                    while (is_digit(look_ahead())) {
                        int exp_digid = get_char();
                        if (!string_append_char(num, (char) exp_digid)) {
                            string_destroy(num);
                            return error(ERR_INTERNAL, "Out of memory in scanner");
                        }
                    }

                    is_float_number = true;
                }

                // Conversion and emit
                if (is_float_number) {
                    double float_val = 0.0;
                    int conv = str_to_number(num, /*as_float=*/true, NULL, &float_val);
                    if (conv != SUCCESS) {
                        string_destroy(num);
                        return conv;
                    }

                    out->type = T_FLOAT;
                    out->value_float = float_val;
                    string_destroy(num);
                    return SUCCESS;
                } else {
                    long long int_val = 0;
                    int conv = str_to_number(num, /*as_float=*/false, &int_val, NULL);
                    if (conv != SUCCESS) {
                        string_destroy(num);
                        return conv;
                    }

                    out->type = T_INT;
                    out->value_int = int_val;
                    string_destroy(num);
                    return SUCCESS;
                }
            } // end DEC_FRACTION_OR_EXP
        }

        /** =========================
         *  STRINGS
         *  - Single-line: " ... " with escapes \" \\ \n \r \t \xHH
         *  - Multi-line:  """ ... """ with indentation handling and no escapes
         *  ========================= */
        if (is_quote(c)) {
            get_char(); // consume first '"'

            // Ensure token->value exists and is empty
            if (!out->value) {
                out->value = string_create(DEFAULT_SIZE);
                if (!out->value)
                    return error(ERR_INTERNAL, "Out of memory in scanner");
            } else {
                out->value->length = 0;
                if (out->value->data) out->value->data[0] = '\0';
            }

            int la1 = look_ahead();

            // Check for multiline prefix """
            if (is_quote(la1)) {
                get_char(); // consume second '"'
                int la2 = look_ahead();

                if (is_quote(la2)) {
                    // ===== MULTI-LINE STRING =====
                    get_char(); // consume third '"'

                    bool first_line_trim = true; // skip whitespace-only tail of the opening line
                    bool at_line_start = false; // just crossed LF-  content of the new line not yet committed
                    bool pending_newline = false; // LF seen but not yet appended
                    bool line_has_content = false; // char since last LF

                    size_t ws_cap = 32;
                    size_t ws_len = 0;
                    char *ws_buf = (char *) malloc(ws_cap);
                    if (!ws_buf)
                        return error(ERR_INTERNAL, "Out of memory in scanner");

                    int ml_result = SUCCESS;

                    while (true) {
                        int string_char_ml = get_char();
                        if (string_char_ml == EOF) {
                            ml_result = error(ERR_LEX, "Unterminated multi-line string literal at L%d C%d",
                                              scanner_get_line(), scanner_get_col());
                            goto ml_cleanup;
                        }

                        // Opening-line trimming: drop SPACE/TAB until LF
                        if (first_line_trim) {
                            if (string_char_ml == LF) {
                                first_line_trim = false;
                                at_line_start = true;
                                pending_newline = false;
                                ws_len = 0;
                                line_has_content = false;
                                continue;
                            }
                            if (is_space_or_tab(string_char_ml)) continue;
                            first_line_trim = false; // first real char on opening line
                        }

                        // Detect closing delimiter  """
                        if (is_quote(string_char_ml)) {
                            int q2 = look_ahead();
                            if (is_quote(q2)) {
                                get_char(); // consume second '"'
                                int q3 = look_ahead();
                                if (is_quote(q3)) {
                                    get_char(); // consume third '"' - close

                                    // Closing-line trim
                                    if (!line_has_content) {
                                        pending_newline = false;
                                    } else {
                                        if (pending_newline) {
                                            if (!string_append_char(out->value, (char) LF)) {
                                                ml_result = error(ERR_INTERNAL, "Out of memory in scanner");
                                                goto ml_cleanup;
                                            }
                                            pending_newline = false;
                                        }
                                        for (size_t i = 0; i < ws_len; ++i) {
                                            if (!string_append_char(out->value, ws_buf[i])) {
                                                ml_result = error(ERR_INTERNAL, "Out of memory in scanner");
                                                goto ml_cleanup;
                                            }
                                        }
                                    }

                                    out->type = T_ML_STRING;
                                    ml_result = SUCCESS;
                                    goto ml_cleanup;
                                } else {
                                    // Two quotes as literal content
                                    if (at_line_start && !line_has_content) {
                                        if (pending_newline) {
                                            if (!string_append_char(out->value, (char) LF)) {
                                                ml_result = error(ERR_INTERNAL, "Out of memory in scanner");
                                                goto ml_cleanup;
                                            }
                                            pending_newline = false;
                                        }
                                        for (size_t i = 0; i < ws_len; ++i) {
                                            if (!string_append_char(out->value, ws_buf[i])) {
                                                ml_result = error(ERR_INTERNAL, "Out of memory in scanner");
                                                goto ml_cleanup;
                                            }
                                        }
                                        at_line_start = false;
                                        line_has_content = true;
                                        ws_len = 0;
                                    }
                                    if (!string_append_char(out->value, '"') || !string_append_char(out->value, '"')) {
                                        ml_result = error(ERR_INTERNAL, "Out of memory in scanner");
                                        goto ml_cleanup;
                                    }
                                    continue;
                                }
                            } else {
                                // Single quote as literal content
                                if (at_line_start && !line_has_content) {
                                    if (pending_newline) {
                                        if (!string_append_char(out->value, (char) LF)) {
                                            ml_result = error(ERR_INTERNAL, "Out of memory in scanner");
                                            goto ml_cleanup;
                                        }
                                        pending_newline = false;
                                    }
                                    for (size_t i = 0; i < ws_len; ++i) {
                                        if (!string_append_char(out->value, ws_buf[i])) {
                                            ml_result = error(ERR_INTERNAL, "Out of memory in scanner");
                                            goto ml_cleanup;
                                        }
                                    }
                                    at_line_start = false;
                                    line_has_content = true;
                                    ws_len = 0;
                                }
                                if (!string_append_char(out->value, '"')) {
                                    ml_result = error(ERR_INTERNAL, "Out of memory in scanner");
                                    goto ml_cleanup;
                                }
                                continue;
                            }
                        }

                        // Newline: delay emitting LF
                        if (string_char_ml == LF) {
                            pending_newline = true;
                            at_line_start = true;
                            ws_len = 0;
                            line_has_content = false;
                            continue;
                        }

                        // Start-of-line buffering: spaces/tabs
                        if (at_line_start && !line_has_content) {
                            if (is_space_or_tab(string_char_ml)) {
                                size_t need = ws_len + 1;
                                if (need > ws_cap) {
                                    size_t ncap = ws_cap * 2;
                                    char *nbuf = (char *) realloc(ws_buf, ncap);
                                    if (!nbuf) {
                                        ml_result = error(ERR_INTERNAL, "Out of memory in scanner");
                                        goto ml_cleanup;
                                    }
                                    ws_buf = nbuf;
                                    ws_cap = ncap;
                                }
                                ws_buf[ws_len++] = (char) string_char_ml;
                                continue;
                            }
                            // first non-whitespace on the line
                            if (pending_newline) {
                                if (!string_append_char(out->value, (char) LF)) {
                                    ml_result = error(ERR_INTERNAL, "Out of memory in scanner");
                                    goto ml_cleanup;
                                }
                                pending_newline = false;
                            }
                            for (size_t i = 0; i < ws_len; ++i) {
                                if (!string_append_char(out->value, ws_buf[i])) {
                                    ml_result = error(ERR_INTERNAL, "Out of memory in scanner");
                                    goto ml_cleanup;
                                }
                            }
                            at_line_start = false;
                            line_has_content = true;
                            ws_len = 0;
                        }

                        // Regular char
                        if (!is_allowed_ascii_multi_line_literal(string_char_ml)) {
                            ml_result = error(ERR_LEX, "Disallowed character in multi-line string 0x%02X at L%d C%d",
                                              string_char_ml, scanner_get_line(), scanner_get_col());
                            goto ml_cleanup;
                        }
                        if (!string_append_char(out->value, (char) string_char_ml)) {
                            ml_result = error(ERR_INTERNAL, "Out of memory in scanner");
                            goto ml_cleanup;
                        }
                    }

                ml_cleanup:
                    free(ws_buf);
                    return ml_result;
                }

                // ===== EMPTY single-line string: "" =====
                out->type = T_STRING;
                return SUCCESS;
            }

            // ===== SINGLE-LINE STRING =====
            while (true) {
                int string_char = get_char();

                if (string_char == EOF || is_eol(string_char)) {
                    return error(ERR_LEX, "Unterminated string literal at L%d C%d", scanner_get_line(), scanner_get_col());
                }

                if (is_quote(string_char)) {
                    out->type = T_STRING;
                    return SUCCESS;
                }

                if (is_escape_lead(string_char)) {
                    int esc = get_char();
                    if (esc == EOF || is_eol(esc)) {
                        return error(ERR_LEX, "Unterminated escape in string at L%d C%d", scanner_get_line(), scanner_get_col());
                    }

                    if (is_simple_escape_letter(esc)) {
                        char out_ch;
                        switch (esc) {
                            case '"': out_ch = '"';
                                break;
                            case '\\': out_ch = '\\';
                                break;
                            case 'n': out_ch = (char) LF;
                                break;
                            case 'r': out_ch = (char) CR;
                                break;
                            case 't': out_ch = (char) TAB;
                                break;
                            default: return error(ERR_INTERNAL, "Unhandled simple escape '\\%c' at L%d C%d", esc, scanner_get_line(), scanner_get_col());
                        }
                        if (!string_append_char(out->value, out_ch))
                            return error(ERR_INTERNAL, "Out of memory in scanner");
                        continue;
                    }

                    if (is_hex_lead(esc)) {
                        int hex1 = get_char();
                        int hex2 = get_char();
                        if (!is_hex_digit(hex1) || !is_hex_digit(hex2)) {
                            return error(ERR_LEX, "Invalid hex escape in string at L%d C%d", scanner_get_line(), scanner_get_col());
                        }
                        int byte_val = (hex_value(hex1) << 4) | hex_value(hex2);
                        if (!string_append_char(out->value, (char) byte_val))
                            return error(ERR_INTERNAL, "Out of memory in scanner");
                        continue;
                    }
                    return error(ERR_LEX, "Unknown escape '\\%c' in string at L%d C%d", esc, scanner_get_line(), scanner_get_col());
                }

                // Regular character in single-line string
                if (!is_allowed_ascii_single_line_literal(string_char)) {
                    return error(ERR_LEX, "Disallowed character in string 0x%02X at L%d C%d", string_char, scanner_get_line(), scanner_get_col());
                }
                if (!string_append_char(out->value, (char) string_char)) {
                    return error(ERR_INTERNAL, "Out of memory in scanner");
                }
            }
        }

        /** =========================
         *  SLASH & COMMENTS
         *  - Line comment: // ... (until LF/EOF) -> emit T_EOL
         *  - Block comment:  with nesting -> whitespace
         *  - Plain '/' -> T_DIV
         *  ========================= */
        if (is_slash(c)) {
            get_char(); // consume '/'
            int la = look_ahead();

            // Line comment: // ... (until LF or EOF), return one T_EOL
            if (la == '/') {
                get_char(); // consume the second '/'
                // Consume characters until EOL or EOF.
                while (true) {
                    int next_char = get_char();
                    if (next_char == EOF || is_eol(next_char)) break;
                }
                out->type = T_EOL;
                return SUCCESS;
            }

            // Block comment: /* ... */ with nesting
            if (la == '*') {
                get_char(); // consume '*'
                int depth = 1;
                while (true) {
                    int next_char = get_char();
                    if (next_char == EOF) {
                        return error(ERR_LEX, "Unterminated block comment at L%d C%d", scanner_get_line(), scanner_get_col());
                    }
                    // start nested: '/*'
                    if (next_char == '/' && look_ahead() == '*') {
                        get_char();
                        depth++;
                        continue;
                    }
                    // end current level: '*/'
                    if (next_char == '*' && look_ahead() == '/') {
                        get_char();
                        depth--;
                        if (depth == 0)
                            break;
                    }
                }
                // Block comment is whitespace
                continue;
            }

            // Plain slash is division operator
            out->type = T_DIV;
            return SUCCESS;
        }

        /** =========================
         *  BASIC ARITHMETIC: + - *
         *  Emits T_PLUS / T_MINUS / T_MUL.
         *  ========================= */
        if (is_basic_operator(c)) {
            get_char(); // consume '+' | '-' | '*'
            switch (c) {
                case '+': out->type = T_PLUS;
                    return SUCCESS;
                case '-': out->type = T_MINUS;
                    return SUCCESS;
                case '*': out->type = T_MUL;
                    return SUCCESS;
                default: break;
            }
            return error(ERR_INTERNAL, "Unhandled basic operator '%c' at L%d C%d", c, scanner_get_line(), scanner_get_col());
        }

        /** =========================
         *  COMPARISONS / ASSIGN / NOT: = ! < >
         *  - With '=': ==, !=, <=, >=
         *  - Single:  =, <, >, !
         *  ========================= */
        if (is_operator_starter(c)) {
            int first = get_char(); // consume '=', '!', '<', '>'
            int la = look_ahead(); // peek next

            if (la == '=') {
                get_char(); // consume '='
                switch (first) {
                    case '=': out->type = T_EQ;
                        break; // '=='
                    case '!': out->type = T_NEQ;
                        break; // '!='
                    case '<': out->type = T_LE;
                        break; // '<='
                    case '>': out->type = T_GE;
                        break; // '>='
                    default: break;
                }
                return SUCCESS;
            } else {
                switch (first) {
                    case '=': out->type = T_ASSIGN;
                        return SUCCESS; // '='
                    case '<': out->type = T_LT;
                        return SUCCESS; // '<'
                    case '>': out->type = T_GT;
                        return SUCCESS; // '>'
                    case '!': out->type = T_NOT;
                        return SUCCESS; // '!'
                    default: break;
                }
                return error(ERR_INTERNAL, "Unhandled operator-starter '%c' at L%d C%d", first, scanner_get_line(), scanner_get_col());
            }
        }

        /** =========================
         *  BOOLEAN AND/OR: &&  ||
         *  Requires double char, otherwise lexical error.
         *  ========================= */
        if (is_bool_and_or(c)) {
            int first = get_char(); // consume '&' or '|'
            int la = look_ahead(); // must match the same char
            if (la == first) {
                get_char(); // consume second '&' or '|'
                if (first == '&') {
                    out->type = T_AND;
                    return SUCCESS;
                } else {
                    out->type = T_OR;
                    return SUCCESS;
                }
            }
            return error(ERR_LEX, "Unexpected '%c' at L%d C%d", first, scanner_get_line(), scanner_get_col());
        }

        /** =========================
         *  PARENTHESES / BRACES: ( ) { }
         *  Emits T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE.
         *  ========================= */
        if (is_paren_or_brace(c)) {
            get_char(); // consume '(' | ')' | '{' | '}'
            switch (c) {
                case '(': out->type = T_LPAREN;
                    return SUCCESS;
                case ')': out->type = T_RPAREN;
                    return SUCCESS;
                case '{': out->type = T_LBRACE;
                    return SUCCESS;
                case '}': out->type = T_RBRACE;
                    return SUCCESS;
                default: break;
            }
            return error(ERR_INTERNAL, "Unhandled paren/brace '%c' at L%d C%d", c, scanner_get_line(), scanner_get_col());
        }

        /** =========================
         *  PUNCTUATION: , : ?
         *  Emits T_COMMA, T_COLON, T_QUESTION.
         *  ========================= */
        if (is_punct(c)) {
            get_char(); // consume ',' | ':' | '?'
            switch (c) {
                case ',': out->type = T_COMMA;
                    return SUCCESS;
                case ':': out->type = T_COLON;
                    return SUCCESS;
                case '?': out->type = T_QUESTION;
                    return SUCCESS;
                default: break;
            }
            return error(ERR_INTERNAL, "Unhandled punct '%c' at L%d C%d", c, scanner_get_line(), scanner_get_col());
        }

        /** =========================
         *  DOT FAMILY: ".", "..", "..."
         *  Emits T_DOT, T_RANGE_INC (".."), T_RANGE_EXC ("...").
         *  ========================= */
        if (is_dot(c)) {
            get_char(); // consumed first '.'
            int la1 = look_ahead();
            if (la1 == '.') {
                get_char(); // consumed second '.'
                int la2 = look_ahead();
                if (la2 == '.') {
                    get_char(); // consumed third '.'
                    out->type = T_RANGE_EXC;
                    return SUCCESS;
                } else {
                    out->type = T_RANGE_INC; // ".."
                    return SUCCESS;
                }
            } else {
                out->type = T_DOT; // "."
                return SUCCESS;
            }
        }

        /** =========================
         *  INVALID OUT-OF-STRING ASCII
         *  Reject controls (except TAB/LF/SPACE) and non-ASCII.
         *  ========================= */
        if (!is_allowed_ascii(c)) {
            return error(ERR_LEX, "Invalid character 0x%02X at L%d C%d", c, scanner_get_line(), scanner_get_col());
        }

        // Fallback: allowed ASCII, but not recognized above
        return error(ERR_LEX, "Unexpected character '%c' (0x%02X) at L%d C%d", c, (unsigned int) (unsigned char) c, scanner_get_line(), scanner_get_col());
    }
}

/* Append next token from source into the given token list. */
int scanner_append_next_token(DLListTokens *list) {
    if (!list)
        return error(ERR_INTERNAL, "scanner_append_next_token: null list");

    tokenPtr t = token_create();
    if (!t)
        return error(ERR_INTERNAL, "scanner_append_next_token: token_create failed");

    int status = get_next_token(t);
    if (status != SUCCESS) {
        token_destroy(t);
        return status;
    }

    DLLTokens_InsertLast(list, t);
    return SUCCESS;
}

/* Tokenize entire input stream, producing a list of tokens.
 * Stops after T_EOF is appended.
 */
int scanner(FILE *source, DLListTokens *out_list) {
    if (!source || !out_list)
        return error(ERR_INTERNAL, "scanner: null source or out_list");

    DLLTokens_Init(out_list);
    scanner_init(source);

    while (true) {
        int status = scanner_append_next_token(out_list);
        if (status != SUCCESS) {
            scanner_destroy();
            return status;
        }

        if (out_list->last && out_list->last->token && out_list->last->token->type == T_EOF) {
            scanner_destroy();
            return SUCCESS;
        }
    }
}
