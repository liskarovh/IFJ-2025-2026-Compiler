/**
* @authors Martin Bíško (xbiskom00)
 *
 * @file parser.c
 *
 * Syntax analyzer implementation
 * BUT FIT
 */

#include "parser.h"
#include "error.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

ast_class current_class = NULL;
bool has_own_block = false;

/// @brief Parse the token list and generate the AST
/// @param tokenList The list of tokens to parse
/// @param out_ast The output AST
/// @param expected_rule The grammar rule to apply
/// @return SUCCESS on success, or an error code on failure
int parser(DLListTokens *tokenList, ast out_ast, enum grammar_rule expected_rule) {
    while(tokenList->active->token->type == T_EOL) {
        DLLTokens_Next(tokenList);
    }
    token_format_string(tokenList->active->token);

    switch (expected_rule)
    {
    case GRAMMAR_PROGRAM: {
        int err = parser(tokenList, out_ast, GRAMMAR_IMPORT);
        if (err != SUCCESS) {
            return err;
        }
        
        err = parser(tokenList, out_ast, GRAMMAR_CLASS_LIST);
        if (err != SUCCESS) {
            return err;
        }

        break;
    }
    case GRAMMAR_IMPORT: {
        if(tokenList->active->token->type != T_KW_IMPORT) {
            return ERR_SYN;
        }

        out_ast->import = ast_import_init();

        DLLTokens_Next(tokenList);

        int err = parser(tokenList, out_ast, GRAMMAR_IMPORT_IFJ25);
        if (err != SUCCESS) {
            return err;
        }

        break;
    }
    case GRAMMAR_IMPORT_IFJ25: {
        if(strcmp(tokenList->active->token->value->data, "ifj25") != 0) {
            return ERR_SYN;
        }

        out_ast->import->path = tokenList->active->token->value->data;

        DLLTokens_Next(tokenList);

        int err = parser(tokenList, out_ast, GRAMMAR_IMPORT_FOR);
        if (err != SUCCESS) {
            return err;
        }
        break;
    }
    case GRAMMAR_IMPORT_FOR: {
        if(strcmp(tokenList->active->token->value->data, "for") != 0) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        int err = parser(tokenList, out_ast, GRAMMAR_IMPORT_IFJ);
        if (err != SUCCESS) {
            return err;
        }
        break;
    }
    case GRAMMAR_IMPORT_IFJ: {
        if(strcmp(tokenList->active->token->value->data, "Ifj") != 0) {
            return ERR_SYN;
        }

        out_ast->import->alias = tokenList->active->token->value->data;

        DLLTokens_Next(tokenList);
        break;
    }
    case GRAMMAR_CLASS_LIST: {
        if(tokenList->active->token->type == T_EOF) {
            break;
        }

        int err = parser(tokenList, out_ast, GRAMMAR_CLASS_DEF);
        if (err != SUCCESS) {
            return err;
        }
        err = parser(tokenList, out_ast, GRAMMAR_CLASS_LIST);
        if (err != SUCCESS) {
            return err;
        }
        break;
    }
    case GRAMMAR_CLASS_DEF: {
        if(tokenList->active->token->type != T_KW_CLASS) {
            return ERR_SYN;
        }

        current_class = ast_class_init(&out_ast->class_list);
        DLLTokens_Next(tokenList);
        
        if(tokenList->active->token->type != T_IDENT) {
            return ERR_SYN;
        }
        current_class->name = tokenList->active->token->value->data;
        DLLTokens_Next(tokenList);

        int err = parser(tokenList, out_ast, GRAMMAR_BODY);
        if (err != SUCCESS) {
            return err;
        }

        break;
    }
    case GRAMMAR_BODY: {
        if(tokenList->active->token->type != T_LBRACE) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);
        while (tokenList->active->token->type == T_EOL)
        {
            DLLTokens_Next(tokenList);
        }
        if(current_class->current == NULL) {
            ast_block_init(&current_class);
        } else if(has_own_block == false) {
            ast_add_new_node(&current_class, AST_BLOCK);
        }
        has_own_block = false;

        if(get_token_type_ignore_eol(tokenList) == T_LBRACE) {
            int err = parser(tokenList, out_ast, GRAMMAR_BODY);
            if (err != SUCCESS) {
                return err;
            }
        }

        int err = parser(tokenList, out_ast, GRAMMAR_COMMAND_LIST);
        if (err != SUCCESS) {
            return err;
        }

        while(tokenList->active->token->type == T_EOL) {
            DLLTokens_Next(tokenList);
        }

        if(tokenList->active->token->type != T_RBRACE) {
            return ERR_SYN;
        } 
        ast_block_parent(&out_ast->class_list);
        DLLTokens_Next(tokenList);
        break;
    }
    case GRAMMAR_COMMAND_LIST: {
        if(get_token_type_ignore_eol(tokenList) == T_LBRACE) {
            int err = parser(tokenList, out_ast, GRAMMAR_BODY);
            if (err != SUCCESS) {
                return err;
            }
        }

        if(get_token_type_ignore_eol(tokenList) == T_RBRACE) {
            break;
        }

        int err = parser(tokenList, out_ast, GRAMMAR_COMMAND);
        if (err != SUCCESS) {
            return err;
        }
        DLLTokens_Next(tokenList);

        if(
            get_token_type_ignore_eol(tokenList) == T_KW_STATIC ||
            get_token_type_ignore_eol(tokenList) == T_KW_VAR ||
            get_token_type_ignore_eol(tokenList) == T_IDENT ||
            get_token_type_ignore_eol(tokenList) == T_KW_IF ||
            get_token_type_ignore_eol(tokenList) == T_KW_FOR ||
            get_token_type_ignore_eol(tokenList) == T_KW_WHILE ||
            get_token_type_ignore_eol(tokenList) == T_KW_RETURN ||
            get_token_type_ignore_eol(tokenList) == T_LBRACE
        ) {
            int err = parser(tokenList, out_ast, GRAMMAR_COMMAND_LIST);
            if (err != SUCCESS) {
                return err;
            }
        }
        break;
    }
    case GRAMMAR_COMMAND: {
        if (tokenList->active->token->type == T_KW_STATIC) {
            int err = parser(tokenList, out_ast, GRAMMAR_FUN_DEF);
            if (err != SUCCESS) {
                return err;
            }
        }
        else if (tokenList->active->token->type == T_KW_VAR) {
            int err = parser(tokenList, out_ast, GRAMMAR_DECLARATION);
            if (err != SUCCESS) {
                return err;
            }
        }
        else if (tokenList->active->token->type == T_IDENT) {
            if(tokenList->active->next->token->type == T_ASSIGN) {
                int err = parser(tokenList, out_ast, GRAMMAR_ASSIGNMENT);
                if (err != SUCCESS) {
                    return err;
                }
            } else {
                int err = parser(tokenList, out_ast, GRAMMAR_FUN_CALL);
                if (err != SUCCESS) {
                    return err;
                }
            }
        }
        else if(tokenList->active->token->type == T_KW_IF) {
            int err = parser(tokenList, out_ast, GRAMMAR_CONDITION);
            if (err != SUCCESS) {
                return err;
            }
        }
        else if (tokenList->active->token->type == T_KW_FOR)
        {
            int err = parser(tokenList, out_ast, GRAMMAR_FOR);
            if (err != SUCCESS) {
                return err;
            }
        }
        else if (tokenList->active->token->type == T_KW_WHILE) {
            int err = parser(tokenList, out_ast, GRAMMAR_WHILE);
            if (err != SUCCESS) {
                return err;
            }
        }
        else if (tokenList->active->token->type == T_KW_RETURN) {
            int err = parser(tokenList, out_ast, GRAMMAR_RETURN);
            if (err != SUCCESS) {
                return err;
            }
        } else {
            return ERR_SYN;
        }

        break;
    }
    case GRAMMAR_FUN_DEF: {
        if(tokenList->active->token->type != T_KW_STATIC) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        
        ast_add_new_node(&current_class, AST_FUNCTION);
        ast_function current_function = current_class->current->current->data.function;
        
        if(tokenList->active->token->type != T_IDENT) {
            return ERR_SYN;
        }
        current_function->name = tokenList->active->token->value->data;
        current_class->current = current_function->code;
        DLLTokens_Next(tokenList);

        int err = parser(tokenList, out_ast, GRAMMAR_PARAMS);
        if (err != SUCCESS) {
            return err;
        }

        has_own_block = true;
        err = parser(tokenList, out_ast, GRAMMAR_BODY);
        if (err != SUCCESS) {
            return err;
        }
        break;
    }
    case GRAMMAR_PARAMS: {
        if(tokenList->active->token->type != T_LPAREN) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        int err = parser(tokenList, out_ast, GRAMMAR_PARAM_LIST);
        if (err != SUCCESS) {
            return err;
        }

        if(tokenList->active->token->type != T_RPAREN) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);
        break;
    }
    case GRAMMAR_PARAM_LIST: {
        if(tokenList->active->token->type == T_RPAREN) {
            break;
        }

        if(tokenList->active->token->type != T_IDENT) {
            return ERR_SYN;
        }

        if(current_class->current->current != NULL) {
            if(current_class->current->current->type == AST_FUNCTION) {
                ast_function current_function = current_class->current->current->data.function;
                if(current_function->parameters == NULL) {
                    current_function->parameters = malloc(sizeof(struct ast_parameter));
                    current_function->parameters->name = tokenList->active->token->value->data;
                    current_function->parameters->next = NULL;
                } else {
                    ast_parameter param_iter = current_function->parameters;
                    while(param_iter->next != NULL) {
                        param_iter = param_iter->next;
                    }
                    param_iter->next = malloc(sizeof(struct ast_parameter));
                    param_iter->next->name = tokenList->active->token->value->data;
                    param_iter->next->next = NULL;
                }
            } else if(current_class->current->current->type == AST_CALL_FUNCTION) {
                ast_fun_call current_fun_call = current_class->current->current->data.function_call;
                if(current_fun_call->parameters == NULL) {
                    current_fun_call->parameters = malloc(sizeof(struct ast_parameter));
                    current_fun_call->parameters->name = tokenList->active->token->value->data;
                    current_fun_call->parameters->next = NULL;
                } else {
                    ast_parameter param_iter = current_fun_call->parameters;
                    while(param_iter->next != NULL) {
                        param_iter = param_iter->next;
                    }
                    param_iter->next = malloc(sizeof(struct ast_parameter));
                    param_iter->next->name = tokenList->active->token->value->data;
                    param_iter->next->next = NULL;
                }
            }
        }
        
        DLLTokens_Next(tokenList);
        
        if(tokenList->active->token->type == T_COMMA) {
            DLLTokens_Next(tokenList);
            int err = parser(tokenList, out_ast, GRAMMAR_PARAM_LIST);
            if (err != SUCCESS) {
                return err;
            }
        }
        break;
    }
    case GRAMMAR_DECLARATION: {
        if(tokenList->active->token->type != T_KW_VAR) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);
        if(tokenList->active->token->type != T_IDENT) {
            return ERR_SYN;
        }
        ast_add_new_node(&current_class, AST_VAR_DECLARATION);
        current_class->current->current->data.declaration.name = tokenList->active->token->value->data;
        DLLTokens_Next(tokenList);
        if(tokenList->active->token->type != T_EOL) {
            return ERR_SYN;
        }
        break;
    }
    case GRAMMAR_FUN_CALL:{
        if(tokenList->active->token->type != T_IDENT) {
            return ERR_SYN;
        }
        ast_add_new_node(&current_class, AST_CALL_FUNCTION);
        current_class->current->current->data.function_call->name = tokenList->active->token->value->data;
        DLLTokens_Next(tokenList);

        int err = parser(tokenList, out_ast, GRAMMAR_PARAMS);
        if (err != SUCCESS) {
            return err;
        }
        break;
    }
    case GRAMMAR_RETURN: {
        if(tokenList->active->token->type != T_KW_RETURN) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);
        ast_add_new_node(&current_class, AST_RETURN);
        
        //TODO
        current_class->current->current->data.return_expr.output = NULL;

        break;
    }
    default:
        break;
    }

    return SUCCESS;
}

