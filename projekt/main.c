/**
* @authors Martin Bíško (xbiskom00)
 * @authors Hana Liškařová (xliskah00)
 *
 * @file main.c
 *
 * Error handling and output to stderr
 * BUT FIT
 */

#include <stdio.h>
#include <stdlib.h>

#include "scanner.h"
#include "parser.h"
#include "token.h"
#include "error.h"
#include "codegen.h"
#include "semantic.h"

int main(void) {
    int result;

    // ===== 1) Lexical analysis (scanner) =====
    DLListTokens token_list;
    DLLTokens_Init(&token_list);

    result = scanner(stdin, &token_list);
    if (result != SUCCESS) {
        DLLTokens_Dispose(&token_list);
        return result;
    }

    DLLTokens_First(&token_list);

    // ===== 2) Build AST (parser) =====
    ast ast_tree = NULL;
    ast_init(&ast_tree);

    result = parser(&token_list, ast_tree, GRAMMAR_PROGRAM);
    if (result != SUCCESS) {
        DLLTokens_Dispose(&token_list);
        // ast_dispose(ast_tree);  // použijeme, až bude AST dispose stabilní
        return result;
    }

    // ===== 3) Semantic analysis – Pass 1 =====
    result = semantic_pass1(ast_tree);
    if (result != SUCCESS) {
        DLLTokens_Dispose(&token_list);
        // ast_dispose(ast_tree);
        return result;
    }


    ast_print(ast_tree);

    // ===== 4) Code generation =====
    generator gen = malloc(sizeof(*gen));
    if (gen == NULL) {
        DLLTokens_Dispose(&token_list);
        // ast_dispose(ast_tree);
        return error(ERR_INTERNAL, "Allocation error");
    }

    init_code(gen, ast_tree);
    generate_code(gen, ast_tree);
    fputs(gen->output->data, stdout);

    // ===== 5) Cleanup =====
    free(gen);
    //ast_dispose(ast_tree);
    DLLTokens_Dispose(&token_list);

    return SUCCESS;
}
