/**
 * @file semantic.c
 * @brief IFJ25 Semantic analysis – Pass 1 (built-ins + signatures + bodies walk) with stdout debug prints.
 *
 * Step 1 (headers):
 *  - Seed IFJ built-ins (arity only) into the global function table.
 *  - Collect user function/getter/setter signatures (overload-by-arity) – recursively across nested blocks.
 *  - Verify that main() with arity 0 exists.
 *
 * Step 2 (bodies):
 *  - Walk bodies with scope_stack (locals & parameters),
 *  - Maintain a textual scope-ID stack ("1", "1.1", "1.1.2", ...),
 *  - Insert all declared symbols (functions, params, locals, accessors) into a global symtab
 *    with keys "<scope>::<name>",
 *  - Check local redeclare, break/continue context,
 *  - Arity checks for built-ins (strict), known user functions (if header seen),
 *  - Literal-only expression checks for arithmetic/relational collisions.
 *  - NOTE: Assignment to unknown identifiers is deferred to Pass 2 (no ERR_DEF here),
 *          because current AST may omit parameter lists for some function definitions.
 *
 * Error codes (error.h):
 *  - ERR_DEF     (3)  : main() arity must be 0
 *  - ERR_REDEF   (4)  : duplicate (name,arity); duplicate getter/setter; local redeclare
 *  - ERR_ARGNUM  (5)  : wrong number of arguments in function call
 *  - ERR_EXPR    (6)  : literal-only type error in an expression
 *  - ERR_SEM     (10) : break/continue outside a loop
 *  - ERR_INTERNAL(99) : internal failure (allocation, TS write, ...)
 */

#include <stdio.h>
#include <string.h>   // strlen, memcpy, strncmp, strcmp, snprintf, strstr
#include <stdbool.h>
#include <stdlib.h>   // NULL, malloc, free, qsort

#include "semantic.h"
#include "builtins.h"
#include "error.h"
#include "string.h"   // projektová string API (typ `string`, string_create, string_append_literal)
#include "symtable.h" // st_foreach atd.

/* =========================================================================
 *                        Error helper with st_dump
 * ========================================================================= */

/**
 * @brief Helper: dump info and symbol tables on non-SUCCESS rc, always log to stderr.
 */
static int sem_return(semantic_ctx *cxt, int rc, const char *where, const char *why) {
    fprintf(stderr,
            "[sem] RETURN rc=%d from %s (%s)\n",
            rc,
            where ? where : "(unknown)",
            why ? why : "no-detail");

    if (rc != SUCCESS) {
        fprintf(stdout,
                "[sem] non-SUCCESS rc=%d at %s, dumping function table, symtab and scopes:\n",
                rc,
                where ? where : "(unknown)");

        if (cxt && cxt->funcs) {
            fprintf(stdout, "---- FUNCTABLE DUMP ----\n");
            st_dump(cxt->funcs, stdout);
        }
        if (cxt && cxt->symtab) {
            fprintf(stdout, "---- SYMTAB DUMP ----\n");
            st_dump(cxt->symtab, stdout);
        }
        if (cxt) {
            fprintf(stdout, "---- SCOPES DUMP ----\n");
            scopes_dump(&cxt->scopes, stdout);
        }
    }
    return rc;
}

/* =========================================================================
 *                  Scope-ID stack helpers (textual scope paths)
 * ========================================================================= */

static void sem_scope_ids_init(sem_scope_id_stack *s) {
    if (!s) return;
    s->depth = -1;
}

/**
 * @brief Enter root scope: creates scope "1".
 */
static void sem_scope_ids_enter_root(sem_scope_id_stack *s) {
    if (!s) return;
    s->depth = 0;
    s->frames[0].child_count = 0;
    snprintf(s->frames[0].path, SEM_MAX_SCOPE_PATH, "1");
}

/**
 * @brief Enter a child scope: creates "P.N" where P is parent path and N is next child index.
 */
static void sem_scope_ids_enter_child(sem_scope_id_stack *s) {
    if (!s) return;

    /* If there is no root yet, start with it. */
    if (s->depth < 0) {
        sem_scope_ids_enter_root(s);
        return;
    }

    if (s->depth + 1 >= SEM_MAX_SCOPE_DEPTH) {
        /* Too deep – in practice this should not happen. */
        return;
    }

    int parent_idx = s->depth;
    int depth      = parent_idx + 1;

    sem_scope_id_frame *parent = &s->frames[parent_idx];
    sem_scope_id_frame *cur    = &s->frames[depth];

    int my_index = ++parent->child_count;
    cur->child_count = 0;

    /* Bezpečně omezíme délku rodičovského řetězce, aby se vešel s ".N" do bufferu. */
    size_t parent_len = strlen(parent->path);
    size_t max_parent = SEM_MAX_SCOPE_PATH - 16; /* rezerva na '.' + číslo + '\0' */
    if (parent_len < max_parent) {
        max_parent = parent_len;
    }

    snprintf(cur->path,
             SEM_MAX_SCOPE_PATH,
             "%.*s.%d",
             (int)max_parent,
             parent->path,
             my_index);

    s->depth = depth;
}

/**
 * @brief Leave current scope-ID frame (go one level up).
 */
static void sem_scope_ids_leave(sem_scope_id_stack *s) {
    if (!s) return;
    if (s->depth >= 0) {
        s->depth--;
    }
}

/**
 * @brief Get textual ID of current scope ("1", "1.1", ...), or "global" if none.
 */
static const char *sem_scope_ids_current(const sem_scope_id_stack *s) {
    if (!s || s->depth < 0) return "global";
    return s->frames[s->depth].path;
}

/* =========================================================================
 *                  Global symbol table helpers (symtab)
 * ========================================================================= */

/**
 * @brief Insert a symbol into the global symtab with current textual scope.
 *
 * symtab key format: "<scope>::<name>"
 *  - scope: sem_scope_ids_current(), e.g. "1.2.3" or "global"
 *  - name : identifier name (function/var/param/accessor base)
 *
 * symbol_type (3rd arg of st_insert) uses the same enum as the main symtable:
 *  - ST_FUN, ST_VAR, ST_PAR, ...
 *
 * For debug / later passes we also fill:
 *  - d->param_count = arity (for functions/accessors, 0 for vars/params),
 *  - d->ID          = name,
 *  - d->scope_name  = textual scope (e.g. "1.2.3").
 */
