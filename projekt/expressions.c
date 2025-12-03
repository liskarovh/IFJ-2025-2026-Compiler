/**
* @authors
*   -   Martin Bíško (xbiskom00)
*   -   Maťej Kurta (xkurtam00)
 *
 * @file expressions.h
 * @brief Precedence parser for expressions.
 * BUT FIT
 */

#include "expressions.h"
#include <string.h>

/*
 * @brief Precedence table 
 */
int prec_table[TABLE_SIZE][TABLE_SIZE] = 
{
//    */ | +- |  r | is | EQ | (  |  i |  ) |  $  |
    { '>', '>', '>', '>', '>', '<', '<', '>', '>' },  // */ 
    { '<', '>', '>', '>', '>', '<', '<', '>', '>' },  // +- 
    { '<', '<', '>', '>', '>', '<', '<', '>', '>' },  // r  
    { '<', '<', '<', '>', '>', '<', '<', '>', '>' },  // is
    { '<', '<', '<', '<', '>', '<', '<', '>', '>' },  // EQ 
    { '<', '<', '<', '<', '<', '<', '<', '=', ' ' },  // (  
    { '>', '>', '>', '>', '>', ' ', ' ', '>', '>' },  // i  
    { '>', '>', '>', '>', '>', ' ', ' ', '>', '>' },  // )  
    { '<', '<', '<', '<', '<', '<', '<', ' ', ' ' }   // $  
};

/*
 * @brief Get precedence table index from symbol
 * @param symbol prec_table_enum symbol
 * @return prec_table_index index
 */
prec_table_index get_prec_index(prec_table_enum symbol){
    // Map symbol to corresponding row/column in precedence table
    switch (symbol){
        case MUL:
        case DIV:
            return I_MUL_DIV;
        case PLUS:
        case MINUS:
            return I_PLUS_MIN;
        case LT:
        case LTEQ:
        case GT:
        case GTEQ:
            return I_RELATION;
        case EQ:
        case N_EQ:
            return I_EQ_NEQ;
        case IS:
            return I_IS;
        case LEFT_PAREN:
            return I_LEFT_BRAC;
        case ID:
        case INT:
        case FLOAT:
        case STRING:
        case NULL_VAR:
            return I_DATA;
        case RIGHT_PAREN:
            return I_RIGHT_BRAC;

        // Everything else maps to dollar (end of expression)
        default:
        return I_DOLLAR;
    }
}

/*
 * @brief Convert token to prec_table_enum symbol
 * @param token token pointer
 * @return prec_table_enum symbol
 */
prec_table_enum token_to_expr(tokenPtr token) {
    // Map scanner token types to expression symbols
    switch (token->type) {
        case T_PLUS: return PLUS;
        case T_MINUS: return MINUS;
        case T_MUL: return MUL;
        case T_DIV: return DIV;
        
        case T_LT: return LT;
        case T_LE: return LTEQ;
        case T_GT: return GT;
        case T_GE: return GTEQ;
        case T_EQ: return EQ;
        case T_NEQ: return N_EQ;
        
        case T_LPAREN: return LEFT_PAREN;
        case T_RPAREN: return RIGHT_PAREN;
        
        case T_KW_NULL: return NULL_VAR;
        case T_INT: return INT;
        case T_FLOAT: return FLOAT;
        case T_STRING: return STRING;
        case T_ML_STRING: return STRING;  // Multiline string same as regular
        case T_IDENT: return ID;
        case T_GLOB_IDENT: return ID;     // Global identifiers treated as ID
        case T_KW_NUM: return ID;
        case T_KW_IS: return IS;
        
        // Any other token ends the expression
        default:
            return DOLLAR;
    }
}

/*
 * @brief Reduce rule in precedence parser
 * @param stack stack pointer
 * @return true if reduction was successful, false otherwise
 */
