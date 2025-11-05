
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
        case IS:
            return I_IS;
        case LEFT_PAREN:
            return I_LEFT_BRAC;
        case ID:
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
        case T_ASSIGN: return EQ;
        case T_NEQ: return N_EQ;
        
        case T_LPAREN: return LEFT_PAREN;
        case T_RPAREN: return RIGHT_PAREN;
        
        case T_INT: return INT;
        case T_FLOAT: return FLOAT;
        case T_STRING: return STRING;
        case T_IDENT: return ID;
        case T_KW_IS: return IS;
        

        default:
            return DOLLAR;
    }
}

bool reduce_rule(stack *stack) {
    if (stack->top && stack->top->next && stack->top->next->next) {
        prec_table_enum top    = stack->top->data;
        prec_table_enum middle = stack->top->next->data;
        prec_table_enum bottom = stack->top->next->next->data;

        if (top == ID && bottom == ID) {
            switch (middle) {
                case PLUS: case MINUS: case MUL: case DIV:
                case LT: case LTEQ: case GT: case GTEQ:
                case EQ: case N_EQ: case IS:
                    stack_pop(stack);
                    stack_pop(stack);
                    stack_pop(stack);
                    stack_push(stack, ID);
                    return true;
            }
        }

        // ( E )
        if (top == RIGHT_PAREN && middle == ID && bottom == LEFT_PAREN) {
            stack_pop(stack);
            stack_pop(stack);
            stack_pop(stack);
            stack_push(stack, ID);
            return true;
        }
    }

    if (stack_top(stack) == ID)
        return true;

    return false;
}




int parse_expr(DLListTokens *tokenlist){
    stack *stack;
    stack_init(&stack);

    stack_push(&stack, DOLLAR);

    tokenPtr token = tokenlist->active->token;

    prec_table_enum input = token_to_expr(token);
    while (1){
    char rel = prec_table[get_prec_index(*(prec_table_enum *)stack_top(stack))][get_prec_index(input)];
   
    if(rel == '<' || rel == '='){
        stack_push(stack,&input);
        DLLTokens_Next(tokenlist);
        token = tokenlist->active->token;
        input = token_to_expr(token);
    }

    else if( rel == '>'){ 
    if(!reduce_rule(&stack)) return ERR_SYN;
    }
    if (stack_top(&stack) == DOLLAR && input == DOLLAR) break;
}
    return 0;
}