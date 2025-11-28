/**
 * @authors
 *   Hana Liškařová (xliskah00)
 *
 * @file scope_stack.h
 * @brief Scope stack for semantic analysis (IFJ25).
 *
 * A thin LIFO wrapper around generic @c stack that stores @c symtable* frames.
 * Each lexical block/function body pushes a new frame with local identifiers, and
 * pops (and frees) it on exit. This enables correct shadowing, and “no redeclare
 * in the same block” checks in O(1) per declaration.
 *
 * BUT FIT
 */

#ifndef SCOPE_STACK_H
#define SCOPE_STACK_H

#include <stdbool.h>
#include <stddef.h>
#include "stack.h"
#include "symtable.h"

/**
 * @brief Scope stack; each node in @c frames holds a @c symtable* for one block.
 */
typedef struct {
    stack frames;  /**< LIFO list of scope frames; each data is a @c symtable* */
} scope_stack;

/**
 * @brief Initialize an empty scope stack.
 * @param s Pointer to a @c scope_stack.
 */
void scopes_init(scope_stack* s);

/**
 * @brief Check whether the scope stack is empty.
 * @param s Pointer to a @c scope_stack.
 * @return @c true if no frames are present, @c false otherwise.
 */
bool scopes_is_empty(const scope_stack* s);

/**
 * @brief Get current depth (number of frames).
 * @note O(n), intended for debugging/tests.
 * @param s Pointer to a @c scope_stack.
 * @return Number of frames in the stack.
 */
size_t scopes_depth(const scope_stack* s);

/**
 * @brief Push a new empty frame (symtable) for a lexical block/function body.
 * @param s Pointer to a @c scope_stack.
 *
 * Allocates a fresh @c symtable; ownership is transferred to the stack and the
 * frame will be freed by ::scopes_pop or ::scopes_dispose.
 */
void scopes_push(scope_stack* s);

/**
 * @brief Pop the top frame and free its @c symtable (destroying all locals).
 * @param s Pointer to a @c scope_stack.
 * @return @c true if a frame was popped, @c false if the stack was empty.
 */
bool scopes_pop(scope_stack* s);

/**
 * @brief Get the top frame’s symtable without removing it.
 * @param s Pointer to a @c scope_stack.
 * @return Pointer to the top @c symtable, or @c NULL if empty.
 */
symtable* scopes_top(scope_stack* s);

/**
 * @brief Declare a local identifier in the current frame.
 *
 * Enforces “no redeclare in the same block”: if the name already exists
 * in the top frame, the function returns @c false.
 *
 * @param s Pointer to a @c scope_stack.
 * @param name Identifier to declare (C string).
 * @param defined Whether the symbol is considered defined (e.g., after @c var).
 * @return @c true on success, @c false on collision or if no frame is present.
 */
bool scopes_declare_local(scope_stack* s, const char* name, bool defined);

/**
 * @brief Lookup a name only in the current frame.
 * @param s Pointer to a @c scope_stack.
 * @param name Identifier to search for.
 * @return Pointer to @c st_data if found, otherwise @c NULL.
 */
st_data* scopes_lookup_in_current(scope_stack* s, const char* name);

/**
 * @brief Lookup a name from the innermost scope outward (supports shadowing).
 * @param s Pointer to a @c scope_stack.
 * @param name Identifier to search for.
 * @return Pointer to @c st_data if found in any frame, otherwise @c NULL.
 */
st_data* scopes_lookup(scope_stack* s, const char* name);

/**
 * @brief Dispose the entire scope stack by popping and freeing all frames.
 * @param s Pointer to a @c scope_stack.
 *
 * After this call the stack is empty. You can reuse it by calling ::scopes_push().
 */
void scopes_dispose(scope_stack* s);


/**
 * @brief Dump the whole scope stack from top to bottom.
 *        For each frame, prints a header and uses st_dump(...) on its symtable.
 * @param scopes Scope stack pointer.
 * @param out    Output stream (e.g., stdout).
 */
void scopes_dump(const scope_stack *scopes, FILE *out);

#endif // SCOPE_STACK_H