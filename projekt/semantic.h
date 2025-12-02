/**
 * @file semantic.h
 * @brief IFJ25 Semantic analysis.
 * @authors
 *  - Hana Liškařová (xliskah00)
 *  - Maťej Kurta (xkurtam00)
 *
 * The semantic module runs all static checks required on the AST and prepares metadata for the code generator.
 *  - seeds Ifj.* built-ins into global function table,
 *  - collects user function/getter/setter signatures,
 *  - checks identifier definitions, calls, loops and literal-only expressions,
 *  - tracks globals  "__".
 */

#ifndef SEMANTIC_H
#define SEMANTIC_H

#include <stdbool.h>

#include "ast.h"
#include "symtable.h"    /* symtable* */
#include "scope_stack.h" /* scope_stack */

/**
 * @brief Maximum nesting depth for semantic scope IDs ("1", "1.1", "1.1.2", ...).
 */
#define SEM_MAX_SCOPE_DEPTH 32

/**
 * @brief Maximum length of a textual scope path (e.g. "1.2.3.4").
 */
#define SEM_MAX_SCOPE_PATH  64

/**
 * @brief One frame in the semantic scope-ID stack.
 */
typedef struct sem_scope_id_frame {
    char path[SEM_MAX_SCOPE_PATH]; /**< textual scope path ("1", "1.2", "1.2.3", ...) */
    int child_count; /**< numbr of child scopes created */
} sem_scope_id_frame;

/**
 * @brief Stack of scope IDs used to generate nested scope labels.
 */
typedef struct sem_scope_id_stack {
    sem_scope_id_frame frames[SEM_MAX_SCOPE_DEPTH]; /**< frames for nested scopes */
    int depth; /**< current depth (-1 = none, 0 = root, ...) */
} sem_scope_id_stack;

/**
 * @brief Semantic analysis working context.
 */
typedef struct semantic_ctx {
    symtable *funcs; /**< global function signature table */
    scope_stack scopes; /**< local scopes stack (blocks/functions) for lookup/redeclare */
    sem_scope_id_stack ids; /**< stack of textual scope IDs ("1", "1.1", ...) */
    int loop_depth; /**< nesting counter for loop checks */
    bool seen_main; /**< true if main() with 0 params found */
} semantic;

/**
 * @brief Runs full semantic analysis on given AST.
 *
 * Initializes the semantic context, performs Pass 1 (headers + bodies),
 * then Pass 2 (identifier and call checks), frees all internal tables and
 * returns the resulting error code.
 *
 * @param tree Program AST root.
 * @return 0 (SUCCESS) on success, otherwise error code (3/4/5/6/10/99).
 */
int semantic_pass1(ast tree);


/**
 * @brief Collects names of all used globals ("__name") during semantic analysis.
 *
 * @param out_globals  Output pointer to dynamically allocated array of char*.
 * @param out_count    Output pointer to number of elements in the array.
 */
int semantic_get_globals(char ***out_globals, size_t *out_count);


#endif /* SEMANTIC_H */
