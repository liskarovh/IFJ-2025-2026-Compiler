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
#include "codegen.h"


int main() {

    DLListTokens token_list;
    DLLTokens_Init(&token_list);

    int result = scanner(stdin, &token_list);

    generator gen = malloc(sizeof(generator));
    if(gen == NULL)
        error(ERR_INTERNAL, "Alocation error");


    if (result != SUCCESS) {
        DLLTokens_Dispose(&token_list);
        return result;
    }
    DLLTokens_First(&token_list);

    ast ast_tree = NULL;
    ast_init(&ast_tree);

    result = parser(&token_list, ast_tree, GRAMMAR_PROGRAM);
    if (result != SUCCESS) {
        DLLTokens_Dispose(&token_list);
        return result;
    }


    //ast_print(ast_tree);

    init_code(gen, ast_tree);
    generate_code(gen, ast_tree);
    fputs(gen->output->data, stdout);
    
    result = 0;
    free(gen);
    //ast_dispose(ast_tree);
    DLLTokens_Dispose(&token_list);

    return result;
}