static int symtab_insert_symbol(semantic_ctx *cxt,
                                symbol_type symbol_type,
                                const char *name,
                                int arity)
{
    if (!cxt || !cxt->symtab || !name) {
        return SUCCESS; /* nothing to store or not initialized */
    }

    const char *scope = sem_scope_ids_current(&cxt->ids);
    char key[SEM_MAX_SCOPE_PATH + 128];

    snprintf(key, sizeof key, "%s::%s",
             scope ? scope : "global",
             name ? name : "(null)");

    /* Pokud už tam je (např. po ošetření redeklarace jinde), nevyhazujeme další chybu –
     * tahle tabulka je primárně debug / přehled.
     */
    if (st_find(cxt->symtab, key)) {
        return SUCCESS;
    }

    st_insert(cxt->symtab, key, symbol_type, true);
    st_data *d = st_get(cxt->symtab, key);
    if (!d) {
        return sem_return(
            cxt,
            error(ERR_INTERNAL, "symtab_insert_symbol: st_get failed for key '%s'", key),
            __func__,
            "symtab_insert_symbol st_get failed"
        );
    }

    d->symbol_type = symbol_type;
    d->param_count = arity;
    d->defined     = true;
    d->global      = false; /* všechny tyhle záznamy jsou "scoped", ne globální funkce */

    /* Save plain identifier name to d->ID */
    if (name) {
        d->ID = string_create(0);
        if (!d->ID || !string_append_literal(d->ID, (char*)name)) {
            return sem_return(
                cxt,
                error(ERR_INTERNAL, "symtab_insert_symbol: failed to store ID '%s'", name),
                __func__,
                "symtab_insert_symbol ID alloc failed"
            );
        }
    } else {
        d->ID = NULL;
    }

    /* Save scope path to d->scope_name */
    const char *scope_str = scope ? scope : "global";
    d->scope_name = string_create(0);
    if (!d->scope_name || !string_append_literal(d->scope_name, (char*)scope_str)) {
        return sem_return(
            cxt,
            error(ERR_INTERNAL, "symtab_insert_symbol: failed to store scope_name '%s'", scope_str),
            __func__,
            "symtab_insert_symbol scope_name alloc failed"
        );
    }

    return SUCCESS;
}

/**
 * @brief Insert accessor symbol (getter/setter) do globální symtab.
 *
 * Klíč v tabulce: "<scope>::<base>@get" nebo "<scope>::<base>@set"
 *  - scope: např. "1" (třída Program)
 *  - base : např. "value"
 *  - ID   : base (co se bude tisknout jako jméno)
 */
static int symtab_insert_accessor_symbol(semantic_ctx *cxt,
                                         bool is_setter,
                                         const char *base_name,
                                         int arity)
{
    if (!cxt || !cxt->symtab || !base_name) {
        return SUCCESS;
    }

    const char *scope = sem_scope_ids_current(&cxt->ids);
    char key[SEM_MAX_SCOPE_PATH + 128];

    snprintf(key, sizeof key, "%s::%s@%s",
             scope ? scope : "global",
             base_name,
             is_setter ? "set" : "get");

    /* Pokud už tam záznam je, neděláme chybu – tabulka je i pro debug. */
    if (st_find(cxt->symtab, key)) {
        return SUCCESS;
    }

    symbol_type st = is_setter ? ST_SETTER : ST_GETTER;

    st_insert(cxt->symtab, key, st, true);
    st_data *d = st_get(cxt->symtab, key);
    if (!d) {
        return sem_return(
            cxt,
            error(ERR_INTERNAL,
                  "symtab_insert_accessor_symbol: st_get failed for key '%s'", key),
            __func__,
            "symtab_insert_accessor_symbol st_get failed"
        );
    }

    d->symbol_type = st;
    d->param_count = arity;
    d->defined     = true;
    d->global      = false;

    /* ID = jméno property pro výpis (např. "value") */
    d->ID = string_create(0);
    if (!d->ID || !string_append_literal(d->ID, (char*)base_name)) {
        return sem_return(
            cxt,
            error(ERR_INTERNAL,
                  "symtab_insert_accessor_symbol: failed to store ID '%s'", base_name),
            __func__,
            "symtab_insert_accessor_symbol ID alloc failed"
        );
    }

    /* scope_name = textová cesta scope (např. "1") */
    const char *scope_str = scope ? scope : "global";
    d->scope_name = string_create(0);
    if (!d->scope_name || !string_append_literal(d->scope_name, (char*)scope_str)) {
        return sem_return(
            cxt,
            error(ERR_INTERNAL,
                  "symtab_insert_accessor_symbol: failed to store scope_name '%s'", scope_str),
            __func__,
            "symtab_insert_accessor_symbol scope_name alloc failed"
        );
    }

    return SUCCESS;
}

/* =========================================================================
 *                             Small helpers
 * ========================================================================= */

/**
 * @brief Compose a function signature key as "name#arity".
 */
static void make_fun_key(char *buf, size_t n, const char *name, int arity) {
    snprintf(buf, n, "%s#%d", name ? name : "(null)", arity);
}

/**
 * @brief Compose an accessor key as "get:base" or "set:base".
 */
static void make_acc_key(char *buf, size_t n, const char *base, bool is_setter) {
    snprintf(buf, n, "%s:%s", is_setter ? "set" : "get", base ? base : "(null)");
}

/**
 * @brief Count parameters in a singly-linked list of ast_parameter.
 */
static int count_params(ast_parameter p) {
    int n = 0;
    while (p) {
        ++n;
        p = p->next;
    }
    return n;
}

/**
 * @brief Obtain the root block of a class (walk up via parent).
 */
static ast_block class_root_block(ast_class cls) {
    if (!cls) return NULL;
    ast_block b = cls->current;
    while (b && b->parent) b = b->parent;
    return b;
}

/**
 * @brief Return true if identifier is a special global of the form "__name".
 */
static bool is_magic_global(const char *name) {
    return name && name[0]=='_' && name[1]=='_';
}

