/**
 * @authors Šimon Dufek (xdufeks00)

 * @file codegen.h
 * 
 * Code generator implementation using Syntactic tree
 * BUT FIT
 */

#ifndef IFJ_CODE_GEN
#define IFJ_CODE_GEN

#include "ast.h"
#include "string.h"
#include "stack.h"

typedef struct loop_labels {
    char *start_label;
    char *end_label;
} loop_labels_t;

typedef struct generator{
    string output;
    unsigned counter;
    stack loop_stack;
}* generator;

enum arity{
    ARITY_UNARY,
    ARITY_BINARY,
    ARITY_UNDEFINED
};

void init_code(generator gen, ast syntree);
void generate_code(generator gen, ast syntree);

#endif