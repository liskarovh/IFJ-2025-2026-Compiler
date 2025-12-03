/**
* @authors Martin Bíško (xbiskom00)
 *
 * @file parser.c
 *
 * @brief Syntax analyzer implementation
 * BUT FIT
 */

#include "parser.h"
#include "error.h"
#include "expressions.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

// Current class being parsed - used to track context during AST construction
ast_class current_class = NULL;
// Flag indicating if current block has its own block declaration
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
        // Import statement must start with 'import' keyword
        if(tokenList->active->token->type != T_KW_IMPORT) {
            return ERR_SYN;
        }

        out_ast->import = ast_import_init();

        DLLTokens_Next(tokenList);

        // Only "ifj25" is a valid import path in IFJ25 language
        if(tokenList->active->token->type != T_STRING || strcmp(tokenList->active->token->value->data, "ifj25") != 0) {
            return ERR_SYN;
        }

        out_ast->import->path = tokenList->active->token->value->data;
        DLLTokens_Next(tokenList);

        // Expect 'for' keyword after import path
        if(tokenList->active->token->type != T_KW_FOR || strcmp(tokenList->active->token->value->data, "for") != 0) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        // Alias must be 'Ifj' - this is the only valid alias for the standard library
        if(tokenList->active->token->type != T_IDENT || strcmp(tokenList->active->token->value->data, "Ifj") != 0) {
            return ERR_SYN;
        }

        out_ast->import->alias = tokenList->active->token->value->data;

        DLLTokens_Next(tokenList);

        if(tokenList->active->token->type == T_EOF) {
            return ERR_SYN;
        }

        break;
    }
    case GRAMMAR_CLASS_LIST: {
        // End of file means no more classes to parse
        if(tokenList->active->token->type == T_EOF) {
            break;
        }

        // Parse class definitions recursively until EOF
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
        // Class definition must start with 'class' keyword
        if(tokenList->active->token->type != T_KW_CLASS) {
            return ERR_SYN;
        }

        // Initialize new class node in AST and set it as current context
        current_class = ast_class_init(&out_ast->class_list);
        DLLTokens_Next(tokenList);
        
        // Class name must be a valid identifier
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
        // Body must start with opening brace
        if(tokenList->active->token->type != T_LBRACE) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);
        // Opening brace must be followed by newline
        if(tokenList->active->token->type != T_EOL) {
            return ERR_SYN;
        }

        // Skip all empty lines at the beginning of the block
        while (tokenList->active->token->type == T_EOL)
        {
            DLLTokens_Next(tokenList);
        }
        // Initialize block node if not already created
        if(current_class->current == NULL) {
            ast_block_init(&current_class);
        } else if(has_own_block == false) {
            ast_add_new_node(&current_class, AST_BLOCK);
        }
        has_own_block = false;

        // Handle nested blocks recursively
        if(get_token_type_ignore_eol(tokenList) == T_LBRACE) {
            int err = parser(tokenList, out_ast, GRAMMAR_BODY);
            if (err != SUCCESS) {
                return err;
            }
        }

        // Parse all commands inside the body
        int err = parser(tokenList, out_ast, GRAMMAR_COMMAND_LIST);
        if (err != SUCCESS) {
            return err;
        }

        // Skip trailing empty lines before closing brace
        while(tokenList->active->token->type == T_EOL) {
            DLLTokens_Next(tokenList);
        }

        // Body must end with closing brace
        if(tokenList->active->token->type != T_RBRACE) {
            return ERR_SYN;
        } 
        // Move back to parent block in AST
        ast_block_parent(&out_ast->class_list);
        DLLTokens_Next(tokenList);
        break;
    }
    case GRAMMAR_COMMAND_LIST: {
        // Check for nested block
        if(get_token_type_ignore_eol(tokenList) == T_LBRACE) {
            int err = parser(tokenList, out_ast, GRAMMAR_BODY);
            if (err != SUCCESS) {
                return err;
            }
        }

        // End of command list
        if(get_token_type_ignore_eol(tokenList) == T_RBRACE) {
            break;
        }

        // Parse single command
        int err = parser(tokenList, out_ast, GRAMMAR_COMMAND);
        if (err != SUCCESS) {
            return err;
        }
        DLLTokens_Next(tokenList);

        // Check if there are more commands to parse
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
        // 'static' keyword can introduce function def, getter, or setter
        if (tokenList->active->token->type == T_KW_STATIC) {
            // Look ahead to determine what kind of static definition this is
            if(tokenList->active->next->next->token->type == T_LBRACE) {
                // static identifier { -> getter
                int err = parser(tokenList, out_ast, GRAMMAR_GETTER);
                if( err != SUCCESS) {
                    return err;
                }
            } else if(tokenList->active->next->next->token->type == T_ASSIGN) {
                // static identifier = -> setter
                int err = parser(tokenList, out_ast, GRAMMAR_SETTER);
                if( err != SUCCESS) {
                    return err;
                }
            }
            else {
                // Otherwise it's a regular function definition
                int err = parser(tokenList, out_ast, GRAMMAR_FUN_DEF);
                if (err != SUCCESS) {
                    return err;
                }
            }
        }
        // Variable declaration
        else if (tokenList->active->token->type == T_KW_VAR) {
            int err = parser(tokenList, out_ast, GRAMMAR_DECLARATION);
            if (err != SUCCESS) {
                return err;
            }
        }
        // Identifier can be IFJ call, assignment, or function call
        else if (tokenList->active->token->type == T_IDENT ||
                 tokenList->active->token->type == T_GLOB_IDENT) {
            // Check if it's a builtin function call (Ifj.something)
            if(strcmp(tokenList->active->token->value->data, "Ifj") == 0) {
                int err = parser(tokenList, out_ast, GRAMMAR_IFJ_CALL);
                if (err != SUCCESS) {
                    return err;
                }
            }
            // Check if next token is assignment operator -> this is an assignment
            else if(tokenList->active->next->token->type == T_ASSIGN) {
                int err = parser(tokenList, out_ast, GRAMMAR_ASSIGNMENT);
                if (err != SUCCESS) {
                    return err;
                }
            } else {
                // If not assignment, must be a function call
                int err = parser(tokenList, out_ast, GRAMMAR_FUN_CALL);
                if (err != SUCCESS) {
                    return err;
                }
            }
        }
        // Conditional statement
        else if(tokenList->active->token->type == T_KW_IF) {
            int err = parser(tokenList, out_ast, GRAMMAR_CONDITION);
            if (err != SUCCESS) {
                return err;
            }
        }
        // For loop
        else if (tokenList->active->token->type == T_KW_FOR)
        {
            int err = parser(tokenList, out_ast, GRAMMAR_FOR);
            if (err != SUCCESS) {
                return err;
            }
        }
        // While loop
        else if (tokenList->active->token->type == T_KW_WHILE) {
            int err = parser(tokenList, out_ast, GRAMMAR_WHILE);
            if (err != SUCCESS) {
                return err;
            }
        }
        // Break statement - just add node to AST
        else if (tokenList->active->token->type == T_KW_BREAK) {
            DLLTokens_Next(tokenList);
            ast_add_new_node(&current_class, AST_BREAK);
        }
        // Continue statement - just add node to AST
        else if (tokenList->active->token->type == T_KW_CONTINUE) {
            DLLTokens_Next(tokenList);
            ast_add_new_node(&current_class, AST_CONTINUE);
        }
        // Return statement
        else if (tokenList->active->token->type == T_KW_RETURN) {
            int err = parser(tokenList, out_ast, GRAMMAR_RETURN);
            if (err != SUCCESS) {
                return err;
            }
        } else {
            // Unknown command - syntax error
            return ERR_SYN;
        }

        break;
    }
    case GRAMMAR_FUN_DEF: {
        // Function definition must start with 'static'
        if(tokenList->active->token->type != T_KW_STATIC) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        // Create function node in AST
        ast_add_new_node(&current_class, AST_FUNCTION);
        ast_function current_function = current_class->current->current->data.function;
        
        // Function name must be valid identifier
        if(tokenList->active->token->type != T_IDENT) {
            return ERR_SYN;
        }
        current_function->name = tokenList->active->token->value->data;
        DLLTokens_Next(tokenList);

        // Parse function parameters
        int err = parser(tokenList, out_ast, GRAMMAR_PARAMS);
        if (err != SUCCESS) {
            return err;
        }
        
        // Set function body as current context for parsing
        current_class->current = current_function->code;

        // Function body has its own block scope
        has_own_block = true;
        err = parser(tokenList, out_ast, GRAMMAR_BODY);
        if (err != SUCCESS) {
            return err;
        }
        break;
    }
    case GRAMMAR_PARAMS: {
        // Parameters must start with opening parenthesis
        if(tokenList->active->token->type != T_LPAREN) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        // Parse the actual parameter list
        int err = parser(tokenList, out_ast, GRAMMAR_PARAM_LIST);
        if (err != SUCCESS) {
            return err;
        }

        // Parameters must end with closing parenthesis
        if(tokenList->active->token->type != T_RPAREN) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);
        // For function calls, must be followed by newline
        if(current_class->current->current->type == AST_CALL_FUNCTION || current_class->current->current->type == AST_IFJ_FUNCTION) {
            if (tokenList->active->token->type != T_EOL)
                return ERR_SYN;
        }
        break;
    }
    case GRAMMAR_PARAM_LIST: {
        // Empty parameter list is valid
        if(tokenList->active->token->type == T_RPAREN) {
            break;
        }

        // Validate parameter type - must be identifier or literal value
        int token_type = tokenList->active->token->type;
        if(token_type != T_IDENT && token_type != T_STRING &&
            token_type != T_ML_STRING && token_type != T_FLOAT &&
            token_type != T_INT && token_type != T_BOOL_FALSE &&
            token_type != T_BOOL_TRUE && token_type != T_GLOB_IDENT &&
            token_type != T_KW_NULL) {
            return ERR_SYN;
        }

        // Handle different contexts where parameters can appear
        if(current_class->current->current != NULL) {
            // Function definition - parameters are formal parameters
            if(current_class->current->current->type == AST_FUNCTION) {
                // Function params must be identifiers, not literals
                if (token_type != T_IDENT) return ERR_SEM;
                ast_function current_function = current_class->current->current->data.function;
                if(current_function->parameters == NULL) {
                    current_function->parameters = malloc(sizeof(struct ast_parameter));
                    if(tokenList->active->token->type == T_FLOAT) {
                        current_function->parameters->value_type = AST_VALUE_FLOAT;
                        current_function->parameters->value.double_value = tokenList->active->token->value_float;
                    } else if (tokenList->active->token->type == T_INT) {
                        current_function->parameters->value_type = AST_VALUE_INT;
                        current_function->parameters->value.int_value = tokenList->active->token->value_int;
                    } else if (tokenList->active->token->type == T_KW_NULL)
                        current_function->parameters->value_type = AST_VALUE_NULL;
                    else {
                        if (tokenList->active->token->type == T_IDENT)
                            current_function->parameters->value_type = AST_VALUE_IDENTIFIER;
                        else current_function->parameters->value_type = AST_VALUE_STRING;
                        current_function->parameters->value.string_value = tokenList->active->token->value->data;
                    }
                    current_function->parameters->next = NULL;
                } else {
                    ast_parameter param_iter = current_function->parameters;
                    while(param_iter->next != NULL) {
                        param_iter = param_iter->next;
                    }
                    param_iter->next = malloc(sizeof(struct ast_parameter));
                    if(tokenList->active->token->type == T_FLOAT) {
                        param_iter->next->value_type = AST_VALUE_FLOAT;
                        param_iter->next->value.double_value = tokenList->active->token->value_float;
                    } else if (tokenList->active->token->type == T_INT) {
                        param_iter->next->value_type = AST_VALUE_INT;
                        param_iter->next->value.int_value = tokenList->active->token->value_int;
                    } else if (tokenList->active->token->type == T_KW_NULL)
                            param_iter->next->value_type = AST_VALUE_NULL;
                    else {
                        if (tokenList->active->token->type == T_IDENT)
                            param_iter->next->value_type = AST_VALUE_IDENTIFIER;
                        else param_iter->next->value_type = AST_VALUE_STRING;
                        param_iter->next->value.string_value = tokenList->active->token->value->data;
                    }
                    param_iter->next->next = NULL;
                }
            // Function call - parameters are actual arguments
            } else if(current_class->current->current->type == AST_CALL_FUNCTION) {
                ast_fun_call current_fun_call = current_class->current->current->data.function_call;
                if(current_fun_call->parameters == NULL) {
                    current_fun_call->parameters = malloc(sizeof(struct ast_parameter));
                    if(tokenList->active->token->type == T_FLOAT) {
                        current_fun_call->parameters->value_type = AST_VALUE_FLOAT;
                        current_fun_call->parameters->value.double_value = tokenList->active->token->value_float;
                    } else if (tokenList->active->token->type == T_INT) {
                        current_fun_call->parameters->value_type = AST_VALUE_INT;
                        current_fun_call->parameters->value.int_value = tokenList->active->token->value_int;
                    } else if (tokenList->active->token->type == T_KW_NULL)
                        current_fun_call->parameters->value_type = AST_VALUE_NULL;
                    else {
                        if (tokenList->active->token->type == T_IDENT)
                            current_fun_call->parameters->value_type = AST_VALUE_IDENTIFIER;
                        else current_fun_call->parameters->value_type = AST_VALUE_STRING;
                        current_fun_call->parameters->value.string_value = tokenList->active->token->value->data;
                    }
                    current_fun_call->parameters->next = NULL;
                } else {
                    ast_parameter param_iter = current_fun_call->parameters;
                    while(param_iter->next != NULL) {
                        param_iter = param_iter->next;
                    }
                    param_iter->next = malloc(sizeof(struct ast_parameter));
                    if(tokenList->active->token->type == T_FLOAT) {
                        param_iter->next->value_type = AST_VALUE_FLOAT;
                        param_iter->next->value.double_value = tokenList->active->token->value_float;
                    } else if (tokenList->active->token->type == T_INT) {
                        param_iter->next->value_type = AST_VALUE_INT;
                        param_iter->next->value.int_value = tokenList->active->token->value_int;
                    } else if (tokenList->active->token->type == T_KW_NULL)
                        param_iter->next->value_type = AST_VALUE_NULL;
                    else {
                        if (tokenList->active->token->type == T_IDENT)
                            param_iter->next->value_type = AST_VALUE_IDENTIFIER;
                        else param_iter->next->value_type = AST_VALUE_STRING;
                        param_iter->next->value.string_value = tokenList->active->token->value->data;
                    }
                    param_iter->next->next = NULL;
                }
            // IFJ builtin function - same handling as regular function call
            } else if(current_class->current->current->type == AST_IFJ_FUNCTION) {
                ast_ifj_function current_ifj_function = current_class->current->current->data.ifj_function;
                if(current_ifj_function->parameters == NULL) {
                    current_ifj_function->parameters = malloc(sizeof(struct ast_parameter));
                    if(tokenList->active->token->type == T_FLOAT) {
                        current_ifj_function->parameters->value_type = AST_VALUE_FLOAT;
                        current_ifj_function->parameters->value.double_value = tokenList->active->token->value_float;
                    } else if (tokenList->active->token->type == T_INT) {
                        current_ifj_function->parameters->value_type = AST_VALUE_INT;
                        current_ifj_function->parameters->value.int_value = tokenList->active->token->value_int;
                    } else if (tokenList->active->token->type == T_KW_NULL)
                        current_ifj_function->parameters->value_type = AST_VALUE_NULL;
                    else {
                        if (tokenList->active->token->type == T_IDENT)
                            current_ifj_function->parameters->value_type = AST_VALUE_IDENTIFIER;
                        else current_ifj_function->parameters->value_type = AST_VALUE_STRING;
                        current_ifj_function->parameters->value.string_value = tokenList->active->token->value->data;
                    }
                    current_ifj_function->parameters->next = NULL;
                } else {
                    ast_parameter param_iter = current_ifj_function->parameters;
                    while(param_iter->next != NULL) {
                        param_iter = param_iter->next;
                    }
                    param_iter->next = malloc(sizeof(struct ast_parameter));
                    if(tokenList->active->token->type == T_FLOAT) {
                        param_iter->next->value_type = AST_VALUE_FLOAT;
                        param_iter->next->value.double_value = tokenList->active->token->value_float;
                    } else if (tokenList->active->token->type == T_INT) {
                        param_iter->next->value_type = AST_VALUE_INT;
                        param_iter->next->value.int_value = tokenList->active->token->value_int;
                    } else if (tokenList->active->token->type == T_KW_NULL)
                        param_iter->next->value_type = AST_VALUE_NULL;
                    else {
                        if (tokenList->active->token->type == T_IDENT)
                            param_iter->next->value_type = AST_VALUE_IDENTIFIER;
                        else param_iter->next->value_type = AST_VALUE_STRING;
                        param_iter->next->value.string_value = tokenList->active->token->value->data;
                    }
                    param_iter->next->next = NULL;
                }
            }
        }
        
        DLLTokens_Next(tokenList);
        
        // If comma follows, parse more parameters recursively
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
        // Declaration must start with 'var' keyword
        if(tokenList->active->token->type != T_KW_VAR) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);
        // Variable name must be identifier
        if(tokenList->active->token->type != T_IDENT && tokenList->active->token->type != T_GLOB_IDENT) {
            return ERR_SYN;
        }
        // Create declaration node in AST
        ast_add_new_node(&current_class, AST_VAR_DECLARATION);
        char *var_name = tokenList->active->token->value->data;
        current_class->current->current->data.declaration.name = var_name;
        DLLTokens_Next(tokenList);

        // Declaration with initialization
        if (tokenList->active->token->type == T_ASSIGN) {
            // Also create assignment node for the initial value
            ast_add_new_node(&current_class, AST_ASSIGNMENT); 

            current_class->current->current->data.assignment.name = var_name;
            
            DLLTokens_Next(tokenList);

            // Parse the initialization expression
            ast_expression current_expression; 
            int err = parse_expr(tokenList, &current_expression);
            if (err != SUCCESS) {
                return err;
            }
            current_class->current->current->data.assignment.value = current_expression;
        }
        // Declaration without initialization - must end with newline
        else if (tokenList->active->token->type != T_EOL) {
            return ERR_SYN;
        }
        break;
    }
    case GRAMMAR_FUN_CALL:{
        // Function call must start with identifier
        if(tokenList->active->token->type != T_IDENT) {
            return ERR_SYN;
        }
        // Create function call node in AST
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
        // Return statement must start with 'return' keyword
        if(tokenList->active->token->type != T_KW_RETURN) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);
        ast_add_new_node(&current_class, AST_RETURN);

        // Empty return - no expression
        if(tokenList->active->token->type == T_EOL) {
            current_class->current->current->data.return_expr.output = NULL;
        } else {
            // Return with value - parse the expression
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
        // Create assignment node and store variable name
        ast_add_new_node(&current_class, AST_ASSIGNMENT);
        current_class->current->current->data.assignment.name = tokenList->active->token->value->data;

        DLLTokens_Next(tokenList);

        // Must have assignment operator
        if (tokenList->active->token->type != T_ASSIGN) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        // Parse the right-hand side expression
        ast_expression current_expression; 
        int err = parse_expr(tokenList, &current_expression);
        if (err != SUCCESS) {
            return err;
        }
        current_class->current->current->data.assignment.value = current_expression;

        break;
    }
    case GRAMMAR_CONDITION: {
        // Condition must start with 'if' keyword
        if(tokenList->active->token->type != T_KW_IF) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        // Condition expression must be in parentheses
        if(tokenList->active->token->type != T_LPAREN) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        ast_add_new_node(&current_class, AST_CONDITION);

        // Parse the condition expression
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

        // Set if-branch as current context for body parsing
        current_class->current = current_class->current->current->data.condition.if_branch;

        // Parse if-branch body
        has_own_block = true;
        err = parser(tokenList, out_ast, GRAMMAR_BODY);
        if (err != SUCCESS) {
            return err;

        }

        // Check for optional else branch
        if(tokenList->active->token->type != T_KW_ELSE) {
            return SUCCESS;
        }
        DLLTokens_Next(tokenList);

        // Set else-branch as current context
        current_class->current = current_class->current->current->data.condition.else_branch;
        // Parse else-branch body
        has_own_block = true;
        err = parser(tokenList, out_ast, GRAMMAR_BODY);
        if (err != SUCCESS) {
            return err;

        }

        break;
    }
    case GRAMMAR_WHILE: {
        // While loop must start with 'while' keyword
        if(tokenList->active->token->type != T_KW_WHILE) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        // Condition must be in parentheses
        if(tokenList->active->token->type != T_LPAREN) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        ast_add_new_node(&current_class, AST_WHILE_LOOP);

        // Parse loop condition
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

        // Set loop body as current context
        current_class->current = current_class->current->current->data.while_loop.body;

        // Parse loop body
        has_own_block = true;
        err = parser(tokenList, out_ast, GRAMMAR_BODY);
        if (err != SUCCESS) {
            return err;

        }
        
        break;
    }
    case GRAMMAR_GETTER: {
        // Getter must start with 'static' keyword
        if(tokenList->active->token->type != T_KW_STATIC) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        // Getter name must be identifier
        if(tokenList->active->token->type != T_IDENT) {
            return ERR_SYN;
        }

        // Create getter node in AST
        ast_add_new_node(&current_class, AST_GETTER);
        current_class->current->current->data.getter.name = tokenList->active->token->value->data;
        DLLTokens_Next(tokenList);

        // Set getter body as current context
        current_class->current = current_class->current->current->data.getter.body;

        // Parse getter body
        has_own_block = true;
        int err = parser(tokenList, out_ast, GRAMMAR_BODY);
        if (err != SUCCESS) {
            return err;
        }

        break;
    }
    case GRAMMAR_SETTER: {
        // Setter must start with 'static' keyword
        if(tokenList->active->token->type != T_KW_STATIC) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        // Setter name must be identifier
        if(tokenList->active->token->type != T_IDENT) {
            return ERR_SYN;
        }

        // Create setter node in AST
        ast_add_new_node(&current_class, AST_SETTER);
        current_class->current->current->data.setter.name = tokenList->active->token->value->data;
        DLLTokens_Next(tokenList);

        // Setter uses assignment syntax: static name = (param) { body }
        if(tokenList->active->token->type != T_ASSIGN) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);
        // Parameter must be in parentheses
        if(tokenList->active->token->type != T_LPAREN) {
            return ERR_SYN;
        }
        
        DLLTokens_Next(tokenList);
        // Parameter name must be identifier
        if(tokenList->active->token->type != T_IDENT) {
            return ERR_SYN;
        }
        current_class->current->current->data.setter.param = tokenList->active->token->value->data;
        DLLTokens_Next(tokenList);
        
        if(tokenList->active->token->type != T_RPAREN) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        // Set setter body as current context
        current_class->current = current_class->current->current->data.setter.body;

        // Parse setter body
        has_own_block = true;
        int err = parser(tokenList, out_ast, GRAMMAR_BODY);
        if (err != SUCCESS) {
            return err;
        }

        break;
    }
    case GRAMMAR_IFJ_CALL: {
        // IFJ call must start with 'Ifj' identifier
        if(tokenList->active->token->type != T_IDENT || strcmp(tokenList->active->token->value->data, "Ifj") != 0) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        // Must be followed by dot operator
        if(tokenList->active->token->type != T_DOT) {
            return ERR_SYN;
        }
        DLLTokens_Next(tokenList);

        // Function name must be identifier
        if(tokenList->active->token->type != T_IDENT) {
            return ERR_SYN;
        }

        // Create IFJ function call node in AST
        ast_add_new_node(&current_class, AST_IFJ_FUNCTION);
        current_class->current->current->data.ifj_function->name = tokenList->active->token->value->data;
        DLLTokens_Next(tokenList);

        // Parse function call parameters
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

