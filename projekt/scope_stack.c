/**
 * @authors
 *   Hana Liškařová (xliskah00)
 *
 * @file scope_stack.c
 * @brief Implementation of a scope stack for semantic analysis (IFJ25).
 *
 * Each scope frame stores a @c symtable* of local identifiers. The API enables:
 *  - push/pop of frames on block/function entry/exit,
 *  - declaring locals with “no redeclare in the same block” rule,
 *  - lookups with correct shadowing (inner frames take precedence),
 *  - deterministic cleanup of all locals upon frame pop.
 *
 * BUT FIT
 */

#include "scope_stack.h"
#include <assert.h>

void scopes_init(scope_stack* s){
    assert(s);
    stack_init(&s->frames);
}

bool scopes_is_empty(const scope_stack* s){
    assert(s);
    return stack_is_empty((stack*)&s->frames);
}

size_t scopes_depth(const scope_stack* s){
    assert(s);
    size_t n = 0;
    for (stack_item* it = s->frames.top; it; it = it->next) ++n;
    return n;
}

void scopes_push(scope_stack* s){
    assert(s);
    symtable* t = st_init();
    if (!t) return;                 // OOM: fail silently; caller may detect via top==NULL
    stack_push(&s->frames, (void*)t);
}

bool scopes_pop(scope_stack* s){
    assert(s);
    symtable* t = (symtable*)stack_pop(&s->frames);
    if (!t) return false;
    st_free(t);
    return true;
}

symtable* scopes_top(scope_stack* s){
    assert(s);
    return (symtable*)stack_top(&s->frames);
}

bool scopes_declare_local(scope_stack* s, const char* name, bool defined){
    assert(s && name);
    symtable* top = scopes_top(s);
    if (!top) return false;                 // no frame pushed → programmer error in caller

    // enforce “no redeclare in the same block”
    if (st_find(top, (char*)name)) return false;

    st_insert(top, (char*)name, ST_VAR, defined);
    // verify (in case st_insert failed internally)
    return st_get(top, (char*)name) != NULL;
}

st_data* scopes_lookup_in_current(scope_stack* s, const char* name){
    assert(s && name);
    symtable* top = scopes_top(s);
    if (!top) return NULL;
    return st_get(top, (char*)name);
}

st_data* scopes_lookup(scope_stack* s, const char* name){
    assert(s && name);
    for (stack_item* it = s->frames.top; it; it = it->next){
        symtable* t = (symtable*)it->data;
        if (!t) continue;
        st_data* d = st_get(t, (char*)name);
        if (d) return d;
    }
    return NULL;
}

void scopes_dispose(scope_stack* s){
    assert(s);
    while (scopes_pop(s)) { /* pop until empty */ }
}


void scopes_dump(const scope_stack *scopes, FILE *out) {
    if (!scopes || !out) return;

    fprintf(out, "== SCOPE STACK DUMP (top → bottom) ==\n");

    int idx = 0;
    for (const stack_item *it = scopes->frames.top; it; it = it->next, ++idx) {
        symtable *frame = (symtable *)it->data;
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