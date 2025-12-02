/**
 * @authors
 *   Hana Liškařová (xliskah00)
 *
 * @file scope_stack.c
 * @brief Implementation of a scope stack for semantic analysis (IFJ25).
 *
 * Each scope stores symtable* of local identifiers. Enables:
 *  - push/pop frames on block/function entry/exit,
 *  - declaring locals with no redeclare in the same block,
 *  - lookups with correct shadowing
 *
 * BUT FIT
 */

#include "scope_stack.h"
#include <assert.h>

/**
 * @brief Initialize an empty scope stack.
 * @param s Pointer to a @c scope_stack.
 */
void scopes_init(scope_stack *s) {
    assert(s);
    stack_init(&s->frames);
}

/**
 * @brief Push a new empty frame (symtable) for a block/function body.
 * @param s Pointer to a @c scope_stack.
 */
void scopes_push(scope_stack *s) {
    assert(s);
    symtable *t = st_init();
    if (!t)
        return;
    stack_push(&s->frames, t);
}

/**
 * @brief Pop the top frame and free its @c symtable (destroying all locals).
 * @param s Pointer to a @c scope_stack.
 * @return @c true if a frame was popped, @c false if the stack was empty.
 */
bool scopes_pop(scope_stack *s) {
    assert(s);
    symtable *t = stack_pop(&s->frames);
    if (!t)
        return false;
    st_free(t);
    return true;
}

/**
 * @brief Get the top frame’s symtable without removing it.
 * @param s Pointer to a @c scope_stack.
 * @return Pointer to the top @c symtable, or @c NULL if empty.
 */
symtable *scopes_top(scope_stack *s) {
    assert(s);
    return stack_top(&s->frames);
}

/**
 * @brief Declare a local identifier in the current frame.
 *
 * Enforces “no redeclare in the same block”.
 *
 * @param s Pointer to a @c scope_stack.
 * @param name Identifier name (string).
 * @param defined True if the identifier is defined, false otherwise.
 * @return @c true if declaration succeeded, @c false on redeclare or error.
 */
bool scopes_declare_local(scope_stack *s, const char *name, bool defined) {
    assert(s && name);
    symtable *top = scopes_top(s);
    if (!top)
        return false;

    if (st_find(top, (char *) name))
        return false;

    st_insert(top, (char *) name, ST_VAR, defined);

    return st_get(top, (char *) name) != NULL;
}

/**
 * @brief Lookup an identifier in the current frame only.
 * @param s Pointer to a @c scope_stack.
 * @param name Identifier name (string).
 * @return Pointer to symbol data, or @c NULL if not found.
 */
st_data *scopes_lookup_in_current(scope_stack *s, const char *name) {
    assert(s && name);
    symtable *top = scopes_top(s);
    if (!top)
        return NULL;
    return st_get(top, (char *) name);
}

/**
 * @brief Lookup an identifier in all frames (with shadowing).
 * @param s Pointer to a @c scope_stack.
 * @param name Identifier name (string).
 * @return Pointer to symbol data, or @c NULL if not found.
 */
st_data *scopes_lookup(scope_stack *s, const char *name) {
    assert(s && name);
    for (stack_item *it = s->frames.top; it; it = it->next) {
        symtable *t = it->data;
        if (!t)
            continue;
        st_data *d = st_get(t, (char *) name);
        if (d)
            return d;
    }
    return NULL;
}

/**
 * @brief Dispose the entire scope stack, freeing all frames and their symtables.
 * @param s Pointer to a @c scope_stack.
 */
void scopes_dispose(scope_stack *s) {
    assert(s);
    while (scopes_pop(s)) {
    }
}

/**
 * @brief Dump the entire scope stack to the given output stream.
 * @param scopes Pointer to a @c scope_stack.
 * @param out Output stream (e.g., @c stdout).
 */
void scopes_dump(const scope_stack *scopes, FILE *out) {
    if (!scopes || !out)
        return;

    fprintf(out, "== SCOPE STACK DUMP (top → bottom) ==\n");

    int idx = 0;
    for (const stack_item *it = scopes->frames.top; it; it = it->next, ++idx) {
        symtable *frame = it->data;
        fprintf(out, "-- frame #%d --\n", idx);
        if (frame) {
            st_dump(frame, out);
        } else {
            fprintf(out, "(null frame)\n");
        }
    }

    if (idx == 0) {
        fprintf(out, "(empty)\n");
    }
}
