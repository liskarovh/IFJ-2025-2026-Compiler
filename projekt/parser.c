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
#include "expressions.h"
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
    // token_format_string(tokenList->active->token);

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
        if(tokenList->active->token->type != T_EOL) {
            return ERR_SYN;
        }

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
            get_token_type_ignore_eol(tokenList) == T_GLOB_IDENT ||
            get_token_type_ignore_eol(tokenList) == T_KW_IF ||
            get_token_type_ignore_eol(tokenList) == T_KW_FOR ||
            get_token_type_ignore_eol(tokenList) == T_KW_WHILE ||
            get_token_type_ignore_eol(tokenList) == T_KW_BREAK ||
            get_token_type_ignore_eol(tokenList) == T_KW_CONTINUE ||
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
            if(tokenList->active->next->next->token->type == T_LBRACE) {
                int err = parser(tokenList, out_ast, GRAMMAR_GETTER);
                if( err != SUCCESS) {
                    return err;
                }
            } else if(tokenList->active->next->next->token->type == T_ASSIGN) {
                int err = parser(tokenList, out_ast, GRAMMAR_SETTER);
                if( err != SUCCESS) {
                    return err;
                }
            }
            else {
                int err = parser(tokenList, out_ast, GRAMMAR_FUN_DEF);
                if (err != SUCCESS) {
                    return err;
                }
            }
        }
        else if (tokenList->active->token->type == T_KW_VAR) {
            int err = parser(tokenList, out_ast, GRAMMAR_DECLARATION);
            if (err != SUCCESS) {
                return err;
            }
        }
        else if (tokenList->active->token->type == T_IDENT ||
                 tokenList->active->token->type == T_GLOB_IDENT) {
            if(strcmp(tokenList->active->token->value->data, "Ifj") == 0) {
                int err = parser(tokenList, out_ast, GRAMMAR_IFJ_CALL);
                if (err != SUCCESS) {
                    return err;
                }
            }
            else if(tokenList->active->next->token->type == T_ASSIGN) {
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
        else if (tokenList->active->token->type == T_KW_BREAK) {
            DLLTokens_Next(tokenList);
            ast_add_new_node(&current_class, AST_BREAK);
        }
        else if (tokenList->active->token->type == T_KW_CONTINUE) {
            DLLTokens_Next(tokenList);
            ast_add_new_node(&current_class, AST_CONTINUE);
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
        DLLTokens_Next(tokenList);

        int err = parser(tokenList, out_ast, GRAMMAR_PARAMS);
        if (err != SUCCESS) {
            return err;
        }
        
        current_class->current = current_function->code;

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
            } else if(current_class->current->current->type == AST_IFJ_FUNCTION) {
                ast_ifj_function current_ifj_function = current_class->current->current->data.ifj_function;
                if(current_ifj_function->parameters == NULL) {
                    current_ifj_function->parameters = malloc(sizeof(struct ast_parameter));
                    current_ifj_function->parameters->name = tokenList->active->token->value->data;
                    current_ifj_function->parameters->next = NULL;
                } else {
                    ast_parameter param_iter = current_ifj_function->parameters;
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

        if(tokenList->active->token->type == T_EOL) {
            current_class->current->current->data.return_expr.output = NULL;
        } else {
            ast_expression return_expression; 
            int err = parse_expr(tokenList, &return_expression);
            if (err != SUCCESS) {
                return err;
            }
            current_class->current->current->data.return_expr.output = return_expression;
        }

        break;
    }
    case GRAMMAR_ASSIGNMENT: {
        ast_add_new_node(&current_class, AST_ASSIGNMENT);
        current_class->current->current->data.assignment.name = tokenList->active->token->value->data;

        DLLTokens_Next(tokenList);

        if (tokenList->active->token->type != T_ASSIGN) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        ast_expression current_expression; 
        int err = parse_expr(tokenList, &current_expression);
        if (err != SUCCESS) {
            return err;
        }
        current_class->current->current->data.assignment.value = current_expression;

        break;
    }
    case GRAMMAR_CONDITION: {
        if(tokenList->active->token->type != T_KW_IF) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        if(tokenList->active->token->type != T_LPAREN) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        ast_add_new_node(&current_class, AST_CONDITION);

        ast_expression condition_expression; 
        int err = parse_expr(tokenList, &condition_expression);
        if (err != SUCCESS) {
            return err;
        }

        current_class->current->current->data.condition.condition = condition_expression;

        if(tokenList->active->token->type != T_RPAREN) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        current_class->current = current_class->current->current->data.condition.if_branch;

        has_own_block = true;
        err = parser(tokenList, out_ast, GRAMMAR_BODY);
        if (err != SUCCESS) {
            return err;

        }

        if(tokenList->active->token->type != T_KW_ELSE) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        current_class->current = current_class->current->current->data.condition.else_branch;
        has_own_block = true;
        err = parser(tokenList, out_ast, GRAMMAR_BODY);
        if (err != SUCCESS) {
            return err;

        }

        break;
    }
    case GRAMMAR_WHILE: {
        if(tokenList->active->token->type != T_KW_WHILE) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        if(tokenList->active->token->type != T_LPAREN) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        ast_add_new_node(&current_class, AST_WHILE_LOOP);

        ast_expression while_expression; 
        int err = parse_expr(tokenList, &while_expression);
        if (err != SUCCESS) {
            return err;
        }

        current_class->current->current->data.while_loop.condition = while_expression;

        if(tokenList->active->token->type != T_RPAREN) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        current_class->current = current_class->current->current->data.while_loop.body;

        has_own_block = true;
        err = parser(tokenList, out_ast, GRAMMAR_BODY);
        if (err != SUCCESS) {
            return err;

        }
        
        break;
    }
    case GRAMMAR_GETTER: {
        if(tokenList->active->token->type != T_KW_STATIC) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        if(tokenList->active->token->type != T_IDENT) {
            return ERR_SYN;
        }

        ast_add_new_node(&current_class, AST_GETTER);
        current_class->current->current->data.getter.name = tokenList->active->token->value->data;
        DLLTokens_Next(tokenList);

        current_class->current = current_class->current->current->data.getter.body;

        has_own_block = true;
        int err = parser(tokenList, out_ast, GRAMMAR_BODY);
        if (err != SUCCESS) {
            return err;
        }

        break;
    }
    case GRAMMAR_SETTER: {
        if(tokenList->active->token->type != T_KW_STATIC) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        if(tokenList->active->token->type != T_IDENT) {
            return ERR_SYN;
        }

        ast_add_new_node(&current_class, AST_SETTER);
        current_class->current->current->data.setter.name = tokenList->active->token->value->data;
        DLLTokens_Next(tokenList);

        if(tokenList->active->token->type != T_ASSIGN) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);
        if(tokenList->active->token->type != T_LPAREN) {
            return ERR_SYN;
        }
        
        DLLTokens_Next(tokenList);
        if(tokenList->active->token->type != T_IDENT) {
            return ERR_SYN;
        }
        current_class->current->current->data.setter.param = tokenList->active->token->value->data;
        DLLTokens_Next(tokenList);
        
        if(tokenList->active->token->type != T_RPAREN) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        current_class->current = current_class->current->current->data.setter.body;

        has_own_block = true;
        int err = parser(tokenList, out_ast, GRAMMAR_BODY);
        if (err != SUCCESS) {
            return err;
        }

        break;
    }
    case GRAMMAR_IFJ_CALL: {
        if(tokenList->active->token->type != T_IDENT || strcmp(tokenList->active->token->value->data, "Ifj") != 0) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);
        
        if(tokenList->active->token->type != T_DOT) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        if(tokenList->active->token->type != T_IDENT) {
            return ERR_SYN;
        }

        ast_add_new_node(&current_class, AST_IFJ_FUNCTION);
        current_class->current->current->data.ifj_function->name = tokenList->active->token->value->data;
        DLLTokens_Next(tokenList);

        int err = parser(tokenList, out_ast, GRAMMAR_PARAMS);
        if (err != SUCCESS) {
            return err;
        }
        
        return SUCCESS;
    }
    default:
        break;
    }

    return SUCCESS;
}

