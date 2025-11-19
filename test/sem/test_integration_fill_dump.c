/**
 * @authors
 *   Hana Liškařová (xliskah00)
 *
 * @file tests/test_integration_fill_dump.c
 * @brief Integration test that uses stack + scope_stack + symtable to fill and dump one symtable.
 *
 * The test:
 *  - Uses a generic @c stack to build lists of names to declare,
 *  - Uses @c scope_stack to create outer/inner scopes and declare locals (shadowing),
 *  - Uses a global @c symtable ("ALL") to collect:
 *      • overloaded functions (name#arity),
 *      • getters/setters (get:name / set:name),
 *      • snapshot of all locals from every scope frame (prefixed as scopeN:name),
 *  - Prints the FULL content of the "ALL" table (occupied slots + summary).
 *
 * Build:
 *   gcc -std=c17 -Wall -Wextra -O2 stack.c symtable.c scope_stack.c tests/test_integration_fill_dump.c -o test_integration
 * Run:
 *   ./test_integration
 *
 * BUT FIT
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../projekt/stack.h"
#include "../../projekt/symtable.h"
#include "../../projekt/scope_stack.h"

/* ───────────────────────── helpers: enum → text (pretty dump) ───────────────────────── */

static const char* dtype_str(data_type t){
    switch(t){
        case ST_NULL:   return "NULL";
        case ST_INT:    return "INT";
        case ST_DOUBLE: return "DOUBLE";
        case ST_STRING: return "STRING";
        case ST_BOOL:   return "BOOL";
        case ST_VOID:   return "VOID";
        case ST_U8:     return "U8";
        default:        return "?";
    }
}
static const char* stype_str(symbol_type t){
    switch(t){
        case ST_VAR:  return "VAR";
        case ST_CONST:return "CONST";
        case ST_FUN:  return "FUN";
        case ST_PAR:  return "PAR";
        case ST_GLOB: return "GLOB";
        default:      return "?";
    }
}

/** @brief Build composite key for a function variant by (name, arity). */
static void make_fun_key(char *buf, size_t n, const char *name, int arity){
    snprintf(buf, n, "%s#%d", name, arity);
}

/** @brief Dump the full content of a symtable. */
static void st_dump(symtable *t, const char *title){
    unsigned vars=0, consts=0, funs=0, pars=0, globs=0, unknown=0;

    printf("========== %s ==========\n", title ? title : "SYMTABLE DUMP");
    printf("Capacity=%u, occupied=%u\n", (unsigned)SYMTABLE_SIZE, t->size);
    printf("------------------------------------------------------------\n");
    for (unsigned i=0; i<SYMTABLE_SIZE; ++i){
        st_symbol *s = &t->table[i];
        if (!s->occupied) continue;
        st_data *d = s->data;
        const char* ksym = d ? stype_str(d->symbol_type) : "?";
        const char* kdt  = d ? dtype_str(d->data_type)   : "?";
        int def = d ? d->defined : 0;
        int pc  = (d && d->symbol_type==ST_FUN) ? d->param_count : -1;

        printf("[%05u] key=%-28s kind=%-5s defined=%d dtype=%-6s",
               i, s->key ? s->key : "(null)", ksym, def, kdt);
        if (pc >= 0) printf(" params=%d", pc);
        putchar('\n');

        if (!d) continue;
        switch(d->symbol_type){
            case ST_VAR:  ++vars; break;
            case ST_CONST:++consts; break;
            case ST_FUN:  ++funs; break;
            case ST_PAR:  ++pars; break;
            case ST_GLOB: ++globs; break;
            default: ++unknown; break;
        }
    }
    printf("------------------------------------------------------------\n");
    printf("Summary: VAR=%u CONST=%u FUN=%u PAR=%u GLOB=%u UNKNOWN=%u\n",
           vars, consts, funs, pars, globs, unknown);
    printf("============================================================\n");
}

/* ───────────────────────── helpers: use plain stack to prepare names ───────────────────────── */

/**
 * @brief Push N freshly-allocated C-strings "<prefix>_<NN>" onto a generic stack.
 *        Caller must later pop and free each string.
 */