bool reduce_rule(stack *stack) {
    if(!stack->top) {
        return false;
    }

    expr_item top = *(expr_item *)stack->top->data;
    // Rule: i -> E (reduce identifier/literal to expression)
    if(top.symbol == INT || top.symbol == FLOAT || top.symbol == STRING || top.symbol == ID || top.symbol == NULL_VAR) {
        expr_item item = *(expr_item *)stack_pop(stack);

        if (item.expr == NULL) { 
            // Create new AST expression node for this value
            item.expr = malloc(sizeof(struct ast_expression));
            if(item.expr == NULL) {
                return false; 
            }

            // Identifiers need special handling
            if(top.symbol == ID) {
                if(item.token != NULL) {
                    item.expr->type = AST_IDENTIFIER;
                    item.expr->operands.identifier.value = item.token->value->data;
                } else {
                    // Already processed as function call
                    item.expr->type = AST_IFJ_FUNCTION_EXPR;
                }
            } else {
                // Literal value - determine type
                item.expr->type = AST_VALUE;
                item.expr->operands.identity.value_type = 
                    (item.symbol == INT) ? AST_VALUE_INT :
                    (item.symbol == FLOAT) ? AST_VALUE_FLOAT :
                    (item.symbol == STRING) ? AST_VALUE_STRING:
                    AST_VALUE_NULL;
                // Store actual value based on type
                if(item.symbol == INT) {
                    item.expr->operands.identity.value.int_value = item.token->value_int;
                } else if(item.symbol == FLOAT) {
                    item.expr->operands.identity.value.double_value = item.token->value_float;
                } else {
                    item.expr->operands.identity.value.string_value = item.token->value->data;
                }
            }
        }
        // Mark as reduced expression
        item.symbol = EXPR;

        // Remove shift marker if present
        top = *(expr_item *)stack->top->data;
        if(top.symbol == SHIFT_MARK) {
            expr_item *p = (expr_item *)stack_pop(stack); 
            if(p) free(p);
        }

        // Push reduced expression back
        stack_push_value(stack, &item, sizeof(item));
        return true;
    }
    // Rule: E op E -> E (binary operations)
    else if(stack->top != NULL && stack->top->next != NULL && stack->top->next->next != NULL) {
        // Check for E op E pattern on stack
        expr_item middle = *(expr_item *)stack->top->next->data;
        expr_item bottom = *(expr_item *)stack->top->next->next->data;

        // Both operands must be expressions
        if(top.symbol == EXPR && bottom.symbol == EXPR) {
            // Middle must be an operator
            switch (middle.symbol) {
                case MUL: 
                case DIV:
                case PLUS:
                case MINUS:
                case LT:
                case LTEQ:
                case GT:
                case GTEQ:
                case EQ:
                case N_EQ:
                case IS: {
                    // Create new expression node for binary operation
                    expr_item newExpr;
                    newExpr.symbol = EXPR;
                    newExpr.expr = malloc(sizeof(struct ast_expression));
                    // Determine AST node type based on operator
                    newExpr.expr->type = 
                        (middle.symbol == MUL) ? AST_MUL :
                        (middle.symbol == DIV) ? AST_DIV :
                        (middle.symbol == PLUS) ? AST_ADD :
                        (middle.symbol == MINUS) ? AST_SUB :
                        (middle.symbol == LT) ? AST_LT :
                        (middle.symbol == LTEQ) ? AST_LE :
                        (middle.symbol == GT) ? AST_GT :
                        (middle.symbol == GTEQ) ? AST_GE :
                        (middle.symbol == EQ) ? AST_EQUALS :
                        (middle.symbol == N_EQ) ? AST_NOT_EQUAL :
                        AST_IS;
                    expr_item *p;
                    // Pop right operand
                    p = stack_pop(stack); 
                    newExpr.expr->operands.binary_op.right = p->expr;
                    if(p) free(p);

                    // Pop operator (discard)
                    p = stack_pop(stack); free(p);
                    
                    // Pop left operand
                    p = stack_pop(stack);
                    newExpr.expr->operands.binary_op.left = p->expr;
                    if (p) free(p);

                    // Remove shift marker if present
                    top = *(expr_item *)stack->top->data;
                    if(top.symbol == SHIFT_MARK) {
                        expr_item *p = (expr_item *)stack_pop(stack); 
                        if(p) free(p);
                    }

                    // Push the new combined expression
                    stack_push_value(stack, &newExpr, sizeof(newExpr));
                    return true;
                }
                default:
                    return false;
                    break;
            }
        }
        // Rule: ( E ) -> E (parenthesized expression)
        else if(top.symbol == RIGHT_PAREN && middle.symbol == EXPR && bottom.symbol == LEFT_PAREN) {
            void *p;
            expr_item *expr;
            // Pop right paren
            p = stack_pop(stack); if (p) free(p);
            // Keep the expression
            expr = stack_pop(stack); 
            // Pop left paren
            p = stack_pop(stack); if (p) free(p);

            // Remove shift marker if present
            top = *(expr_item *)stack->top->data;
            if(top.symbol == SHIFT_MARK) {
                expr_item *p = (expr_item *)stack_pop(stack); 
                if(p) free(p);
            }

            stack_push_value(stack, expr, sizeof(expr_item));
            return true;
        }
    }

    return false;
}