/**
 * @brief True if qname starts with "Ifj." (built-in namespace).
 */
static bool is_builtin_qname(const char *name) {
    return name && strncmp(name, "Ifj.", 4) == 0;
}

/* =========================================================================
 *                        Function-table ops + main()
 * ========================================================================= */

/* forward decls pro sběr hlaviček s propagací scope_name */
static int collect_from_block(semantic_ctx *cxt, ast_block blk, const char *scope_name);

/**
 * @brief Insert a user function signature (name, arity) into the global table.
 *        Raises ERR_REDEF (4) on duplicate (name,arity). Uloží scope_name a ID do st_data.
 *        V Pass-1 NEPŘIDÁVÁ funkci do cxt->symtab – to se děje až v bodies při AST_FUNCTION.
 */
static int funcs_insert_signature(semantic_ctx *cxt,
                                  const char *name, int arity,
                                  const char *scope_name)
{
    char key[256];
    make_fun_key(key, sizeof key, name, arity);
    if (st_find(cxt->funcs, (char*)key)) {
        fprintf(stdout, "[sem] REDEF function signature rejected: %s\n", key);
        return sem_return(
            cxt,
            error(ERR_REDEF, "duplicate function signature: %s", key),
            __func__,
            "duplicate function signature"
        );
    }

    fprintf(stdout, "[sem] insert function signature: %s (class-scope=%s)\n",
            key, scope_name ? scope_name : "(null)");

    st_insert(cxt->funcs, (char*)key, ST_FUN, true);
    st_data *d = st_get(cxt->funcs, (char*)key);
    if (!d) {
        return sem_return(
            cxt,
            error(ERR_INTERNAL, "failed to store function signature: %s", key),
            __func__,
            "st_get failed for function signature"
        );
    }

    d->symbol_type = ST_FUN;
    d->param_count = arity;
    d->defined     = false;
    d->global      = true;

    /* uložit scope_name (jméno třídy) */
    if (scope_name) {
        d->scope_name = string_create(0);
        if (!d->scope_name || !string_append_literal(d->scope_name, (char*)scope_name)) {
            return sem_return(
                cxt,
                error(ERR_INTERNAL, "failed to store function scope_name"),
                __func__,
                "string alloc for scope_name failed (func)"
            );
        }
    } else {
        d->scope_name = NULL;
    }

    /* uložit ID = název funkce (bez třídy) */
    if (name) {
        d->ID = string_create(0);
        if (!d->ID || !string_append_literal(d->ID, (char*)name)) {
            return sem_return(
                cxt,
                error(ERR_INTERNAL, "failed to store function name (ID)"),
                __func__,
                "string alloc for ID failed (func)"
            );
        }
    } else {
        d->ID = NULL;
    }

    /* do cxt->symtab se tahle funkce přidá až při průchodu těl (visit_node),
       aby měla správný číselný scope (1, 1.1, ...) */
    return SUCCESS;
}

/**
 * @brief Insert getter/setter signature (enforce at most one getter and one setter per base).
 *        Uloží scope_name a ID (u getteru/settingu = base).
 *        Do cxt->symtab se accessor přidá až v bodies, ne tady.
 */
static int funcs_insert_accessor(semantic_ctx *cxt,
                                 const char *base, bool is_setter,
                                 const char *scope_name,
                                 const char *setter_param_opt)
{
    char key[256];
    make_acc_key(key, sizeof key, base, is_setter);
    if (st_find(cxt->funcs, (char*)key)) {
        fprintf(stdout, "[sem] REDEF %s for '%s'\n",
                is_setter ? "setter" : "getter",
                base ? base : "(null)");
        return sem_return(
            cxt,
            error(ERR_REDEF,
                  is_setter ? "duplicate setter for '%s'" : "duplicate getter for '%s'",
                  base ? base : "(null)"),
            __func__,
            "duplicate getter/setter"
        );
    }

    fprintf(stdout, "[sem] insert %s for '%s' as %s (class-scope=%s)\n",
            is_setter ? "setter" : "getter",
            base ? base : "(null)",
            key,
            scope_name ? scope_name : "(null)");

    st_insert(cxt->funcs, (char*)key, ST_FUN, true);
    st_data *d = st_get(cxt->funcs, (char*)key);
    if (!d) {
        return sem_return(
            cxt,
            error(ERR_INTERNAL, "failed to store accessor signature: %s", key),
            __func__,
            "st_get failed for accessor"
        );
    }

    d->symbol_type = ST_FUN;
    d->param_count = is_setter ? 1 : 0;
    d->defined     = false;
    d->global      = true;

    /* scope_name */
    if (scope_name) {
        d->scope_name = string_create(0);
        if (!d->scope_name || !string_append_literal(d->scope_name, (char*)scope_name)) {
            return sem_return(
                cxt,
                error(ERR_INTERNAL, "failed to store accessor scope_name"),
                __func__,
                "string alloc for scope_name failed (accessor)"
            );
        }
    } else {
        d->scope_name = NULL;
    }

    /* ID = base jméno accessorů (např. "value") */
    if (base) {
        d->ID = string_create(0);
        if (!d->ID || !string_append_literal(d->ID, (char*)base)) {
            return sem_return(
                cxt,
                error(ERR_INTERNAL, "failed to store accessor base (ID)"),
                __func__,
                "string alloc for ID failed (accessor)"
            );
        }
    } else {
        d->ID = NULL;
    }

    (void)setter_param_opt; /* zatím nevyužito v Pass-1 */

    /* do cxt->symtab se getter/setter vloží v bodies (AST_GETTER/AST_SETTER) */
    return SUCCESS;
}

/**
 * @brief If the function is main(), verify arity==0 and remember presence.
 */
static int check_and_mark_main(semantic_ctx *cxt, const char *name, int arity) {
    if (!name || strcmp(name, "main") != 0) return SUCCESS;
    fprintf(stdout, "[sem] encountered main() with arity=%d\n", arity);
    if (arity != 0) {
        return sem_return(
            cxt,
            error(ERR_DEF, "main() must have 0 parameters"),
            __func__,
            "main() arity != 0"
        );
    }
    cxt->seen_main = true;
    return SUCCESS;
}

