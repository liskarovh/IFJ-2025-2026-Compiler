
#include "expressions.h"
#include <string.h>


int prec_table[TABLE_SIZE][TABLE_SIZE] = 
{
//    */ | +- |  r | is | EQ | (  |  i |  ) |  $  |
    { '>', '>', '>', '>', '>', '<', '<', '>', '>' },
    { '<', '>', '>', '>', '>', '<', '<', '>', '>' },
    { '<', '<', '>', '>', '>', '<', '<', '>', '>' },
    { '<', '<', '<', '>', '>', '<', '<', '>', '>' },
    { '<', '<', '<', '<', '>', '<', '<', '>', '>' },
    { '<', '<', '<', '<', '<', '<', '<', '=', ' ' },
    { '>', '>', '>', '>', '>', ' ', ' ', '>', '>' },
    { '>', '>', '>', '>', '>', ' ', ' ', '>', '>' },
    { '<', '<', '<', '<', '<', '<', '<', ' ', ' ' }
};

prec_table_index get_prec_index(prec_table_enum symbol){

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

        default:
        return I_DOLLAR;
    }
}


prec_table_enum token_to_expr(tokenPtr token) {
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
        case T_IDENT: return ID;
        case T_GLOB_IDENT: return ID;
        case T_KW_IS: return IS;
        

        default:
            return DOLLAR;
    }
}