/*
 * @brief Push SHIFT_MARK onto the stack
 * Inserts shift marker below non-terminal symbols on top
 * @param s stack pointer
 */
void push_shift(stack *s) {
    // Temporary stack to hold items above the insertion point
    stack tmpStack;
    stack_init(&tmpStack);

    // Pop all non-terminals from top until we find terminal or dollar
    while((*(expr_item *)s->top->data).symbol != DOLLAR && (*(expr_item *)s->top->data).symbol != SHIFT_MARK) {
        void *p = stack_pop(s);
        stack_push_value(&tmpStack, p, sizeof(expr_item));
    }

    // Insert shift marker if not already present
    if((*(expr_item *)s->top->data).symbol != SHIFT_MARK) {
        prec_table_enum shift = SHIFT_MARK;
        stack_push_value(s, &shift, sizeof(shift));
    }

    // Put non-terminals back on top
    while(!stack_is_empty(&tmpStack)) {
        void *p = stack_pop(&tmpStack);
        stack_push_value(s, p, sizeof(expr_item));
    }
}

/*
 * @brief Get the top terminal symbol from the stack
 * Skips non-terminal symbols (EXPR) and shift markers
 * @param stack stack pointer
 * @return prec_table_enum top terminal symbol
 */
prec_table_enum get_top_terminal(stack *stack) {
    stack_item *current = stack->top;
    // Walk down the stack looking for terminal
    while(current != NULL) {
        expr_item item = *(expr_item *)current->data;
        // Skip non-terminals and markers
        if(item.symbol != SHIFT_MARK && item.symbol != EXPR) {
            return item.symbol;
        }
        current = current->next;
    }
    // No terminal found - return end marker
    return DOLLAR;
}