/* =========================================================================
 *                      Calls & literal-only expr checks
 * ========================================================================= */

/**
 * @brief True if a signature (name,arity) exists in the global function table.
 */
static bool funcs_has_signature(semantic_ctx *cxt, const char *name, int arity) {
    char key[256];
    make_fun_key(key, sizeof key, name, arity);
    return st_find(cxt->funcs, (char*)key) != NULL;
}

/**
 * @brief Arity checks for a call:
 *        - Built-ins: must match exactly now (ERR_ARGNUM=5).
 *        - User: if header is known, check now; else defer to Pass 2.
 */
static int check_call_arity(semantic_ctx *cxt, const char *name, int arity) {
    fprintf(stdout, "[sem] call: %s(arity=%d)\n", name ? name : "(null)", arity);
    if (!name) return SUCCESS;

    if (is_builtin_qname(name)) {
        if (!funcs_has_signature(cxt, name, arity)) {
            fprintf(stdout, "[sem]  -> ARGNUM mismatch for builtin %s\n", name);
            return sem_return(
                cxt,
                error(ERR_ARGNUM,
                      "wrong number of arguments for %s (arity=%d)", name, arity),
                __func__,
                "builtin argnum mismatch"
            );
        }
        return SUCCESS;
    }

    /* user function */
    if (funcs_has_signature(cxt, name, arity)) return SUCCESS;
    /* header not known yet: defer to Pass 2 */
    return SUCCESS;
}

typedef enum { LK_UNKNOWN=0, LK_NUM, LK_STRING } lit_kind;

/**
 * @brief True if expression is exactly an integer literal.
 */
static bool expr_is_int_literal(ast_expression e) {
    return e && e->type == AST_VALUE && e->operands.identity.value_type == AST_VALUE_INT;
}

/**
 * @brief Coarse literal-kind of a value expression: number/string/unknown.
 *
 * DŮLEŽITÉ: Parser může identifikátory kódovat jako AST_VALUE_STRING.
 * V Pass-1 je NESMÍME považovat za string *literály*, jinak vznikají falešné ERR_EXPR
 * např. u (b < 2). Proto pro AST_VALUE_STRING vrať LK_UNKNOWN.
 */
static lit_kind lit_of_value(ast_expression e) {
    if (!e || e->type != AST_VALUE) return LK_UNKNOWN;
    switch (e->operands.identity.value_type) {
        case AST_VALUE_INT:
        case AST_VALUE_FLOAT:  return LK_NUM;
        case AST_VALUE_STRING: return LK_UNKNOWN;
        default:               return LK_UNKNOWN;
    }
}

static int visit_expression(semantic_ctx *cxt, ast_expression e);

/**
 * @brief Visit an expression (only Pass-1 checks we can do early).
 */
static int visit_expression(semantic_ctx *cxt, ast_expression e) {
    if (!e) return SUCCESS;

    switch (e->type) {
        case AST_VALUE:
        case AST_NOT:
        case AST_NOT_NULL:
            return SUCCESS;

        case AST_FUNCTION_CALL: {
            const char *fname = e->operands.function_call.name;
            int ar = (int)e->operands.function_call.parameter_count; /* z AST */
            return check_call_arity(cxt, fname, ar);
        }

        /* binary-like shapes */
        case AST_ADD: case AST_SUB: case AST_MUL: case AST_DIV:
        case AST_EQUALS: case AST_NOT_EQUAL:
        case AST_LT: case AST_LE: case AST_GT: case AST_GE:
        case AST_AND: case AST_OR:
        case AST_TERNARY:
        case AST_IS: {
            ast_expression L = e->operands.binary_op.left;
            ast_expression R = e->operands.binary_op.right;
            int rc = visit_expression(cxt, L);
            if (rc != SUCCESS) return rc;
            rc = visit_expression(cxt, R);
            if (rc != SUCCESS) return rc;

            /* literal-only checks */
            lit_kind lkL = lit_of_value(L);
            lit_kind lkR = lit_of_value(R);

            if (e->type == AST_ADD) {
                if (lkL && lkR) {
                    if (!((lkL==LK_NUM && lkR==LK_NUM) || (lkL==LK_STRING && lkR==LK_STRING))) {
                        return sem_return(
                            cxt,
                            error(ERR_EXPR, "invalid literal '+' operands"),
                            __func__,
                            "invalid literal '+' operands"
                        );
                    }
                }
            } else if (e->type == AST_SUB || e->type == AST_DIV) {
                if (lkL && lkR) {
                    if (!(lkL==LK_NUM && lkR==LK_NUM)) {
                        return sem_return(
                            cxt,
                            error(ERR_EXPR, "invalid literal arithmetic operands"),
                            __func__,
                            "invalid literal '-' or '/' operands"
                        );
                    }
                }
            } else if (e->type == AST_MUL) {
                if (lkL && lkR) {
                    bool ok = (lkL==LK_NUM && lkR==LK_NUM)
                           || (lkL==LK_STRING && expr_is_int_literal(R));
                    if (!ok) {
                        return sem_return(
                            cxt,
                            error(ERR_EXPR, "invalid literal '*' operands"),
                            __func__,
                            "invalid literal '*' operands"
                        );
                    }
                }
            } else if (e->type == AST_LT || e->type == AST_LE ||
                       e->type == AST_GT || e->type == AST_GE) {
                if (lkL && lkR) {
                    if (!(lkL==LK_NUM && lkR==LK_NUM)) {
                        return sem_return(
                            cxt,
                            error(ERR_EXPR, "relational operators require numeric literals"),
                            __func__,
                            "relational op on non-numeric literals"
                        );
                    }
                }
            } else if (e->type == AST_IS) {
                /* Pokud by RHS byl explicitní název typu, ověřil by se zde. */
            }
            return SUCCESS;
        }

        default:
            return SUCCESS;
    }
}

/* =========================================================================
 *          Copy IFJ builtins from cxt->funcs do cxt->symtab (scope=global)
 * ========================================================================= */

