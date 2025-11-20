/**
 * @file semantic.h
 * @brief IFJ25 Semantic analysis – Pass 1 over AST.
 * @authors
 *  - Hana Liškařová (xliskah00)
 *
 * This module performs the first semantic pass over the AST:
 *  - loads built-in functions into the global function table,
 *  - collects function/getter/setter signatures (overload by arity),
 *  - walks bodies with a scope stack (locals & parameters),
 *  - fills a global symbol table with *all* declared symbols (incl. nested scopes),
 *  - performs immediate checks (codes 3/4/5/6/10).
 *
 * Error codes (from error.h):
 *  - ERR_DEF     (3)  : missing main() with 0 params
 *  - ERR_REDEF   (4)  : duplicate (name,arity) function; second getter/setter; local redeclare
 *  - ERR_ARGNUM  (5)  : wrong number of arguments in function call
 *  - ERR_EXPR    (6)  : literal-only type error in an expression
 *  - ERR_SEM     (10) : break/continue outside a loop
 *  - ERR_INTERNAL(99) : internal error (allocation, TS write, ...)
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
 *
 * Each frame holds:
 *  - textual path of this scope (e.g. "1", "1.2", "1.2.3"),
 *  - number of children already created under this scope (for numbering 1.1, 1.2, ...).
 */
typedef struct sem_scope_id_frame {
    char path[SEM_MAX_SCOPE_PATH]; /**< textual scope path ("1", "1.2", "1.2.3", ...) */
    int  child_count;              /**< how many child scopes were already created */
} sem_scope_id_frame;

/**
 * @brief Stack of scope IDs used to generate nested scope labels.
 *
 * This stack is independent of @ref scope_stack; it only tracks textual IDs
 * for debug / final symbol-table output (e.g. 1, 1.1, 1.1.2, ...).
 */
typedef struct sem_scope_id_stack {
    sem_scope_id_frame frames[SEM_MAX_SCOPE_DEPTH]; /**< frames for nested scopes */
    int                depth;                       /**< current depth (-1 = none, 0 = root, ...) */
} sem_scope_id_stack;

/**
 * @brief Semantic analysis working context for Pass 1.
 *
 * Fields:
 *  - funcs  : global function signature table (built-ins + user functions/getters/setters),
 *  - symtab : global symbol table containing *all* symbols with their scope IDs,
 *  - scopes : runtime scope stack used for visibility/redeclare checks,
 *  - ids    : textual scope-ID stack used for labeling scopes ("1", "1.1", ...),
 *  - loop_depth : nesting level of loops (for break/continue checks),
 *  - seen_main  : true if main() with 0 params was found.
 */
typedef struct semantic_ctx {
    symtable          *funcs;   /**< global function signature table */
    symtable          *symtab;  /**< global symbol table of all symbols (incl. nested scopes) */
    scope_stack        scopes;  /**< local scopes stack (blocks/functions) for lookup/redeclare */
    sem_scope_id_stack ids;     /**< stack of textual scope IDs ("1", "1.1", ...) */
    int                loop_depth; /**< nesting counter for loop checks */
    bool               seen_main;  /**< true if main() with 0 params was found */
} semantic;

/**
 * @brief Run the first semantic pass over the AST.
 *
 * On success, the context internal to this module will have:
 *  - populated global function table (built-ins + user functions),
 *  - populated global symbol table (all symbols with scope IDs),
 *  - verified semantic constraints for Pass 1.
 *
 * @param tree Program AST root.
 * @return 0 (SUCCESS) on success, otherwise an error code (3/4/5/6/10/99).
 */
int semantic_pass1(ast tree);

#endif /* SEMANTIC_H */
