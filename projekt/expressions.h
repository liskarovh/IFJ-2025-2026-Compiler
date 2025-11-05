#ifndef EXPRESSIONS_H
#define EXPRESSIONS_H

#include <stdlib.h>
#include "stack.h"
#include "error.h"
#include "scanner.h"
#include "token.h"
#include "parser.h"
#include "ast.h"

#define TABLE_SIZE 9

typedef enum {

    INT,
    FLOAT,
    STRING,
    MUL,
    DIV,
    PLUS,
    MINUS,
    LT,
    LTEQ,
    GT,
    GTEQ,
    IS,
    EQ,
    N_EQ,
    LEFT_PAREN,
    RIGHT_PAREN,
    ID,
    DOLLAR


} prec_table_enum;

typedef enum {
    I_MUL_DIV,
    I_PLUS_MIN,
    I_RELATION,
    I_IS,
    I_EQ_NEQ,
    I_LEFT_BRAC,
    I_DATA,
    I_RIGHT_BRAC,
    I_DOLLAR
}prec_table_index ;

/*
typedef struct stack_item
{
    prec_table_enum symbol;
    struct stack_item *next;
} stack_item;


typedef struct {
    stack_item *top;
} expr_stack;
*/

typedef enum {

NT_MUL_NT,  //E → E * E
NT_DIV_NT,  //E → E / E
NT_PLUS_NT, //E → E + E
NT_MINUS_NT,//E → E - E
NT_LE_NT,   //E → E < E
NT_LEQ_NT,  //E → E <= E
NT_GT_NT,   //E → E > E
NT_GEQ_NT,  //E → E >= E
NT_EQ_NT,   //E → E == E
NT_NEQ_NT,  //E → E != E
NT_IS_NT,   //E → E is E
PAR_NT_PAR, //E → ( E )
NT_ID      //E → i
} prec_rules;




int parse_expr(DLListTokens *tokenlist);

#endif // EXPRESSIONS_H