bool reduce_rule(stack *stack) {
    if(!stack->top) {
        return false;
    }

    expr_item top = *(expr_item *)stack->top->data;
    if(top.symbol == INT || top.symbol == FLOAT || top.symbol == STRING || top.symbol == ID || top.symbol == NULL_VAR) {
        expr_item item = *(expr_item *)stack_pop(stack);

        if (item.expr == NULL) { 
            item.expr = malloc(sizeof(struct ast_expression));
            if(item.expr == NULL) {
                return false; 
            }

            if(top.symbol == ID) {
                if(item.token != NULL) {
                    item.expr->type = AST_IDENTIFIER;
                    item.expr->operands.identifier.value = item.token->value->data;
                } else {
                    item.expr->type = AST_IFJ_FUNCTION_EXPR;
                }
            } else {
                item.expr->type = AST_VALUE;
                item.expr->operands.identity.value_type = 
                    (item.symbol == INT) ? AST_VALUE_INT :
                    (item.symbol == FLOAT) ? AST_VALUE_FLOAT :
                    (item.symbol == STRING) ? AST_VALUE_STRING:
                    AST_VALUE_NULL;
                if(item.symbol == INT) {
                    item.expr->operands.identity.value.int_value = item.token->value_int;
                } else if(item.symbol == FLOAT) {
                    item.expr->operands.identity.value.double_value = item.token->value_float;
                } else {
                    item.expr->operands.identity.value.string_value = item.token->value->data;
                }
            }
        }
        item.symbol = EXPR;

        top = *(expr_item *)stack->top->data;
        if(top.symbol == SHIFT_MARK) {
            expr_item *p = (expr_item *)stack_pop(stack); 
            if(p) free(p);
        }

        stack_push_value(stack, &item, sizeof(item));
        return true;
    }
    else if(top.symbol == ID) {
        expr_item item = *(expr_item *)stack_pop(stack);
        item.expr = malloc(sizeof(struct ast_expression));
        item.expr->type = AST_ID;
        item.expr->operands.identifier.value = item.token->value->data;
        item.symbol = EXPR;

        top = *(expr_item *)stack->top->data;
        if(top.symbol == SHIFT_MARK) {
            expr_item *p = (expr_item *)stack_pop(stack); 
            if(p) free(p);
        }

        stack_push_value(stack, &item, sizeof(item));
        return true;
    }
    else if(stack->top != NULL && stack->top->next != NULL && stack->top->next->next != NULL) {
        expr_item middle = *(expr_item *)stack->top->next->data;
        expr_item bottom = *(expr_item *)stack->top->next->next->data;

        if(top.symbol == EXPR && bottom.symbol == EXPR) {
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
                    expr_item newExpr;
                    newExpr.symbol = EXPR;
                    newExpr.expr = malloc(sizeof(struct ast_expression));
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
                    p = stack_pop(stack); 
                    newExpr.expr->operands.binary_op.right = p->expr;
                    if(p) free(p);

                    p = stack_pop(stack); free(p);
                    
                    p = stack_pop(stack);
                    newExpr.expr->operands.binary_op.left = p->expr;
                    if (p) free(p);

                    top = *(expr_item *)stack->top->data;
                    if(top.symbol == SHIFT_MARK) {
                        expr_item *p = (expr_item *)stack_pop(stack); 
                        if(p) free(p);
                    }

                    stack_push_value(stack, &newExpr, sizeof(newExpr));
                    return true;
                }
                default:
                    return false;
                    break;
            }
        }
        else if(top.symbol == RIGHT_PAREN && middle.symbol == EXPR && bottom.symbol == LEFT_PAREN) {
            void *p;
            expr_item *expr;
            p = stack_pop(stack); if (p) free(p);
            expr = stack_pop(stack); 
            p = stack_pop(stack); if (p) free(p);

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

void push_shift(stack *s) {
    stack tmpStack;
    stack_init(&tmpStack);

    while((*(expr_item *)s->top->data).symbol != DOLLAR && (*(expr_item *)s->top->data).symbol != SHIFT_MARK) {
        void *p = stack_pop(s);
        stack_push_value(&tmpStack, p, sizeof(expr_item));
    }

    if((*(expr_item *)s->top->data).symbol != SHIFT_MARK) {
        prec_table_enum shift = SHIFT_MARK;
        stack_push_value(s, &shift, sizeof(shift));
    }

    while(!stack_is_empty(&tmpStack)) {
        void *p = stack_pop(&tmpStack);
        stack_push_value(s, p, sizeof(expr_item));
    }
}

prec_table_enum get_top_terminal(stack *stack) {
    stack_item *current = stack->top;
    while(current != NULL) {
        expr_item item = *(expr_item *)current->data;
        if(item.symbol != SHIFT_MARK && item.symbol != EXPR) {
            return item.symbol;
        }
        current = current->next;
    }
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

    expr_item dollar;
    dollar.symbol = DOLLAR;
    dollar.token = NULL;
    stack_push_value(&stack, &dollar, sizeof(dollar));

    tokenPtr token = tokenlist->active->token;

    int bracked_depth = 0;

    prec_table_enum input = token_to_expr(token);
    while (true){
        void *topdata = stack_top(&stack);
        if (!topdata) return ERR_SYN;
        prec_table_enum top_sym = get_top_terminal(&stack);

        char rel = prec_table[get_prec_index(top_sym)][get_prec_index(input)];

        if(rel == '<' || rel == '='){
            if(rel == '<'){
                push_shift(&stack);
            }

            if(input == LEFT_PAREN){
                bracked_depth++;
            }
            else if(input == RIGHT_PAREN){
                bracked_depth--;
                if(input == RIGHT_PAREN && bracked_depth < 0){
                    break;
                }
            }
            if (input == ID && tokenlist->active->next != NULL && tokenlist->active->next->token->type == T_LPAREN) {
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
                

                item.expr->operands.function_call->parameters = NULL;
                struct ast_parameter *last_param = NULL;

                while (tokenlist->active->token->type != T_RPAREN) {
                    int type = tokenlist->active->token->type;
                    if(type != T_IDENT && type != T_STRING && type != T_ML_STRING && 
                        type != T_FLOAT && type != T_INT && 
                        type != T_BOOL_FALSE && type != T_BOOL_TRUE) {
                        return ERR_SYN;
                    }

                    struct ast_parameter *new_param = malloc(sizeof(struct ast_parameter));
                    if (new_param == NULL) {
                        return ERR_INTERNAL;
                    }
                    new_param->name = tokenlist->active->token->value->data;
                    new_param->next = NULL;

                    if (item.expr->operands.function_call->parameters == NULL) {
                        item.expr->operands.function_call->parameters = new_param;
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
            } else if(input == ID && strcmp(token->value->data, "Ifj") == 0) {
                DLLTokens_Next(tokenlist);
                if(tokenlist->active->token->type != T_DOT) return ERR_SYN;
                DLLTokens_Next(tokenlist);
                if(tokenlist->active->token->type != T_IDENT) return ERR_SYN;
                
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
                

                item.expr->operands.ifj_function->parameters = NULL;
                struct ast_parameter *last_param = NULL;

                while (tokenlist->active->token->type != T_RPAREN) {
                    int type = tokenlist->active->token->type;
                    if(type != T_IDENT && type != T_STRING && type != T_ML_STRING && 
                        type != T_FLOAT && type != T_INT && 
                        type != T_BOOL_FALSE && type != T_BOOL_TRUE) {
                        return ERR_SYN;
                    }

                    struct ast_parameter *new_param = malloc(sizeof(struct ast_parameter));
                    if (new_param == NULL) {
                        return ERR_INTERNAL;
                    }
                    new_param->name = tokenlist->active->token->value->data;
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
                /* push a copy of input */
                expr_item item;
                item.symbol = input;
                item.token = tokenlist->active->token;
                item.expr = NULL;
                stack_push_value(&stack, &item, sizeof(expr_item));
            }

            DLLTokens_Next(tokenlist);
            token = tokenlist->active->token;
            input = token_to_expr(token);
        }
        else if( rel == '>'){ 
            if(!reduce_rule(&stack)) {
                return ERR_SYN;
            }
        }
        else {
            if(input == RIGHT_PAREN && bracked_depth == 0) {
                break;
            }

            return ERR_SYN;
        }

        if (input == DOLLAR) { 
            if(!reduce_rule(&stack)) {
                return ERR_SYN;
            }
            break;
        }
    }

    if(stack.top == NULL || stack.top->next == NULL) {
        return ERR_SYN;
    }

    while(!((*(expr_item *)stack.top->data).symbol == EXPR && (*(expr_item *)stack.top->next->data).symbol == DOLLAR)) {
        if(!reduce_rule(&stack)) {
            return ERR_SYN;

        }
    }

    stack_item *result = stack.top;
    *out_ast = (*(expr_item *)result->data).expr;

    while(stack.top != NULL) {
        void *p = stack_pop(&stack);
        free(p);
    }

    return SUCCESS;
}