/* callback pro st_foreach nad cxt->funcs – vloží IFj.* do symtab */
static void sem_copy_builtin_cb(const char *key, st_data *data, void *user_data) {
    semantic_ctx *cxt = (semantic_ctx*)user_data;
    if (!cxt || !key || !data) return;

    /* bereme jen builtiny Ifj.* */
    if (strncmp(key, "Ifj.", 4) != 0) {
        return;
    }

    /* name = část před '#' (Ifj.read_num#0 -> Ifj.read_num) */
    const char *hash = strchr(key, '#');
    size_t len = hash ? (size_t)(hash - key) : strlen(key);
    char name_buf[128];
    if (len >= sizeof(name_buf)) {
        len = sizeof(name_buf) - 1;
    }
    memcpy(name_buf, key, len);
    name_buf[len] = '\0';

    int arity = data->param_count;

    /* ids.depth je v tu chvíli -1 => scope="global" */
    (void)symtab_insert_symbol(cxt, ST_FUN, name_buf, arity);
}

/* zavolá callback nad celou funkční tabulkou */
static void sem_copy_builtins_to_symtab(semantic_ctx *cxt) {
    if (!cxt || !cxt->funcs || !cxt->symtab) return;
    st_foreach(cxt->funcs, sem_copy_builtin_cb, cxt);
}

/* =========================================================================
 *                       Bodies walk (scopes & nodes)
 * ========================================================================= */

static int visit_block(semantic_ctx *cxt, ast_block blk);
static int visit_node (semantic_ctx *cxt, ast_node  n);

/**
 * @brief Declare a function parameter list in the current scope.
 */
static int declare_param_list(semantic_ctx *cxt, ast_parameter params) {
    for (ast_parameter p = params; p; p = p->next) {
        fprintf(stdout, "[sem] param declare: %s (scope=%s)\n",
                p->name,
                sem_scope_ids_current(&cxt->ids));

        if (!scopes_declare_local(&cxt->scopes, p->name, true)) {
            return sem_return(
                cxt,
                error(ERR_REDEF,
                      "parameter '%s' redeclared in the same scope", p->name),
                __func__,
                "parameter redeclared in same scope"
            );
        }

        st_data *d = scopes_lookup_in_current(&cxt->scopes, p->name);
        if (d) d->symbol_type = ST_PAR;

        /* uložíme parametr i do symtab */
        int rc = symtab_insert_symbol(cxt, ST_PAR, p->name, 0);
        if (rc != SUCCESS) return rc;
    }
    return SUCCESS;
}

/**
 * @brief Visit a block: push scope, assign scope-ID, visit all nodes, pop.
 */
static int visit_block(semantic_ctx *cxt, ast_block blk) {
    if (!blk) return SUCCESS;

    fprintf(stdout, "[sem] scope PUSH (blk=%p)\n", (void*)blk);

    /* scope-ID: první blok vytvoří root "1", další jsou děti */
    if (cxt->ids.depth < 0) {
        sem_scope_ids_enter_root(&cxt->ids);
    } else {
        sem_scope_ids_enter_child(&cxt->ids);
    }

    scopes_push(&cxt->scopes);

    for (ast_node n = blk->first; n; n = n->next) {
        int rc = visit_node(cxt, n);
        if (rc != SUCCESS) {
            scopes_pop(&cxt->scopes);
            sem_scope_ids_leave(&cxt->ids);
            fprintf(stdout, "[sem] scope POP (early err, blk=%p)\n", (void*)blk);
            return rc;
        }
    }

    if (!scopes_pop(&cxt->scopes)) {
        sem_scope_ids_leave(&cxt->ids);
        return sem_return(
            cxt,
            error(ERR_INTERNAL, "scope stack underflow"),
            __func__,
            "scope stack underflow in visit_block"
        );
    }

    sem_scope_ids_leave(&cxt->ids);
    fprintf(stdout, "[sem] scope POP (blk=%p)\n", (void*)blk);
    return SUCCESS;
}

/**
 * @brief Visit a single AST node in statement position.
 */
