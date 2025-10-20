// test/scan_dump.c
#include <stdio.h>
#include "../../../projekt/scanner.h"
#include "../../../projekt/token.h"
#include "../../../projekt/string.h"
#include "../../../projekt/error.h"

// --- add to ../test/scan_dump.c, before main() ---
static const char* tt(int t) {
    switch (t) {
        case T_LPAREN: return "T_LPAREN";
        case T_RPAREN: return "T_RPAREN";
        case T_LBRACE: return "T_LBRACE";
        case T_RBRACE: return "T_RBRACE";
        case T_COMMA: return "T_COMMA";
        case T_COLON: return "T_COLON";
        case T_QUESTION: return "T_QUESTION";
        case T_DOT: return "T_DOT";
        case T_RANGE_INC: return "T_RANGE_INC";
        case T_RANGE_EXC: return "T_RANGE_EXC";
        case T_PLUS: return "T_PLUS";
        case T_MINUS: return "T_MINUS";
        case T_MUL: return "T_MUL";
        case T_DIV: return "T_DIV";
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

        case T_EOL: return "T_EOL";
        case T_EOF: return "T_EOF";

        case T_IDENT:       return "T_IDENT";
        case T_GLOB_IDENT:  return "T_GLOB_IDENT";

        case T_STRING:    return "T_STRING";
        case T_ML_STRING: return "T_ML_STRING";

        case T_INT:   return "T_INT";
        case T_FLOAT: return "T_FLOAT";

        // Keywords / bool
        case T_KW_CLASS:    return "T_KW_CLASS";
        case T_KW_IF:       return "T_KW_IF";
        case T_KW_ELSE:     return "T_KW_ELSE";
        case T_KW_IS:       return "T_KW_IS";
        case T_KW_NULL:     return "T_KW_NULL";
        case T_KW_RETURN:   return "T_KW_RETURN";
        case T_KW_VAR:      return "T_KW_VAR";
        case T_KW_WHILE:    return "T_KW_WHILE";
        case T_KW_STATIC:   return "T_KW_STATIC";
        case T_KW_IMPORT:   return "T_KW_IMPORT";
        case T_KW_FOR:      return "T_KW_FOR";
        case T_KW_NUM:      return "T_KW_NUM";
        case T_KW_STRING:   return "T_KW_STRING";
        case T_KW_NULLTYPE: return "T_KW_NULLTYPE";
        case T_KW_IFJ:      return "T_KW_IFJ";
        case T_KW_IN:       return "T_KW_IN";
        case T_KW_BREAK:    return "T_KW_BREAK";
        case T_KW_CONTINUE: return "T_KW_CONTINUE";
        case T_BOOL_TRUE:   return "T_BOOL_TRUE";
        case T_BOOL_FALSE:  return "T_BOOL_FALSE";

        default: return "<UNKNOWN_TOKEN>";
    }
}


// v token.c obvykle bývá helper na převod enum->text
// pokud ho máte jinak pojmenovaný, uprav deklaraci:
extern const char *tt(int type);

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <input-file>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }

    DLListTokens list;
    int status = scanner(f, &list);
    fclose(f);

    if (status != SUCCESS) {
        fprintf(stderr, "scanner() failed with code %d\n", status);
        return 2;
    }

    printf("== TOKENS (full dump) ==\n");

    size_t count = 0;
    DLLTokenElementPtr elem = list.first;   // iterátor

    while (elem != NULL) {
        tokenPtr tok = elem->token;

        // typ tokenu
        printf("%s", tt(tok->type));

        // payloady tokenů
        switch (tok->type) {
            // identifikátory a řetězce – tisk textu
            case T_IDENT:
            case T_GLOB_IDENT:
            case T_STRING:
#ifdef T_ML_STRING
            case T_ML_STRING:
#endif
                if (tok->value && tok->value->data && tok->value->length > 0) {
                    printf(" : '%.*s'", (int)tok->value->length, tok->value->data);
                } else {
                    printf(" : ''");
                }
                break;

            // čísla – tisk hodnoty
            case T_INT:
                printf(" : %ld", (long)tok->value_int);
                break;

            case T_FLOAT:
                // %g zobrazuje rozumně i exponenty
                printf(" : %g", tok->value_float);
                break;

            default:
                // ostatní tokeny bez payloadu
                break;
        }

        putchar('\n');
        count++;
        elem = elem->next;
    }

    printf("== COUNT: %zu ==\n", count);

    // POZOR: jen testujeme výpis. Pokud chceš čistý výstup ve valgrindu,
    // po dumpu zavolej funkci, která uvolní celý DLListTokens (podle vaší implementace).
    // Např.:
    // DLLTokens_Dispose(&list);

    return 0;
}
