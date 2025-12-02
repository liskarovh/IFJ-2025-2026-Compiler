/**
 * @authors Hana Liškařová (xliskah00)
 *
 * @file scope_stack.h
 * @brief Scope stack for semantic analysis (IFJ25).
 *
 * Each scope frame stores a @c symtable* of local identifiers. Enables:
 *  - push/pop of frames on block/function entry/exit,
 *  - declaring locals with no redeclare in the same block,
 *  - lookups with correct shadowing
 * BUT FIT
 */

#ifndef SCOPE_STACK_H
#define SCOPE_STACK_H

#include <stdbool.h>
#include <stddef.h>
#include "stack.h"
#include "symtable.h"

/**
 * @brief Scope stack - each node in @c frames holds a @c symtable* for one block.
 */
typedef struct {
    stack frames;
} scope_stack;

/**
 * @brief Initialize an empty scope stack.
 * @param s Pointer to a @c scope_stack.
 */
void scopes_init(scope_stack *s);

/**
 * @brief Push a new empty frame (symtable) for a block/function body.
 * @param s Pointer to a @c scope_stack.
 */
void scopes_push(scope_stack *s);

/**
 * @brief Pop the top frame and free its @c symtable (destroying all locals).
 * @param s Pointer to a @c scope_stack.
 * @return @c true if a frame was popped, @c false if the stack was empty.
 */
bool scopes_pop(scope_stack *s);

/**
 * @brief Get the top frame’s symtable without removing it.
 * @param s Pointer to a @c scope_stack.
 * @return Pointer to the top @c symtable, or @c NULL if empty.
 */
symtable *scopes_top(scope_stack *s);

/**
 * @brief Declare a local identifier in the current frame.
 *
 * @param s Pointer to a @c scope_stack.
 * @param name Identifier to declare (C string).
 * @param defined Whether the symbol is considered defined.
 * @return @c true on success, @c false on collision or if no frame is present.
 */
bool scopes_declare_local(scope_stack *s, const char *name, bool defined);

/**
 * @brief Lookup a name only in the current frame.
 * @param s Pointer to a @c scope_stack.
 * @param name Identifier to search for.
 * @return Pointer to @c st_data if found, otherwise @c NULL.
 */
st_data *scopes_lookup_in_current(scope_stack *s, const char *name);

/**
 * @brief Lookup a name from the innermost scope outward.
 * @param s Pointer to a @c scope_stack.
 * @param name Identifier to search for.
 * @return Pointer to @c st_data if found in any frame, otherwise @c NULL.
 */
st_data *scopes_lookup(scope_stack *s, const char *name);

/**
 * @brief Dispose the entire scope stack by popping and freeing all frames.
 * @param s Pointer to a @c scope_stack.
 */
void scopes_dispose(scope_stack *s);


/**
 * @brief Dump the whole scope stack from top to bottom.
 * @param scopes Scope stack pointer.
 * @param out    Output stream.
 */
void scopes_dump(const scope_stack *scopes, FILE *out);

#endif // SCOPE_STACK_H