static int visit_node(semantic_ctx *cxt, ast_node n) {
    if (!n) return SUCCESS;

    switch (n->type) {
        case AST_BLOCK:
            return visit_block(cxt, n->data.block);

        case AST_CONDITION: {
            int rc = visit_expression(cxt, n->data.condition.condition);
            if (rc != SUCCESS) return rc;
            rc = visit_block(cxt, n->data.condition.if_branch);
            if (rc != SUCCESS) return rc;
            rc = visit_block(cxt, n->data.condition.else_branch);
            return rc;
        }

        case AST_WHILE_LOOP: {
            int rc = visit_expression(cxt, n->data.while_loop.condition);
            if (rc != SUCCESS) return rc;
            cxt->loop_depth++;
            fprintf(stdout, "[sem] while enter (depth=%d, scope=%s)\n",
                    cxt->loop_depth,
                    sem_scope_ids_current(&cxt->ids));
            rc = visit_block(cxt, n->data.while_loop.body);
            fprintf(stdout, "[sem] while leave (depth=%d, scope=%s)\n",
                    cxt->loop_depth,
                    sem_scope_ids_current(&cxt->ids));
            cxt->loop_depth--;
            return rc;
        }

        case AST_BREAK:
            if (cxt->loop_depth <= 0) {
                return sem_return(
                    cxt,
                    error(ERR_SEM, "break outside of loop"),
                    __func__,
                    "break outside of loop"
                );
            }
            fprintf(stdout, "[sem] break (ok, depth=%d, scope=%s)\n",
                    cxt->loop_depth,
                    sem_scope_ids_current(&cxt->ids));
            return SUCCESS;

        case AST_CONTINUE:
            if (cxt->loop_depth <= 0) {
                return sem_return(
                    cxt,
                    error(ERR_SEM, "continue outside of loop"),
                    __func__,
                    "continue outside of loop"
                );
            }
            fprintf(stdout, "[sem] continue (ok, depth=%d, scope=%s)\n",
                    cxt->loop_depth,
                    sem_scope_ids_current(&cxt->ids));
            return SUCCESS;

        case AST_EXPRESSION:
            return visit_expression(cxt, n->data.expression);

        case AST_VAR_DECLARATION: {
            const char *name = n->data.declaration.name;
            fprintf(stdout, "[sem] var declare: %s (scope=%s)\n",
                    name ? name : "(null)",
                    sem_scope_ids_current(&cxt->ids));

            if (!scopes_declare_local(&cxt->scopes, name, true)) {
                return sem_return(
                    cxt,
                    error(ERR_REDEF,
                          "variable '%s' already declared in this scope",
                          name ? name : "(null)"),
                    __func__,
                    "variable redeclared in same scope"
                );
            }
            st_data *d = scopes_lookup_in_current(&cxt->scopes, name);
            if (d) d->symbol_type = ST_VAR;

            /* uložit lokální proměnnou do symtab */
            int rc = symtab_insert_symbol(cxt, ST_VAR, name, 0);
            if (rc != SUCCESS) return rc;

            return SUCCESS;
        }

        case AST_ASSIGNMENT: {
            const char *name = n->data.assignment.name;
            fprintf(stdout, "[sem] assign to: %s (scope=%s)\n",
                    name ? name : "(null)",
                    sem_scope_ids_current(&cxt->ids));
            /* Pass 1 policy: __globals ok; ostatní neznámé LHS jen zalogujeme a odložíme do Pass 2 */
            if (!is_magic_global(name)) {
                if (!scopes_lookup(&cxt->scopes, name)) {
                    fprintf(stdout,
                            "[sem]  -> unresolved LHS in Pass 1 (defer): %s\n",
                            name ? name : "(null)");
                }
            }
            return visit_expression(cxt, n->data.assignment.value);
        }

        case AST_FUNCTION: {
            ast_function f = n->data.function;
            fprintf(stdout, "[sem] function body: %s (scope=%s)\n",
                    f->name ? f->name : "(null)",
                    sem_scope_ids_current(&cxt->ids));

            int rc;
            int ar = count_params(f->parameters);

            /* nejdřív zaregistrujeme samotnou funkci do symtab
               v aktuálním scope (např. 1.1 pro main) */
            rc = symtab_insert_symbol(cxt, ST_FUN, f->name, ar);
            if (rc != SUCCESS) {
                return rc;
            }

            /* funkční scope – parametry */
            scopes_push(&cxt->scopes);
            sem_scope_ids_enter_child(&cxt->ids);

            rc = declare_param_list(cxt, f->parameters);
            if (rc != SUCCESS) {
                scopes_pop(&cxt->scopes);
                sem_scope_ids_leave(&cxt->ids);
                return rc;
            }

            /* tělo funkce (blok) – další vnořený scope */
            rc = visit_block(cxt, f->code);
            if (rc != SUCCESS) {
                scopes_pop(&cxt->scopes);
                sem_scope_ids_leave(&cxt->ids);
                return rc;
            }

            if (!scopes_pop(&cxt->scopes)) {
                sem_scope_ids_leave(&cxt->ids);
                return sem_return(
                    cxt,
                    error(ERR_INTERNAL,
                          "scope stack underflow (function)"),
                    __func__,
                    "scope stack underflow in function body"
                );
            }

            sem_scope_ids_leave(&cxt->ids);
            return SUCCESS;
        }

        case AST_CALL_FUNCTION: {
            ast_fun_call call = n->data.function_call;
            int ar = count_params(call->parameters);
            return check_call_arity(cxt, call->name, ar);
        }

        case AST_RETURN:
            return visit_expression(cxt, n->data.return_expr.output);

        case AST_GETTER: {
            fprintf(stdout, "[sem] getter body: %s (scope=%s)\n",
                    n->data.getter.name ? n->data.getter.name : "(null)",
                    sem_scope_ids_current(&cxt->ids));

            int rc;

            /* getter jako accessor symbol v aktuálním scope (např. 1) */
            rc = symtab_insert_accessor_symbol(cxt, false, n->data.getter.name, 0);
            if (rc != SUCCESS) {
                return rc;
            }

            scopes_push(&cxt->scopes);
            sem_scope_ids_enter_child(&cxt->ids);

            rc = visit_block(cxt, n->data.getter.body);
            if (rc != SUCCESS) {
                scopes_pop(&cxt->scopes);
                sem_scope_ids_leave(&cxt->ids);
                return rc;
            }
            if (!scopes_pop(&cxt->scopes)) {
                sem_scope_ids_leave(&cxt->ids);
                return sem_return(
                    cxt,
                    error(ERR_INTERNAL,
                          "scope stack underflow (getter)"),
                    __func__,
                    "scope stack underflow in getter"
                );
            }

            sem_scope_ids_leave(&cxt->ids);
            return SUCCESS;
        }

        case AST_SETTER: {
            fprintf(stdout, "[sem] setter body: %s (scope=%s)\n",
                    n->data.setter.name ? n->data.setter.name : "(null)",
                    sem_scope_ids_current(&cxt->ids));

            int rc;

            /* setter jako accessor symbol v aktuálním scope (např. 1) */
            rc = symtab_insert_accessor_symbol(cxt, true, n->data.setter.name, 1);
            if (rc != SUCCESS) {
                return rc;
            }

            scopes_push(&cxt->scopes);
            sem_scope_ids_enter_child(&cxt->ids);

            if (!scopes_declare_local(&cxt->scopes, n->data.setter.param, true)) {
                sem_scope_ids_leave(&cxt->ids);
                scopes_pop(&cxt->scopes);
                return sem_return(
                    cxt,
                    error(ERR_REDEF,
                          "setter parameter redeclared: %s",
                          n->data.setter.param),
                    __func__,
                    "setter parameter redeclared"
                );
            }
            st_data *d = scopes_lookup_in_current(&cxt->scopes, n->data.setter.param);
            if (d) d->symbol_type = ST_PAR;

            /* setter parametr do symtab */
            rc = symtab_insert_symbol(cxt, ST_PAR, n->data.setter.param, 0);
            if (rc != SUCCESS) {
                sem_scope_ids_leave(&cxt->ids);
                scopes_pop(&cxt->scopes);
                return rc;
            }

            rc = visit_block(cxt, n->data.setter.body);
            if (rc != SUCCESS) {
                scopes_pop(&cxt->scopes);
                sem_scope_ids_leave(&cxt->ids);
                return rc;
            }
            if (!scopes_pop(&cxt->scopes)) {
                sem_scope_ids_leave(&cxt->ids);
                return sem_return(
                    cxt,
                    error(ERR_INTERNAL,
                          "scope stack underflow (setter)"),
                    __func__,
                    "scope stack underflow in setter"
                );
            }

            sem_scope_ids_leave(&cxt->ids);
            return SUCCESS;
        }
    }

    return SUCCESS;
}

