/**
 * @authors Hana Liškařová (xliskah00)
 * @file scan_driver.c
 *
 * Run full lexical analysis via scanner(), print the token list.
 * BUT FIT
 */
#include <stdio.h>
#include <stdlib.h>
#include "../projekt/scanner.h"
#include "../projekt/token.h"
#include "../projekt/error.h"

static const char *tt(int t) {
    switch (t) {
        case T_EOF: return "T_EOF";
        case T_EOL: return "T_EOL";
        case T_PLUS: return "T_PLUS";
        case T_MINUS: return "T_MINUS";
        case T_MUL: return "T_MUL";
        case T_ASSIGN: return "T_ASSIGN";
        case T_EQ: return "T_EQ";
        case T_NEQ: return "T_NEQ";
        case T_LT: return "T_LT";
        case T_GT: return "T_GT";
        case T_LE: return "T_LE";
        case T_GE: return "T_GE";
        case T_NOT: return "T_NOT";
        case T_AND: return "T_AND";
        case T_OR:  return "T_OR";
        case T_LPAREN: return "T_LPAREN";
        case T_RPAREN: return "T_RPAREN";
        case T_LBRACE: return "T_LBRACE";
        case T_RBRACE: return "T_RBRACE";
        case T_COMMA: return "T_COMMA";
        case T_COLON: return "T_COLON";
        case T_QUESTION: return "T_QUESTION";
        case T_DOT: return "T_DOT";
        case T_RANGE_INC: return "T_RANGE_INC";     /* ".." */
        case T_RANGE_EXC: return "T_RANGE_EXC";     /* "..." */
        default: return "T_UNKNOWN";
    }
}

int main(int argc, char **argv) {
    FILE *src = stdin;
    if (argc == 2) {
        src = fopen(argv[1], "rb");
        if (!src) { perror("fopen"); return 2; }
    }

    DLListTokens list;
    int status = scanner(src, &list);
    if (argc == 2) fclose(src);

    if (status != SUCCESS) {
        fprintf(stderr, "scanner() failed with %d at L%d C%d\n",
                status, scanner_get_line(), scanner_get_col());
        return 1;
    }

    /* print tokens */
    printf("== TOKENS (scanner) len=%d ==\n", list.length);
    for (DLLTokenElementPtr it = list.first; it; it = it->next) {
        printf("%s\n", tt(it->token->type));
    }

    DLLTokens_Dispose(&list);
    return 0;
}
