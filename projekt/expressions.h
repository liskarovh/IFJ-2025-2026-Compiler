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
    SHIFT_MARK,
    EXPR,
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

typedef struct expr_item
{
    prec_table_enum symbol;
    tokenPtr token;
    ast_expression expr;
} expr_item;

typedef enum {

NT_MUL_NT,  //E → E * E
NT_DIV_NT,  //E → E / E
NT_PLUS_NT, //E → E + E
NT_MINUS_NT,//E → E - E
NT_LT_NT,   //E → E < E
NT_LEQ_NT,  //E → E <= E
NT_GT_NT,   //E → E > E
NT_GEQ_NT,  //E → E >= E
NT_EQ_NT,   //E → E == E
NT_NEQ_NT,  //E → E != E
NT_IS_NT,   //E → E is E
PAR_NT_PAR, //E → ( E )
NT_ID      //E → i
} prec_rules;

typedef struct expr_rule {
    prec_rules rule;
    tokenPtr token;
    struct expr_rule *next;
} expr_rule;

/// @brief Checks the precedence table and returns the error code and list of applied rules
/// @param tokenlist List of tokens from the scanner
/// @param out_ast Pointer to store the constructed AST expression
/// @return Error code indicating success or failure
int parse_expr(DLListTokens *tokenlist, ast_expression *out_ast);

#endif // EXPRESSIONS_H