static void name_stack_fill(stack *names, const char *prefix, int count){
    for (int i=0;i<count;i++){
        char buf[128];
        snprintf(buf, sizeof buf, "%s_%02d", prefix, i);
        size_t n = strlen(buf)+1;
        char *s = (char*)malloc(n);
        assert(s);
        memcpy(s, buf, n);
        stack_push(names, s);
    }
}

/**
 * @brief Pop all strings from a generic stack and declare them as locals in CURRENT scope.
 *        Frees each popped string after insert (symtable duplicates key internally).
 */
static void name_stack_declare_all(stack *names, scope_stack *S){
    while (!stack_is_empty(names)){
        char *nm = (char*)stack_pop(names);
        assert(nm);
        bool ok = scopes_declare_local(S, nm, /*defined=*/true);
        assert(ok && "redeclare in the same block should not happen in this generator");
        free(nm);
    }
}

/* ───────────────────────── helpers: collect locals from scopes into one symtable ───────────────────────── */

/**
 * @brief Copy all identifiers from every scope frame into a single @c symtable,
 *        prefixing keys as "scope<depth>:<name>" (top frame has depth 0).
 */
static void collect_all_locals_into(symtable *ALL, scope_stack *S){
    // Determine total depth to compute numeric labels top=0, next=1, ...
    int depth = 0;
    for (stack_item* it = S->frames.top; it; it = it->next) depth++;

    int idx = 0;
    for (stack_item* it = S->frames.top; it; it = it->next, idx++){
        symtable *frame = (symtable*)it->data;
        int label = idx; // 0 = top, 1 = next, ...
        for (unsigned i=0; i<SYMTABLE_SIZE; ++i){
            st_symbol *s = &frame->table[i];
            if (!s->occupied || !s->key) continue;
            char full[256];
            snprintf(full, sizeof full, "scope%d:%s", label, s->key);
            st_insert(ALL, full, ST_VAR, /*defined=*/true);
            st_data *d = st_get(ALL, full);
            assert(d);
            d->symbol_type = ST_VAR;
            d->data_type   = ST_INT; // demo type
        }
    }
}

/* ───────────────────────── main test ───────────────────────── */

int main(void){
    /* 0) One global symtable we will DUMP in the end. */
    symtable *ALL = st_init(); assert(ALL);

    /* 1) Fill global symtable with functions + get/set. */
    // functions: foo#0..5, bar#1..3
    for (int a=0;a<=5;a++){
        char k[128]; make_fun_key(k, sizeof k, "foo", a);
        st_insert(ALL, k, ST_FUN, true);
        st_data *d = st_get(ALL, k); assert(d);
        d->param_count = a; d->data_type = ST_VOID;
    }
    for (int a=1;a<=3;a++){
        char k[128]; make_fun_key(k, sizeof k, "bar", a);
        st_insert(ALL, k, ST_FUN, true);
        st_data *d = st_get(ALL, k); assert(d);
        d->param_count = a; d->data_type = ST_VOID;
    }
    // getter/setter: value
    st_insert(ALL, "get:value", ST_FUN, true);
    st_insert(ALL, "set:value", ST_FUN, true);
    st_data *g = st_get(ALL, "get:value"); if (g){ g->param_count=0; }
    st_data *s = st_get(ALL, "set:value"); if (s){ s->param_count=1; }

    /* 2) Build scopes and declare locals using a plain stack of strings. */
    scope_stack S; scopes_init(&S);

    // outer block
    scopes_push(&S);
    stack outer_names; stack_init(&outer_names);
    name_stack_fill(&outer_names, "outer", 10);      // outer_00..outer_09
    name_stack_declare_all(&outer_names, &S);        // declares into current frame

    // inner block (shadow outer_05)
    scopes_push(&S);
    stack inner_names; stack_init(&inner_names);
    name_stack_fill(&inner_names, "inner", 6);       // inner_00..inner_05
    name_stack_declare_all(&inner_names, &S);
    // explicit shadow: outer_05
    assert(scopes_declare_local(&S, "outer_05", true));

    /* 3) Collect all locals from scope frames into ALL (prefixed keys). */
    collect_all_locals_into(ALL, &S);

    /* 4) Print the global table with EVERYTHING we collected. */
    st_dump(ALL, "INTEGRATED SYMTABLE (funcs, get/set, locals from scopes)");

    /* 5) Cleanup. */
    scopes_dispose(&S);
    st_free(ALL);

    puts("OK: test_integration finished.");
    return 0;
}
