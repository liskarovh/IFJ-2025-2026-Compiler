
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
        prec_table_enum top    = *(prec_table_enum *)stack->top->data;
        prec_table_enum middle = *(prec_table_enum *)stack->top->next->data;
        prec_table_enum bottom = *(prec_table_enum *)stack->top->next->next->data;

        if (top == ID && bottom == ID) {
            switch (middle) {
                case PLUS: case MINUS: case MUL: case DIV:
                case LT: case LTEQ: case GT: case GTEQ:
                case EQ: case N_EQ: case IS: {
                    void *p;
                    p = stack_pop(stack); if (p) free(p);
                    p = stack_pop(stack); if (p) free(p);
                    p = stack_pop(stack); if (p) free(p);
                    prec_table_enum id = ID;
                    stack_push_value(stack, &id, sizeof(id));
                    return true;
                }
                default:
                    break;
            }
        }

        // ( E )
        if (top == RIGHT_PAREN && middle == ID && bottom == LEFT_PAREN) {
            void *p;
            p = stack_pop(stack); if (p) free(p);
            p = stack_pop(stack); if (p) free(p);
            p = stack_pop(stack); if (p) free(p);
            prec_table_enum id = ID;
            stack_push_value(stack, &id, sizeof(id));
            return true;
        }
    }

    void *topdata = stack_top(stack);
    if (topdata) {
        if (*(prec_table_enum *)topdata == ID)
            return true;
    }

    return false;
}




int parse_expr(DLListTokens *tokenlist){
    stack stack;
    stack_init(&stack);

    prec_table_enum dollar = DOLLAR;
    stack_push_value(&stack, &dollar, sizeof(dollar));

    tokenPtr token = tokenlist->active->token;

    prec_table_enum input = token_to_expr(token);
    while (1){
        void *topdata = stack_top(&stack);
        if (!topdata) return ERR_SYN;
        prec_table_enum top_sym = *(prec_table_enum *)topdata;

        char rel = prec_table[get_prec_index(top_sym)][get_prec_index(input)];

        if(rel == '<' || rel == '='){
            /* push a copy of input */
            stack_push_value(&stack, &input, sizeof(input));
            DLLTokens_Next(tokenlist);
            token = tokenlist->active->token;
            input = token_to_expr(token);
        }

        else if( rel == '>'){ 
            if(!reduce_rule(&stack)) return ERR_SYN;
        }

        if ( (*(prec_table_enum *)stack_top(&stack) == DOLLAR) && input == DOLLAR) break;
    }
    return 0;
}