/* =========================================================================
 *                      Header collection (recursive)
 * ========================================================================= */

/**
 * @brief Collect function/getter/setter headers anywhere inside the class block tree.
 *        (Recurses into nested blocks; does NOT descend into function bodies.)
 *        Propagates class scope name into the stored st_data.scope_name.
 *
 * Funkce/accessory jsou zároveň zapsány do cxt->funcs, ale NE do cxt->symtab.
 */
static int collect_headers(semantic_ctx *cxt, ast tree) {
    for (ast_class cls = tree->class_list; cls; cls = cls->next) {
        const char *cls_name = cls->name ? cls->name : "(anonymous)";
        ast_block root = class_root_block(cls);
        if (!root) continue;

        int rc = collect_from_block(cxt, root, cls_name);
        if (rc != SUCCESS) return rc;
    }
    return SUCCESS;
}

/**
 * @brief Recursive walk over a block to collect headers.
 *        - collects AST_FUNCTION / AST_GETTER / AST_SETTER
 *        - recurses into child AST_BLOCK nodes
 *        - does NOT traverse into function bodies (headers only)
 *        - passes scope_name (class) into each inserted symbol
 */
static int collect_from_block(semantic_ctx *cxt, ast_block blk, const char *scope_name) {
    if (!blk) return SUCCESS;

    for (ast_node n = blk->first; n; n = n->next) {
        switch (n->type) {
            case AST_FUNCTION: {
                ast_function f = n->data.function;
                const char *fname = f->name;
                fprintf(stdout, "[sem] header: %s params=",
                        fname ? fname : "(null)");
                const char *sep = "";
                for (ast_parameter p = f->parameters; p; p = p->next) {
                    fprintf(stdout, "%s%s", sep, p->name);
                    sep = ", ";
                }
                fprintf(stdout, "\n");
                int ar = count_params(f->parameters);  /* parser by měl vyplnit seznam parametrů */
                int rc = funcs_insert_signature(cxt, fname, ar, scope_name);
                if (rc != SUCCESS) return rc;
                rc = check_and_mark_main(cxt, fname, ar);
                if (rc != SUCCESS) return rc;
                /* Do NOT recurse into f->code in header phase */
                break;
            }
            case AST_GETTER: {
                int rc = funcs_insert_accessor(cxt, n->data.getter.name, false, scope_name, NULL);
                if (rc != SUCCESS) return rc;
                break;
            }
            case AST_SETTER: {
                const char *param = n->data.setter.param;
                int rc = funcs_insert_accessor(cxt, n->data.setter.name, true, scope_name, param);
                if (rc != SUCCESS) return rc;
                break;
            }
            case AST_BLOCK: {
                int rc = collect_from_block(cxt, n->data.block, scope_name);
                if (rc != SUCCESS) return rc;
                break;
            }
            default:
                /* ostatní uzly ignorujeme */
                break;
        }
    }
    return SUCCESS;
}

/* =========================================================================
 *                  Pretty-print of symtab grouped by scopes
 * ========================================================================= */

typedef struct {
    const char  *scope;       /* např. "1.1.1.1" nebo "global" */
    const char  *name;        /* identifikátor bez prefixu scope:: */
    symbol_type  kind;        /* ST_FUN / ST_VAR / ST_PAR / ... */
    int          arity;       /* pro funkce/accessory; jinak 0 */
} sem_row;

typedef struct {
    sem_row *rows;
    size_t   capacity;
    size_t   count;
} sem_row_acc;

/* callback pro st_foreach – sběr řádků (dvoufázově: nejdřív count, pak fill) */
static void sem_collect_rows_cb(const char *key, st_data *data, void *user_data) {
    sem_row_acc *acc = (sem_row_acc*)user_data;
    if (!acc) return;

    /* 1. průchod – jen spočítat */
    if (!acc->rows) {
        acc->count++;
        return;
    }

    /* 2. průchod – plnit pole */
    if (acc->count >= acc->capacity) return;

    /* scope: z data->scope_name->data, jinak "global" */
    const char *scope_str = "global";
    if (data && data->scope_name && data->scope_name->data && data->scope_name->length > 0) {
        scope_str = data->scope_name->data;
    }

    /* name: přednost má data->ID (např. "value" pro getter/setter),
       jinak část za "::" v key */
    const char *name_str = NULL;
    if (data && data->ID && data->ID->data && data->ID->length > 0) {
        name_str = data->ID->data;
    } else {
        name_str = key;
        const char *sep = strstr(key, "::");
        if (sep && sep[2] != '\0') {
            name_str = sep + 2;
        }
    }

    sem_row *r = &acc->rows[acc->count++];
    r->scope = scope_str;
    r->name  = name_str;
    r->kind  = data ? data->symbol_type : ST_VAR;
    r->arity = data ? data->param_count : 0;
}

/* řazení: scope, pak name, pak kind + arity jen pro deterministiku */
static int sem_row_cmp(const void *a, const void *b) {
    const sem_row *ra = (const sem_row*)a;
    const sem_row *rb = (const sem_row*)b;

    int sc = strcmp(ra->scope, rb->scope);
    if (sc != 0) return sc;

    int nc = strcmp(ra->name, rb->name);
    if (nc != 0) return nc;

    if ((int)ra->kind != (int)rb->kind) {
        return (int)ra->kind - (int)rb->kind;
    }
    return ra->arity - rb->arity;
}

static const char *sem_kind_to_str(symbol_type k) {
    switch (k) {
        case ST_VAR:    return "var";
        case ST_CONST:  return "const";
        case ST_FUN:    return "funkce";
        case ST_PAR:    return "param";
        case ST_GLOB:   return "glob";
        case ST_GETTER: return "getter";
        case ST_SETTER: return "setter";
        default:        return "sym";
    }
}

/**
 * @brief Hezký výpis cxt->symtab podle scope.
 */
