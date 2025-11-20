/**
 * @authors Martin Bíško (xbiskom00)
 * @authors Hana Liškařová (xliskah00)
 *
 * @file main.c;
 * 
 * Error handeling and output to stderr
 * BUT FIT
 */

#include <stdio.h>
#include <stdlib.h>
#include "scanner.h"
#include "parser.h"
#include "token.h"
#include "error.h"
#include "ast.h"
#include "semantic.h"


int main() {

    DLListTokens token_list;
    DLLTokens_Init(&token_list);

    int result = scanner(stdin, &token_list);

    if (result != SUCCESS) {
        DLLTokens_Dispose(&token_list);
        return result;
    }
    DLLTokens_First(&token_list);

    ast ast_tree = NULL;
    ast_init(&ast_tree);

    result = parser(&token_list, ast_tree, GRAMMAR_PROGRAM);
    if (result != SUCCESS) {
        ast_dispose(ast_tree);
        DLLTokens_Dispose(&token_list);
        return result;
    }

    ast_print(ast_tree);

    result = semantic_pass1(ast_tree);
    if (result != SUCCESS) {
        ast_dispose(ast_tree);
        DLLTokens_Dispose(&token_list);
        return result;
    }
    ast_dispose(ast_tree);
    DLLTokens_Dispose(&token_list);

    return result;
}