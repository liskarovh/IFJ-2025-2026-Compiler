/**
 * @file builtins.c
 * @brief Implementation of IFJ25 built-in function registration (Pass 1 arity).
 *
 * Inserts signatures for Ifj.* functions into the provided symtable using
 * keys of the form "<qname>#<arity>". The entries carry ST_FUN and param_count.
 *
 * This module is intentionally lightweight for Pass 1. If you later need
 * richer metadata (types, categories), you can extend this table without
 * touching the semantic pass.
 *
 * @authors
 *   Hana Liškařová (xliskah00)
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "builtins.h"

/** @brief Format "<qname>#<arity>" into @p buf. */
static void make_sig_key(char *buf, size_t n, const char *qname, unsigned arity) {
    snprintf(buf, n, "%s#%u", qname ? qname : "(null)", arity);
}

/** @brief One built-in row. */
typedef struct {
    const char *qname;
    unsigned    arity;
    bool        needs_boolthen;   /**< Register only if cfg.ext_boolthen. */
    bool        needs_statican;   /**< Register only if cfg.ext_statican. */
} builtin_row;

/* Canonical IFJ25 built-in list (names per your spec). */
static const builtin_row g_rows[] = {
    /* I/O */
    { "Ifj.read_str",  0, false, false },
    { "Ifj.read_num",  0, false, false },
    { "Ifj.write",     1, false, false },

    /* Conversions / numeric helpers */
    { "Ifj.floor",     1, false, false },
    { "Ifj.str",       1, false, false },

    /* Strings */
    { "Ifj.length",    1, false, false },
    { "Ifj.substring", 3, false, false },
    { "Ifj.strcmp",    2, false, false },
    { "Ifj.ord",       2, false, false },
    { "Ifj.chr",       1, false, false },

    /* Extensions */
    { "Ifj.read_bool", 0, true,  false },  /* BOOLTHEN */
    { "Ifj.is_int",    1, false, true  }   /* STATICAN (optional helper) */
};

bool builtins_install(symtable *gtab, builtins_config cfg) {
    if (!gtab) return false;

    for (size_t i = 0; i < sizeof(g_rows)/sizeof(g_rows[0]); ++i) {
        const builtin_row *r = &g_rows[i];
        if (r->needs_boolthen && !cfg.ext_boolthen) continue;
        if (r->needs_statican && !cfg.ext_statican) continue;

        char key[256]; make_sig_key(key, sizeof key, r->qname, r->arity);

        /* Idempotent: skip if already present. */
        if (st_find(gtab, key)) continue;

        /* Insert signature "<qname>#<arity>" as ST_FUN with param_count set. */
        st_insert(gtab, key, ST_FUN, true);
        st_data *d = st_get(gtab, key);
        if (!d) return false;

        d->symbol_type = ST_FUN;
        d->param_count = (int)r->arity;
        /* Keep other st_data fields as defaults for Pass 1. */
    }
    return true;
}
