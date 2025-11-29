/**
 * @file builtins.c
 * @brief Implementation of IFJ25 built-in function registration and metadata.
 *
 * Inserts signatures for Ifj.* functions into the provided symtable using
 * keys of the form "<qname>#<arity>" (for example "Ifj.write#1").
 * The entries carry ST_FUN and param_count.
 *
 * Additionally, a small static table keeps the expected parameter kinds for
 * each built-in, which can be queried via builtins_get_param_spec().
 *
 * This module is intentionally lightweight for Pass 1. If you later need
 * richer metadata (return types, categories, etc.), you can extend the table
 * without touching the semantic pass interface.
 *
 * @authors
 *   Hana Liškařová (xliskah00)
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "builtins.h"

/**
 * @brief Format "<qname>#<arity>" into @p buf.
 *
 * @param buf   Destination buffer.
 * @param n     Size of @p buf in bytes.
 * @param qname Fully-qualified name (for example "Ifj.write").
 * @param arity Number of parameters.
 */
static void make_sig_key(char *buf, size_t n, const char *qname, unsigned arity) {
    snprintf(buf, n, "%s#%u", qname ? qname : "(null)", arity);
}

/**
 * @brief One built-in specification row.
 *
 * qname        – fully-qualified name ("Ifj.read_str"),
 * arity        – number of parameters,
 * param_kinds  – expected parameter kinds for indices 0..arity-1,
 * needs_boolthen / needs_statican – feature flags.
 */
typedef struct {
    const char        *qname;
    unsigned           arity;
    builtin_param_kind param_kinds[3];
    data_type          return_type;
    bool               needs_boolthen; /**< Register only if cfg.ext_boolthen. */
    bool               needs_statican; /**< Register only if cfg.ext_statican. */
} builtin_row;

/* Canonical IFJ25 built-in list (names, arity and parameter kinds). */
static const builtin_row g_rows[] = {
    /* I/O */
    { "Ifj.read_str",  0,
      { BUILTIN_PARAM_ANY,     BUILTIN_PARAM_ANY,     BUILTIN_PARAM_ANY },
      ST_STRING,   // návratový typ – String nebo Null, staticky bereme String
      false, false },

    { "Ifj.read_num",  0,
      { BUILTIN_PARAM_ANY,     BUILTIN_PARAM_ANY,     BUILTIN_PARAM_ANY },
      ST_DOUBLE,   // Num
      false, false },

    { "Ifj.write",     1,
      { BUILTIN_PARAM_ANY,     BUILTIN_PARAM_ANY,     BUILTIN_PARAM_ANY },
      ST_NULL,     // "print" – vrací null
      false, false },

    /* Conversions / numeric helpers */
    { "Ifj.floor",     1,
      { BUILTIN_PARAM_NUMBER,  BUILTIN_PARAM_ANY,     BUILTIN_PARAM_ANY },
      ST_DOUBLE,   // bereme jako "Num" – číselný typ
      false, false },

    { "Ifj.str",       1,
      { BUILTIN_PARAM_ANY,     BUILTIN_PARAM_ANY,     BUILTIN_PARAM_ANY },
      ST_STRING,
      false, false },

    /* Strings */
    { "Ifj.length",    1,
      { BUILTIN_PARAM_STRING,  BUILTIN_PARAM_ANY,     BUILTIN_PARAM_ANY },
      ST_DOUBLE,   // délka = číslo
      false, false },

    { "Ifj.substring", 3,
      { BUILTIN_PARAM_STRING,  BUILTIN_PARAM_NUMBER,  BUILTIN_PARAM_NUMBER },
      ST_STRING,
      false, false },

    { "Ifj.strcmp",    2,
      { BUILTIN_PARAM_STRING,  BUILTIN_PARAM_STRING,  BUILTIN_PARAM_ANY },
      ST_DOUBLE,   // porovnání – obvykle číslo (<0,0,>0)
      false, false },

    { "Ifj.ord",       2,
      { BUILTIN_PARAM_STRING,  BUILTIN_PARAM_NUMBER,  BUILTIN_PARAM_ANY },
      ST_DOUBLE,   // kód znaku – číslo
      false, false },

    { "Ifj.chr",       1,
      { BUILTIN_PARAM_NUMBER,  BUILTIN_PARAM_ANY,     BUILTIN_PARAM_ANY },
      ST_STRING,
      false, false },

    /* Extensions */

    { "Ifj.read_bool", 0,
      { BUILTIN_PARAM_ANY,     BUILTIN_PARAM_ANY,     BUILTIN_PARAM_ANY },
      ST_BOOL,     // Bool nebo Null, staticky bereme Bool
      false,  false },  /* BOOLTHEN */

    { "Ifj.is_int",    1,
      { BUILTIN_PARAM_ANY,     BUILTIN_PARAM_ANY,     BUILTIN_PARAM_ANY },
      ST_BOOL,
      false, true  }   /* STATICAN (optional helper) */
};


bool builtins_install(symtable *gtab, builtins_config cfg) {
    if (!gtab) {
        return false;
    }

    for (size_t i = 0; i < sizeof(g_rows) / sizeof(g_rows[0]); ++i) {
        const builtin_row *r = &g_rows[i];

        if (r->needs_boolthen && !cfg.ext_boolthen) {
            continue;
        }
        if (r->needs_statican && !cfg.ext_statican) {
            continue;
        }

        char key[256];
        make_sig_key(key, sizeof key, r->qname, r->arity);

        /* Idempotent: skip if already present. */
        if (st_find(gtab, key)) {
            continue;
        }

        /* Insert signature "<qname>#<arity>" as ST_FUN with param_count set. */
        st_insert(gtab, key, ST_FUN, true);
        st_data *d = st_get(gtab, key);
        if (!d) {
            return false;
        }

        d->symbol_type = ST_FUN;
        d->param_count = (int)r->arity;
        d->data_type   = r->return_type;
    }

    return true;
}

unsigned builtins_get_param_spec(const char *qname,
                                 builtin_param_kind *out_kinds,
                                 unsigned max_kinds) {
    if (!qname) {
        return 0;
    }

    for (size_t i = 0; i < sizeof(g_rows) / sizeof(g_rows[0]); ++i) {
        const builtin_row *r = &g_rows[i];

        if (strcmp(r->qname, qname) == 0) {
            /* Optionally copy parameter kinds to caller. */
            if (out_kinds && max_kinds > 0) {
                unsigned to_copy = (r->arity < max_kinds) ? r->arity : max_kinds;
                for (unsigned j = 0; j < to_copy; ++j) {
                    out_kinds[j] = r->param_kinds[j];
                }
            }
            return r->arity;
        }
    }

    /* Not a known built-in. */
    return 0;
}

bool builtins_is_builtin_qname(const char *name) {
    /* Implementation moved from the header as requested. */
    return name &&
           name[0] == 'I' &&
           name[1] == 'f' &&
           name[2] == 'j' &&
           name[3] == '.';
}
