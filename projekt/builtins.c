/**
* @file builtins.h
 * @brief Registration to the symtable for IFJ25 built-in functions.
 *
 * @authors Hana Liškařová (xliskah00)
 * @note  Project: IFJ / BUT FIT
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "builtins.h"

/**
 * @brief Create a signature key "<qname>#<arity>".
 *
 * @param buf   Output buffer to write the key into.
 * @param n     Size of the output buffer.
 * @param qname Fully-qualified name of the function.
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
 * return_type  – data_type of the return value,
 * needs_boolthen / needs_statican – feature flags.
 */
typedef struct {
    const char *qname;
    unsigned arity;
    builtin_param_kind param_kinds[3];
    data_type return_type;
    bool needs_boolthen;
    bool needs_statican;
} builtin_row;

/* Table of all built-in functions. */
static const builtin_row g_rows[] = {
    /* I/O */
    {
        "Ifj.read_str", 0,
        {BUILTIN_PARAM_ANY, BUILTIN_PARAM_ANY, BUILTIN_PARAM_ANY},
        ST_STRING, // Str
        false, false
    },

    {
        "Ifj.read_num", 0,
        {BUILTIN_PARAM_ANY, BUILTIN_PARAM_ANY, BUILTIN_PARAM_ANY},
        ST_DOUBLE, // Num
        false, false
    },

    {
        "Ifj.write", 1,
        {BUILTIN_PARAM_ANY, BUILTIN_PARAM_ANY, BUILTIN_PARAM_ANY},
        ST_NULL, // Void
        false, false
    },

    /* Conversions / numeric helpers */
    {
        "Ifj.floor", 1,
        {BUILTIN_PARAM_NUMBER, BUILTIN_PARAM_ANY, BUILTIN_PARAM_ANY},
        ST_DOUBLE, // Num
        false, false
    },

    {
        "Ifj.str", 1,
        {BUILTIN_PARAM_ANY, BUILTIN_PARAM_ANY, BUILTIN_PARAM_ANY},
        ST_STRING,
        false, false
    },

    /* Strings */
    {
        "Ifj.length", 1,
        {BUILTIN_PARAM_STRING, BUILTIN_PARAM_ANY, BUILTIN_PARAM_ANY},
        ST_DOUBLE, // Num
        false, false
    },

    {
        "Ifj.substring", 3,
        {BUILTIN_PARAM_STRING, BUILTIN_PARAM_NUMBER, BUILTIN_PARAM_NUMBER},
        ST_STRING, // Str
        false, false
    },

    {
        "Ifj.strcmp", 2,
        {BUILTIN_PARAM_STRING, BUILTIN_PARAM_STRING, BUILTIN_PARAM_ANY},
        ST_DOUBLE, // Num
        false, false
    },

    {
        "Ifj.ord", 2,
        {BUILTIN_PARAM_STRING, BUILTIN_PARAM_NUMBER, BUILTIN_PARAM_ANY},
        ST_DOUBLE, // Num
        false, false
    },

    {
        "Ifj.chr", 1,
        {BUILTIN_PARAM_NUMBER, BUILTIN_PARAM_ANY, BUILTIN_PARAM_ANY},
        ST_STRING, // Str
        false, false
    },

    /* Extensions */

    {
        "Ifj.read_bool", 0,
        {BUILTIN_PARAM_ANY, BUILTIN_PARAM_ANY, BUILTIN_PARAM_ANY},
        ST_BOOL, // Bool
        false, false
    }, /* BOOLTHEN */

    {
        "Ifj.is_int", 1,
        {BUILTIN_PARAM_ANY, BUILTIN_PARAM_ANY, BUILTIN_PARAM_ANY},
        ST_BOOL, // Bool
        false, true
    } /* STATICAN */
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

        if (st_find(gtab, key)) {
            continue;
        }

        // Insert signature "<qname>#<arity>" as ST_FUN with param_count set
        st_insert(gtab, key, ST_FUN, true);
        st_data *d = st_get(gtab, key);
        if (!d) {
            return false;
        }

        d->symbol_type = ST_FUN;
        d->param_count = (int) r->arity;
        d->data_type = r->return_type;
    }
    return true;
}

unsigned builtins_get_param_spec(const char *qname, builtin_param_kind *out_kinds, unsigned max_kinds) {
    if (!qname) {
        return 0;
    }

    for (size_t i = 0; i < sizeof(g_rows) / sizeof(g_rows[0]); ++i) {
        const builtin_row *r = &g_rows[i];

        if (strcmp(r->qname, qname) == 0) {
            if (out_kinds && max_kinds > 0) {
                unsigned to_copy = (r->arity < max_kinds) ? r->arity : max_kinds;
                for (unsigned j = 0; j < to_copy; ++j) {
                    out_kinds[j] = r->param_kinds[j];
                }
            }
            return r->arity;
        }
    }
    return 0;
}

bool builtins_is_builtin_qname(const char *name) {
    return name &&
           name[0] == 'I' &&
           name[1] == 'f' &&
           name[2] == 'j' &&
           name[3] == '.';
}
