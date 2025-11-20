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
    FILE *source = fopen("../test/ifj2025codes_zadani/test.wren", "r");
    if (source == NULL) {
        return ERR_INTERNAL;
    }

    DLListTokens token_list;
    DLLTokens_Init(&token_list);

    int result = scanner(source, &token_list);

    generator gen = malloc(sizeof(generator));
    if(gen == NULL)
        error(ERR_INTERNAL, "Alocation error");


    if (result != SUCCESS) {
        DLLTokens_Dispose(&token_list);
        fclose(source);
        
        return result;
    }
    DLLTokens_First(&token_list);

    ast ast_tree = NULL;
    ast_init(&ast_tree);

    result = parser(&token_list, ast_tree, GRAMMAR_PROGRAM);

    ast_print(ast_tree);


    init_code(gen, ast_tree);
    generate_code(gen, ast_tree);
    fputs(gen->output->data, stdout);


    free(gen);
    ast_dispose(ast_tree);
    DLLTokens_Dispose(&token_list);
    fclose(source);

    return result;
}