/**
 * @authors Šimon Dufek (xdufeks00)

 * @file codegen.h
 * 
 * @brief Code generator implementation using Syntactic tree
 * BUT FIT
 */

#ifndef IFJ_CODE_GEN
#define IFJ_CODE_GEN

#include "ast.h"
#include "string.h"
#include "stack.h"

/*
 * @brief Structure for loop labels
 */
typedef struct loop_labels {
    char *start_label;
    char *end_label;
} loop_labels_t;

/*
 * @brief Code generator structure
 */
typedef struct generator{
    string output;
    unsigned counter;
    stack loop_stack;
}* generator;

/*
 * @brief Enum for operator arity
 */
enum arity{
    ARITY_UNARY,
    ARITY_BINARY,
    ARITY_UNDEFINED
};

/*
 * @brief Function declarations
 * @param gen code generator
 * @param syntree syntax tree
 * @return void
 */
void init_code(generator gen, ast syntree);

/*
 * @brief Main Code generation
 * @param gen code generator
 * @param syntree syntax tree
 * @return void
 */
void generate_code(generator gen, ast syntree);

#endif