static void sem_pretty_print_symtab(semantic_ctx *cxt) {
    if (!cxt || !cxt->symtab) {
        fprintf(stdout, "==== SYMTAB (no table) ====\n");
        return;
    }

    /* 1) zjistit počet záznamů pomocí st_foreach */
    sem_row_acc acc = { NULL, 0, 0 };
    st_foreach(cxt->symtab, sem_collect_rows_cb, &acc);

    if (acc.count == 0) {
        fprintf(stdout, "==== SYMTAB (empty) ====\n");
        return;
    }

    /* 2) alokovat pole a znovu naplnit */
    sem_row *rows = calloc(acc.count, sizeof *rows);
    if (!rows) {
        fprintf(stdout, "==== SYMTAB (alloc failed, fallback dump) ====\n");
        st_dump(cxt->symtab, stdout);
        return;
    }

    acc.rows     = rows;
    acc.capacity = acc.count;
    acc.count    = 0;

    st_foreach(cxt->symtab, sem_collect_rows_cb, &acc);

    qsort(rows, acc.count, sizeof *rows, sem_row_cmp);

    fprintf(stdout,
            "===========================================================\n"
            "SYMBOLICKÁ TABULKA PO semantic_pass1 PRO DANÝ PROGRAM\n"
            "===========================================================\n\n");

    const char *current_scope = NULL;
    for (size_t i = 0; i < acc.count; ++i) {
        sem_row *r = &rows[i];

        if (!current_scope || strcmp(current_scope, r->scope) != 0) {
            /* nový scope – oddělit horizontálně a vytisknout hlavičku */
            current_scope = r->scope;
            fprintf(stdout,
                    "-----------------------------------------------------------\n"
                    "Scope: %s\n"
                    "-----------------------------------------------------------\n",
                    current_scope);
            fprintf(stdout, "%-20s %-10s %-5s\n", "Jméno", "Druh", "Ar");
            fprintf(stdout, "%-20s %-10s %-5s\n", "--------------------", "----------", "-----");
        }

        const char *kind_str = sem_kind_to_str(r->kind);
        fprintf(stdout, "%-20s %-10s %-5d\n", r->name, kind_str, r->arity);
    }

    fprintf(stdout, "-----------------------------------------------------------\n");

    free(rows);
}

/* =========================================================================
 *                              Entry point
 * ========================================================================= */

/**
 * @brief Run the first semantic pass over the AST.
 */
int semantic_pass1(ast tree) {
    semantic_ctx cxt;
    memset(&cxt, 0, sizeof cxt);

    cxt.funcs = st_init();
    if (!cxt.funcs) {
        return sem_return(
            &cxt,
            error(ERR_INTERNAL, "failed to init global function table"),
            __func__,
            "st_init(funcs) failed"
        );
    }

    cxt.symtab = st_init();
    if (!cxt.symtab) {
        int rc = sem_return(
            &cxt,
            error(ERR_INTERNAL, "failed to init global symbol table"),
            __func__,
            "st_init(symtab) failed"
        );
        st_free(cxt.funcs);
        return rc;
    }

    scopes_init(&cxt.scopes);
    sem_scope_ids_init(&cxt.ids);

    cxt.loop_depth = 0;
    cxt.seen_main  = false;

    fprintf(stdout, "[sem] seeding IFJ built-ins...\n");
    builtins_config bcfg = (builtins_config){ .ext_boolthen=false, .ext_statican=false };
    if (!builtins_install(cxt.funcs, bcfg)) {
        int rc = sem_return(
            &cxt,
            error(ERR_INTERNAL, "failed to install built-ins"),
            __func__,
            "builtins_install() failed"
        );
        st_free(cxt.funcs);
        st_free(cxt.symtab);
        return rc;
    }
    fprintf(stdout, "[sem] built-ins seeded.\n");

    /* zkopíruj IFJ builtiny z funkční tabulky do globální symtab (scope=global) */
    sem_copy_builtins_to_symtab(&cxt);

    fprintf(stdout, "==== FUNCTABLE AFTER BUILTINS ====\n");
    st_dump(cxt.funcs, stdout);
    fprintf(stdout, "==== SYMTAB AFTER BUILTINS ====\n");
    sem_pretty_print_symtab(&cxt);
    fprintf(stdout, "==== SCOPES AFTER BUILTINS ====\n");
    scopes_dump(&cxt.scopes, stdout);

    int rc = collect_headers(&cxt, tree);
    if (rc != SUCCESS) {
        rc = sem_return(&cxt, rc, __func__, "collect_headers() failed");
        st_free(cxt.funcs);
        st_free(cxt.symtab);
        return rc;
    }

    fprintf(stdout, "==== FUNCTABLE AFTER COLLECTION ====\n");
    st_dump(cxt.funcs, stdout);
    fprintf(stdout, "==== SYMTAB AFTER COLLECTION ====\n");
    sem_pretty_print_symtab(&cxt);
    fprintf(stdout, "==== SCOPES AFTER COLLECTION ====\n");
    scopes_dump(&cxt.scopes, stdout);

    if (!cxt.seen_main) {
        rc = sem_return(
            &cxt,
            error(ERR_DEF, "missing main() with 0 parameters"),
            __func__,
            "main() with arity 0 not found"
        );
        st_free(cxt.funcs);
        st_free(cxt.symtab);
        return rc;
    }

    /* Walk all class root blocks – zde se začnou generovat scope-ID "1", "1.1", ... */
    for (ast_class cls = tree->class_list; cls; cls = cls->next) {
        ast_block root = class_root_block(cls);
        if (!root) continue;
        rc = visit_block(&cxt, root);
        if (rc != SUCCESS) {
            rc = sem_return(&cxt, rc, __func__, "visit_block() failed");
            st_free(cxt.funcs);
            st_free(cxt.symtab);
            return rc;
        }
    }

    /* Final dumps */
    fprintf(stdout, "==== FUNCTABLE AFTER BODIES ====\n");
    st_dump(cxt.funcs, stdout);
    fprintf(stdout, "==== SYMTAB AFTER BODIES ====\n");
    sem_pretty_print_symtab(&cxt);
    fprintf(stdout, "==== SCOPES AFTER BODIES ====\n");
    scopes_dump(&cxt.scopes, stdout);

    return SUCCESS;
}
