/**
 * @authors
 *   Hana Liškařová (xliskah00)
 *
 * @file tests/test_symtable.c
 * @brief Symtable bulk-fill tests + full dump (variables, consts, functions, get/set).
 *
 * The test:
 *  - Bulk inserts thousands of identifiers (v_*, c_*),
 *  - Inserts overloaded functions by (name#arity),
 *  - Inserts getter/setter using prefixed keys (get:name / set:name),
 *  - Verifies lookups, duplicate inserts (size must not grow),
 *  - Prints the entire table content (occupied slots) + summary.
 *
 * Build:
 *   gcc -std=c17 -Wall -Wextra -O2 symtable.c tests/test_symtable.c -o test_symtab
 * Run:
 *   ./test_symtab
 *
 * BUT FIT
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../projekt/symtable.h"

/** @brief Human-readable data_type name. */
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

/** @brief Human-readable symbol_type name. */
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

/** @brief Build composite key for (name, arity) function overloading. */
static void make_fun_key(char *buf, size_t n, const char *name, int arity){
    snprintf(buf, n, "%s#%d", name, arity);
}

/** @brief Bulk insert of variables/consts; validate presence and set demo meta. */
static void fill_vars(symtable *t, const char *prefix, int count, symbol_type kind, data_type dt){
    char key[128];
    for (int i=0;i<count;i++){
        snprintf(key, sizeof key, "%s_%05d", prefix, i);
        st_insert(t, key, kind, /*defined=*/true);
        st_data *d = st_get(t, key);
        assert(d && "inserted key must be retrievable");
        d->data_type   = dt;
        d->symbol_type = kind;
        d->defined     = true;
        d->param_count = 0;
    }
}

/** @brief Bulk insert of functions with arities 0..arity_max. */
static void fill_funcs(symtable *t, const char *name, int arity_max){
    char key[128];
    for (int a=0;a<=arity_max;a++){
        make_fun_key(key, sizeof key, name, a);
        st_insert(t, key, ST_FUN, /*defined=*/true);
        st_data *d = st_get(t, key);
        assert(d);
        d->symbol_type = ST_FUN;
        d->data_type   = ST_VOID; // demo only
        d->param_count = a;
    }
}

/** @brief Declare getter (0 params) using prefixed key. */
static void declare_getter(symtable *t, const char *name){
    char key[256]; snprintf(key, sizeof key, "get:%s", name);
    st_insert(t, key, ST_FUN, true);
    st_data *d = st_get(t, key); assert(d);
    d->symbol_type = ST_FUN; d->param_count = 0;
}

/** @brief Declare setter (1 param) using prefixed key. */
static void declare_setter(symtable *t, const char *name){
    char key[256]; snprintf(key, sizeof key, "set:%s", name);
    st_insert(t, key, ST_FUN, true);
    st_data *d = st_get(t, key); assert(d);
    d->symbol_type = ST_FUN; d->param_count = 1;
}

/** @brief Print entire table (occupied slots) and type summary. */
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

        printf("[%05u] key=%-24s kind=%-5s defined=%d dtype=%-6s",
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

int main(void){
    symtable *T = st_init(); assert(T);

    // 1) Fill with variables and consts
    fill_vars(T, "v", 2000, ST_VAR,   ST_INT);
    fill_vars(T, "c",  500, ST_CONST, ST_STRING);

    // 2) Overloaded functions + get/set
    fill_funcs(T, "foo", 8);   // foo#0..foo#8
    fill_funcs(T, "bar", 5);   // bar#0..bar#5
    declare_getter(T, "value");
    declare_setter(T, "value");

    // 3) Duplicates must not increase size
    unsigned before = T->size;
    st_insert(T, "v_00010", ST_VAR, true);
    char k[64]; make_fun_key(k, sizeof k, "foo", 3);
    st_insert(T, k, ST_FUN, true);
    assert(T->size == before);

    // 4) Sanity lookups
    assert(st_get(T, "v_00000"));
    assert(st_get(T, "v_01999"));
    assert(st_get(T, "c_00499"));
    char k0[64]; make_fun_key(k0, sizeof k0, "foo", 0);
    char k8[64]; make_fun_key(k8, sizeof k8, "foo", 8);
    assert(st_get(T, k0) && st_get(T, k8));
    assert(st_get(T, "get:value") && st_get(T, "set:value"));

    // 5) Dump the whole table
    st_dump(T, "SYMTABLE (v_*, c_*, foo#, bar#, get/set)");

    st_free(T);
    puts("OK: test_symtable finished.");
    return 0;
}
