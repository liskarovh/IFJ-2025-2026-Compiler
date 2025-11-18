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

typedef struct generator{
    string output;
    unsigned counter;
}* generator;

enum arity{
    ARITY_UNARY,
    ARITY_BINARY,
    ARITY_UNDEFINED
};

void init_code(generator gen, ast syntree);
void generate_code(generator gen, ast syntree);

#endif