/// @brief Checks the precedence table and returns the error code and list of applied rules
/// @param tokenlist List of tokens from the scanner
/// @param out_ast Pointer to store the constructed AST expression
/// @return Error code indicating success or failure
int parse_expr(DLListTokens *tokenlist, ast_expression *out_ast){
    (void)out_ast;
    stack stack;
    stack_init(&stack);

    // Initialize stack with end marker (dollar)
    expr_item dollar;
    dollar.symbol = DOLLAR;
    dollar.token = NULL;
    stack_push_value(&stack, &dollar, sizeof(dollar));

    tokenPtr token = tokenlist->active->token;

    // Track parentheses depth to know when expression ends
    int bracked_depth = 0;

    prec_table_enum input = token_to_expr(token);
    // Main parsing loop
    while (true){
        void *topdata = stack_top(&stack);
        if (!topdata) return ERR_SYN;
        prec_table_enum top_sym = get_top_terminal(&stack);

        // Look up action in precedence table
        char rel = prec_table[get_prec_index(top_sym)][get_prec_index(input)];

        // Shift operation - push input onto stack
        if(rel == '<' || rel == '='){
            // For '<', insert shift marker before pushing
            if(rel == '<'){
                push_shift(&stack);
            }

            // Track parentheses for proper expression boundary
            if(input == LEFT_PAREN){
                bracked_depth++;
            }
            else if(input == RIGHT_PAREN){
                bracked_depth--;
                // Negative depth means we hit closing paren of outer construct
                if(input == RIGHT_PAREN && bracked_depth < 0){
                    break;
                }
            }
            // Handle function calls in expressions
            if (input == ID && tokenlist->active->next != NULL && tokenlist->active->next->token->type == T_LPAREN) {
                // Regular function call - create AST node for it
                expr_item item;
                item.symbol = input;
                item.token = NULL;
                item.expr = malloc(sizeof(struct ast_expression));
                if (item.expr == NULL) return ERR_INTERNAL;
                item.expr->type = AST_FUNCTION_CALL;
                item.expr->operands.function_call = malloc(sizeof(struct ast_fun_call));
                item.expr->operands.function_call->name = tokenlist->active->token->value->data;
                DLLTokens_Next(tokenlist);
                
                if(tokenlist->active->token->type != T_LPAREN) {
                    return ERR_SYN;
                }
                DLLTokens_Next(tokenlist);
                
                // Initialize parameter list
                item.expr->operands.function_call->parameters = NULL;
                struct ast_parameter *last_param = NULL;

                // Parse all parameters until closing paren
                while (tokenlist->active->token->type != T_RPAREN) {
                    // Validate parameter token type
                    int type = tokenlist->active->token->type;
                    if(type != T_IDENT && type != T_STRING && type != T_ML_STRING && 
                        type != T_FLOAT && type != T_INT && 
                        type != T_BOOL_FALSE && type != T_BOOL_TRUE &&
                        type != T_GLOB_IDENT) {
                        return ERR_SYN;
                    }

                    // Allocate new parameter node
                    struct ast_parameter *new_param = malloc(sizeof(struct ast_parameter));
                    if (new_param == NULL) {
                        return ERR_INTERNAL;
                    }
                    
                    // Set parameter value based on type
                    if(tokenlist->active->token->type == T_FLOAT) {
                        new_param->value_type = AST_VALUE_FLOAT;
                        new_param->value.double_value = tokenlist->active->token->value_float;
                    } else if (tokenlist->active->token->type == T_INT) {
                        new_param->value_type = AST_VALUE_INT;
                        new_param->value.int_value = tokenlist->active->token->value_int;
                    } else if (tokenlist->active->token->type == T_KW_NULL)
                        new_param->value_type = AST_VALUE_NULL;
                    else {
                        if (tokenlist->active->token->type == T_IDENT || tokenlist->active->token->type == T_GLOB_IDENT)
                            new_param->value_type = AST_VALUE_IDENTIFIER;
                        else new_param->value_type = AST_VALUE_STRING;
                        new_param->value.string_value = tokenlist->active->token->value->data;
                    }

                    new_param->next = NULL;

                    // Link parameter to the list
                    if (item.expr->operands.function_call->parameters == NULL) {
                        item.expr->operands.function_call->parameters = new_param;
                    } else {
                        last_param->next = new_param;
                    }
                    last_param = new_param;

                    DLLTokens_Next(tokenlist);

                    // Handle comma between parameters
                    if (tokenlist->active->token->type == T_COMMA) {
                        DLLTokens_Next(tokenlist);
                        if (tokenlist->active->token->type == T_RPAREN) {
                            return ERR_SYN;
                        }
                    } else if (tokenlist->active->token->type != T_RPAREN) {
                        return ERR_SYN;
                    }
                }

                stack_push_value(&stack, &item, sizeof(expr_item));
            // Handle IFJ builtin function calls (Ifj.something)
            } else if(input == ID && strcmp(token->value->data, "Ifj") == 0) {
                // Skip 'Ifj' and expect dot operator
                DLLTokens_Next(tokenlist);
                if(tokenlist->active->token->type != T_DOT) return ERR_SYN;
                // Get function name after dot
                DLLTokens_Next(tokenlist);
                if(tokenlist->active->token->type != T_IDENT) return ERR_SYN;
                
                // Create IFJ function expression node
                expr_item item;
                item.symbol = input;
                item.token = NULL;
                item.expr = malloc(sizeof(struct ast_expression));
                if (item.expr == NULL) return ERR_INTERNAL;
                item.expr->type = AST_IFJ_FUNCTION_EXPR;
                item.expr->operands.ifj_function = malloc(sizeof(struct ast_ifj_function));
                if (item.expr->operands.ifj_function == NULL) {
                    return ERR_INTERNAL;
                }
                item.expr->operands.ifj_function->name = tokenlist->active->token->value->data;
                DLLTokens_Next(tokenlist);
                
                if(tokenlist->active->token->type != T_LPAREN) {
                    return ERR_SYN;
                }
                DLLTokens_Next(tokenlist);
                
                // Initialize IFJ function parameters
                item.expr->operands.ifj_function->parameters = NULL;
                struct ast_parameter *last_param = NULL;

                // Parse IFJ function parameters
                while (tokenlist->active->token->type != T_RPAREN) {
                    int type = tokenlist->active->token->type;
                    if(type != T_IDENT && type != T_STRING && type != T_ML_STRING && 
                        type != T_FLOAT && type != T_INT && 
                        type != T_BOOL_FALSE && type != T_BOOL_TRUE &&
                        type != T_GLOB_IDENT && type != T_KW_NULL) {
                        return ERR_SYN;
                    }

                    struct ast_parameter *new_param = malloc(sizeof(struct ast_parameter));
                    if (new_param == NULL) {
                        return ERR_INTERNAL;
                    }
                    if(tokenlist->active->token->type == T_FLOAT) {
                        new_param->value_type = AST_VALUE_FLOAT;
                        new_param->value.double_value = tokenlist->active->token->value_float;
                    } else if (tokenlist->active->token->type == T_INT) {
                        new_param->value_type = AST_VALUE_INT;
                        new_param->value.int_value = tokenlist->active->token->value_int;
                    } else if (tokenlist->active->token->type == T_KW_NULL)
                        new_param->value_type = AST_VALUE_NULL;
                    else {
                        if (tokenlist->active->token->type == T_IDENT || tokenlist->active->token->type == T_GLOB_IDENT)
                            new_param->value_type = AST_VALUE_IDENTIFIER;
                        else new_param->value_type = AST_VALUE_STRING;
                        new_param->value.string_value = tokenlist->active->token->value->data;
                    }
                    new_param->next = NULL;

                    if (item.expr->operands.ifj_function->parameters == NULL) {
                        item.expr->operands.ifj_function->parameters = new_param;
                    } else {
                        last_param->next = new_param;
                    }
                    last_param = new_param;

                    DLLTokens_Next(tokenlist);

                    if (tokenlist->active->token->type == T_COMMA) {
                        DLLTokens_Next(tokenlist);
                        if (tokenlist->active->token->type == T_RPAREN) {
                            return ERR_SYN;
                        }
                    } else if (tokenlist->active->token->type != T_RPAREN) {
                        return ERR_SYN;
                    }
                }
                
                stack_push_value(&stack, &item, sizeof(expr_item));
            } else {
                // Simple value/identifier - just push it
                expr_item item;
                item.symbol = input;
                item.token = tokenlist->active->token;
                item.expr = NULL;
                stack_push_value(&stack, &item, sizeof(expr_item));
            }

            // Move to next token
            DLLTokens_Next(tokenlist);
            token = tokenlist->active->token;
            input = token_to_expr(token);
        }
        // Reduce operation - apply reduction rules
        else if( rel == '>'){ 
            if(!reduce_rule(&stack)) {
                return ERR_SYN;
            }
        }
        // Error in precedence table
        else {
            // Allow closing paren if at zero depth (end of expression)
            if(input == RIGHT_PAREN && bracked_depth == 0) {
                break;
            }

            return ERR_SYN;
        }

        // End of expression - do final reduction
        if (input == DOLLAR) { 
            if(!reduce_rule(&stack)) {
                return ERR_SYN;
            }
            break;
        }
    }

    // Validate final stack state
    if(stack.top == NULL || stack.top->next == NULL) {
        return ERR_SYN;
    }

    // Keep reducing until only EXPR $ remains
    while(!((*(expr_item *)stack.top->data).symbol == EXPR && (*(expr_item *)stack.top->next->data).symbol == DOLLAR)) {
        if(!reduce_rule(&stack)) {
            return ERR_SYN;

        }
    }

    // Extract result expression from stack
    stack_item *result = stack.top;
    *out_ast = (*(expr_item *)result->data).expr;

    // Clean up stack memory
    while(stack.top != NULL) {
        void *p = stack_pop(&stack);
        free(p);
    }

    return SUCCESS;
}