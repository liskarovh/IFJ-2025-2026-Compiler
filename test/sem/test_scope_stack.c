/**
 * @authors
 *   Hana Liškařová (xliskah00)
 *
 * @file tests/test_scope_stack.c
 * @brief Scope-stack tests with frame dumps (shadowing, redeclare-in-block, cleanup).
 *
 * The test:
 *  - Creates outer and inner frames (blocks),
 *  - Declares locals in both; inner shadows one name from outer,
 *  - Verifies lookups (current vs any),
 *  - Dumps each frame top→bottom,
 *  - Pops inner (locals disappear), then disposes all.
 *
 * Build (from ./projekt):
 *   gcc -std=c99 -Wall -Wextra -Werror -pedantic -g stack.c symtable.c scope_stack.c ../test/sem/test_scope_stack.c -o build/test_scopes
 * Run:
 *   ./build/test_scopes
 *
 * BUT FIT
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../projekt/scope_stack.h"

/** @brief Human-readable symbol_type. */
static const char* stype_str(symbol_type t){
    switch(t){
        case ST_VAR:   return "VAR";
        case ST_CONST: return "CONST";
        case ST_FUN:   return "FUN";
        case ST_PAR:   return "PAR";
        case ST_GLOB:  return "GLOB";
        default:       return "?";
    }
}

/** @brief Print a single frame (occupied keys only). */
static void dump_frame(symtable *t, const char *label){
    printf("---- FRAME %s (occupied=%u) ----\n", label ? label : "", t->size);
    for (unsigned i = 0; i < SYMTABLE_SIZE; i++){
        st_symbol *s = &t->table[i];
        if (!s->occupied) continue;
        st_data *d = s->data;
        printf("  %-24s kind=%s\n",
               s->key ? s->key : "(null)",
               d ? stype_str(d->symbol_type) : "N/A");
    }
}

/** @brief Expect presence only in current frame. */
static void expect_current(scope_stack* S, const char* name, bool present){
    st_data* d = scopes_lookup_in_current(S, name);
    if (present) { assert(d && "expected present in current scope"); }
    else         { assert(!d && "expected absent in current scope"); }
}

/** @brief Expect presence in any frame (top→bottom). */
static void expect_any(scope_stack* S, const char* name, bool present){
    st_data* d = scopes_lookup(S, name);
    if (present) { assert(d && "expected present in any scope"); }
    else         { assert(!d && "expected absent in any scope"); }
}

int main(void){
    scope_stack S; scopes_init(&S);

    // outer frame
    scopes_push(&S);
    for (int i = 0; i < 10; i++){
        char n[32]; snprintf(n, sizeof n, "outer_%02d", i);
        assert(scopes_declare_local(&S, n, true));
    }
    // redeclare in same block must fail
    assert(!scopes_declare_local(&S, "outer_05", true));

    // inner frame (shadow outer_05) + a few inner_* locals
    scopes_push(&S);
    assert(scopes_declare_local(&S, "outer_05", true));  // shadow
    for (int i = 0; i < 5; i++){
        char n[32]; snprintf(n, sizeof n, "inner_%02d", i);
        assert(scopes_declare_local(&S, n, true));
    }

    // lookups
    expect_current(&S, "outer_05", true);   // inner shadow exists in current
    expect_current(&S, "outer_04", false);  // not in current
    expect_any(&S, "outer_04", true);       // but visible in outer

    // dump scopes top→bottom
    printf("== SCOPE STACK DUMP (top → bottom) ==\n");
    int idx = 0;
    for (stack_item* it = S.frames.top; it; it = it->next){
        char lab[32]; snprintf(lab, sizeof lab, "#%d", idx++);
        dump_frame((symtable*)it->data, lab);
    }

    // leave inner → inner_* gone, shadow removed
    assert(scopes_pop(&S) == true);
    expect_any(&S, "inner_00", false);
    expect_any(&S, "outer_05", true);       // outer one survives

    // final cleanup
    scopes_dispose(&S);
    puts("OK: test_scope_stack finished.");
    return 0;
}
