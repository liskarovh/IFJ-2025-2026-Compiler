/**
 * @file semantic.c
 * @brief IFJ25 Semantic analysis – Pass 1 (headers + bodies) and Pass 2 (identifier & call checks).
 *
 * Pass 1:
 *  - Seed IFJ built-ins (arity only) into the global function table via builtins_install().
 *  - Collect user function/getter/setter signatures (overload-by-arity) – recursively across nested blocks.
 *  - Verify that main() with arity 0 exists.
 *  - Walk bodies with a scope stack (locals & parameters),
 *  - Maintain a textual scope-ID stack ("1", "1.1", "1.1.2", ...),
 *  - Insert all declared symbols (functions, params, locals, accessors) into a global symtab
 *    with keys "<scope>::<name>",
 *  - Check local redeclare, break/continue context,
 *  - Arity checks for known user functions (if header seen),
 *  - Literal-only expression checks for arithmetic/relational collisions.
 *  - Assignment LHS is checked in Pass-1: unknown identifiers cause ERR_DEF (3),
 *    except for magic globals "__name" and known setters.
 *  - Built-in calls (Ifj.*) mají v Pass-1 navíc kontrolu arity (z tabulky builtins) + typů argumentů,
 *    pokud jsou literály (špatný typ -> ERR_ARGNUM=5).
 *
 * Pass 2:
 *  - Druhý průchod nad AST s novým scope stackem:
 *    - rozlišení identifikátorů (lokální proměnné, parametry, přístupové metody),
 *    - kontrola volání funkcí (uživatelské i Ifj.*) podle tabulky funkcí (funcs),
 *    - další ERR_DEF/ERR_ARGNUM podle zadání.
 *
 * Error codes (error.h):
 *  - ERR_DEF     (3)  : main() arity must be 0; use-before-declare of a local / unknown LHS / undefined ident / call
 *  - ERR_REDEF   (4)  : duplicate (name,arity) in one class; duplicate getter/setter in one class; local redeclare
 *  - ERR_ARGNUM  (5)  : wrong number or literal types of arguments in function/builtin call
 *  - ERR_EXPR    (6)  : literal-only type error in an expression
 *  - ERR_SEM     (10) : break/continue outside of loop
 *  - ERR_INTERNAL(99) : internal failure (allocation, TS write, ...)
 */

#include <stdio.h>
#include <string.h>   // strlen, memcpy, strncmp, strcmp, strstr, strchr
#include <stdbool.h>
#include <stdlib.h>   // NULL, malloc, free, qsort, memset

#include "semantic.h"
#include "builtins.h"
#include "error.h"
#include "string.h"   // project string API (type `string`, string_create, string_append_literal)
#include "symtable.h" // st_foreach etc.

/* Forward declaration: implemented at the end of this file. */
int semantic_pass2(semantic *table, ast syntax_tree);

/* =========================================================================
 *          Global magic-identifier registry for code generator
 * ========================================================================= */
/**
 * @brief Debug: vytiskne aktuální seznam magických globálních proměnných.
 *
 * Volá semantic_get_magic_globals(), vypíše je na stdout a zase je uvolní.
 * g_magic_globals uvnitř semantic.c zůstává nedotčený.
 */
static void sem_debug_print_magic_globals(void)
{
    char **globals = NULL;
    size_t count   = 0;

    int rc = semantic_get_magic_globals(&globals, &count);
    if (rc != SUCCESS) {
        fprintf(stdout,
                "[sem] magic globals: semantic_get_magic_globals failed (rc=%d)\n",
                rc);
        return;
    }

    fprintf(stdout, "[sem] magic globals (%zu):\n", count);
    for (size_t i = 0; i < count; ++i) {
        fprintf(stdout, "  - %s\n",
                globals[i] ? globals[i] : "(null)");
        free(globals[i]);
    }
    free(globals);
}

/**
 * We collect names of all magic globals ("__name") that appear as LHS
 * of assignments during semantic analysis. This registry is process-global,
 * because our semantic_pass1() currently owns the whole semantic_ctx
 * internally and frees it before returning to main.
 *
 * For the purposes of this project, one global registry is enough.
 */

typedef struct {
    char  **items;
    size_t count;
    size_t capacity;
} sem_magic_globals;

static sem_magic_globals g_magic_globals = { NULL, 0, 0 };

/**
 * @brief Reset the registry (called before starting semantic_pass1()).
 */
static void sem_magic_globals_reset(void)
{
    if (g_magic_globals.items) {
        for (size_t i = 0; i < g_magic_globals.count; ++i) {
            free(g_magic_globals.items[i]);
        }
        free(g_magic_globals.items);
    }
    g_magic_globals.items    = NULL;
    g_magic_globals.count    = 0;
    g_magic_globals.capacity = 0;
}

/**
 * @brief Add a magic-global name into the registry (if not already present).
 *
 * Stores a heap-allocated copy of the name.
 */
static int sem_magic_globals_add(const char *name)
{
    if (!name) {
        return SUCCESS;
    }

    /* ensure it's really magic "__something" (defensive) */
    if (name[0] != '_' || name[1] != '_') {
        return SUCCESS;
    }

    /* deduplicate: linear search is fine for small counts */
    for (size_t i = 0; i < g_magic_globals.count; ++i) {
        if (strcmp(g_magic_globals.items[i], name) == 0) {
            return SUCCESS;
        }
    }

    /* grow capacity if needed */
    if (g_magic_globals.count == g_magic_globals.capacity) {
        size_t new_cap = (g_magic_globals.capacity == 0)
                         ? 8
                         : g_magic_globals.capacity * 2;
        char **new_items = realloc(g_magic_globals.items,
                                   new_cap * sizeof(char *));
        if (!new_items) {
            return error(ERR_INTERNAL,
                         "semantic: failed to grow magic globals array");
        }
        g_magic_globals.items    = new_items;
        g_magic_globals.capacity = new_cap;
    }

    /* copy name */
    size_t len = strlen(name);
    char *copy = malloc(len + 1);
    if (!copy) {
        return error(ERR_INTERNAL,
                     "semantic: failed to allocate magic global name");
    }
    memcpy(copy, name, len + 1);

    g_magic_globals.items[g_magic_globals.count++] = copy;
    return SUCCESS;
}

/* =========================================================================
 *                  Small numeric helper (no snprintf)
 * ========================================================================= */

static void sem_uint_to_dec(char *buffer, size_t buffer_size, unsigned int value) {
    if (!buffer || buffer_size == 0) {
        return;
    }

    char temp[16];
    size_t pos = sizeof(temp) - 1;
    temp[pos] = '\0';

    do {
        if (pos == 0) {
            break;
        }
        temp[--pos] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0);

    const char *digits = &temp[pos];
    size_t len = strlen(digits);
    if (len >= buffer_size) {
        len = buffer_size - 1;
    }

    memcpy(buffer, digits, len);
    buffer[len] = '\0';
}

/* =========================================================================
 *                  Scope-ID stack helpers (textual scope paths)
 * ========================================================================= */

static void sem_scope_ids_init(sem_scope_id_stack *scope_id_stack) {
    if (!scope_id_stack) {
        return;
    }
    scope_id_stack->depth = -1;
}

/**
 * @brief Enter root scope: creates scope "1".
 */
static void sem_scope_ids_enter_root(sem_scope_id_stack *scope_id_stack) {
    if (!scope_id_stack) {
        return;
    }
    scope_id_stack->depth = 0;
    scope_id_stack->frames[0].child_count = 0;
    scope_id_stack->frames[0].path[0] = '1';
    scope_id_stack->frames[0].path[1] = '\0';
}

/**
 * @brief Enter a child scope: creates "P.N" where P is parent path and N is next child index.
 */
static void sem_scope_ids_enter_child(sem_scope_id_stack *scope_id_stack) {
    if (!scope_id_stack) {
        return;
    }

    // If there is no root yet, start with it.
    if (scope_id_stack->depth < 0) {
        sem_scope_ids_enter_root(scope_id_stack);
        return;
    }

    if (scope_id_stack->depth + 1 >= SEM_MAX_SCOPE_DEPTH) {
        // Too deep – should not happen.
        return;
    }

    int parent_index = scope_id_stack->depth;
    int new_depth = parent_index + 1;

    sem_scope_id_frame *parent_frame = &scope_id_stack->frames[parent_index];
    sem_scope_id_frame *current_frame = &scope_id_stack->frames[new_depth];

    int child_index = ++parent_frame->child_count;
    current_frame->child_count = 0;

    const char *parent_path = parent_frame->path;
    size_t parent_length = strlen(parent_path);
    if (parent_length >= SEM_MAX_SCOPE_PATH) {
        parent_length = SEM_MAX_SCOPE_PATH - 1;
    }

    size_t pos = 0;
    if (parent_length > 0) {
        memcpy(current_frame->path, parent_path, parent_length);
        pos = parent_length;
    }

    if (pos + 1 < SEM_MAX_SCOPE_PATH) {
        current_frame->path[pos++] = '.';
        current_frame->path[pos] = '\0';

        char index_buffer[16];
        sem_uint_to_dec(index_buffer, sizeof index_buffer, (unsigned int)child_index);
        size_t index_length = strlen(index_buffer);
        size_t remaining = SEM_MAX_SCOPE_PATH - 1 - pos;
        if (index_length > remaining) {
            index_length = remaining;
        }
        if (index_length > 0) {
            memcpy(current_frame->path + pos, index_buffer, index_length);
            pos += index_length;
        }
    }

    current_frame->path[pos] = '\0';
    scope_id_stack->depth = new_depth;
}

/**
 * @brief Leave current scope-ID frame (go one level up).
 */
static void sem_scope_ids_leave(sem_scope_id_stack *scope_id_stack) {
    if (!scope_id_stack) {
        return;
    }
    if (scope_id_stack->depth >= 0) {
        scope_id_stack->depth--;
    }
}

/**
 * @brief Get textual ID of current scope ("1", "1.1", ...), or "global" if none.
 */
static const char *sem_scope_ids_current(const sem_scope_id_stack *scope_id_stack) {
    if (!scope_id_stack || scope_id_stack->depth < 0) {
        return "global";
    }
    return scope_id_stack->frames[scope_id_stack->depth].path;
}

/* =========================================================================
 *                  Global symbol table helpers (symtab)
 * ========================================================================= */

/**
 * @brief Internal helper: insert a symbol with a prepared key into the global symtab.
 *
 * This fills:
 *  - symbol_type
 *  - param_count
 *  - defined/global flags
 *  - ID (identifier_name)
 *  - scope_name (scope_string or "global")
 */
static int symtab_insert_with_key(semantic *semantic_table,
                                  const char *symbol_key,
                                  symbol_type symbol_kind,
                                  const char *identifier_name,
                                  const char *scope_string,
                                  int arity) {
    if (!semantic_table || !semantic_table->symtab || !symbol_key) {
        return SUCCESS;
    }

    if (st_find(semantic_table->symtab, (char *)symbol_key)) {
        return SUCCESS;
    }

    st_insert(semantic_table->symtab, (char *)symbol_key, symbol_kind, true);
    st_data *symbol_data = st_get(semantic_table->symtab, (char *)symbol_key);
    if (!symbol_data) {
        return error(ERR_INTERNAL, "symtab: st_get failed for key '%s'", symbol_key);
    }

    symbol_data->symbol_type = symbol_kind;
    symbol_data->param_count = arity;
    symbol_data->defined = true;
    symbol_data->global = false;

    if (identifier_name) {
        symbol_data->ID = string_create(0);
        if (!symbol_data->ID || !string_append_literal(symbol_data->ID, (char *)identifier_name)) {
            return error(ERR_INTERNAL, "symtab: failed to store ID '%s'", identifier_name);
        }
    } else {
        symbol_data->ID = NULL;
    }

    const char *scope_text = scope_string ? scope_string : "global";
    symbol_data->scope_name = string_create(0);
    if (!symbol_data->scope_name || !string_append_literal(symbol_data->scope_name, (char *)scope_text)) {
        return error(ERR_INTERNAL, "symtab: failed to store scope_name '%s'", scope_text);
    }

    return SUCCESS;
}

/**
 * @brief Insert a symbol into the global symtab with current textual scope.
 *
 * symtab key format: "<scope>::<name>"
 *  - scope: sem_scope_ids_current(), e.g. "1.2.3" or "global"
 *  - name : identifier name (function/var/param/accessor base)
 */
static int symtab_insert_symbol(semantic *semantic_table,
                                symbol_type symbol_kind,
                                const char *identifier_name,
                                int arity) {
    if (!semantic_table || !semantic_table->symtab || !identifier_name) {
        return SUCCESS;
    }

    const char *current_scope = sem_scope_ids_current(&semantic_table->ids);
    const char *scope_text = current_scope ? current_scope : "global";
    const char *name_text  = identifier_name ? identifier_name : "(null)";

    char symbol_key[SEM_MAX_SCOPE_PATH + 128];
    size_t max_total = sizeof(symbol_key) - 1;
    size_t pos = 0;

    size_t scope_len = strlen(scope_text);
    if (scope_len > max_total) {
        scope_len = max_total;
    }
    memcpy(symbol_key + pos, scope_len ? scope_text : "", scope_len);
    pos += scope_len;

    if (pos < max_total) {
        symbol_key[pos++] = ':';
    }
    if (pos < max_total) {
        symbol_key[pos++] = ':';
    }

    size_t name_len = strlen(name_text);
    size_t remaining = max_total - pos;
    if (name_len > remaining) {
        name_len = remaining;
    }
    memcpy(symbol_key + pos, name_len ? name_text : "", name_len);
    pos += name_len;

    symbol_key[pos] = '\0';

    return symtab_insert_with_key(semantic_table,
                                  symbol_key,
                                  symbol_kind,
                                  identifier_name,
                                  scope_text,
                                  arity);
}

/**
 * @brief Insert accessor symbol (getter/setter) into the global symtab.
 *
 * Symtab key: "<scope>::<base>@get" or "<scope>::<base>@set"
 *  - scope: e.g. "1" (class Program)
 *  - base : e.g. "value"
 *  - ID   : base (what is printed as the name)
 */
static int symtab_insert_accessor_symbol(semantic *semantic_table,
                                         bool is_setter,
                                         const char *base_name,
                                         int arity) {
    if (!semantic_table || !semantic_table->symtab || !base_name) {
        return SUCCESS;
    }

    const char *current_scope = sem_scope_ids_current(&semantic_table->ids);
    const char *scope_text    = current_scope ? current_scope : "global";
    const char *suffix        = is_setter ? "set" : "get";

    char symbol_key[SEM_MAX_SCOPE_PATH + 128];
    size_t max_total = sizeof(symbol_key) - 1;
    size_t pos = 0;

    size_t scope_len = strlen(scope_text);
    if (scope_len > max_total) {
        scope_len = max_total;
    }
    memcpy(symbol_key + pos, scope_len ? scope_text : "", scope_len);
    pos += scope_len;

    if (pos < max_total) {
        symbol_key[pos++] = ':';
    }
    if (pos < max_total) {
        symbol_key[pos++] = ':';
    }

    size_t base_len = strlen(base_name);
    size_t remaining = max_total - pos;
    if (base_len > remaining) {
        base_len = remaining;
    }
    memcpy(symbol_key + pos, base_len ? base_name : "", base_len);
    pos += base_len;

    if (pos < max_total) {
        symbol_key[pos++] = '@';
        remaining = max_total - pos;

        size_t suffix_len = strlen(suffix);
        if (suffix_len > remaining) {
            suffix_len = remaining;
        }
        memcpy(symbol_key + pos, suffix_len ? suffix : "", suffix_len);
        pos += suffix_len;
    }

    symbol_key[pos] = '\0';

    symbol_type accessor_symbol_type = is_setter ? ST_SETTER : ST_GETTER;
    return symtab_insert_with_key(semantic_table,
                                  symbol_key,
                                  accessor_symbol_type,
                                  base_name,
                                  scope_text,
                                  arity);
}

/* =========================================================================
 *                             Small helpers
 * ========================================================================= */

/**
 * @brief Compose a function signature key as "name#arity".
 */
static void make_function_key(char *buffer,
                              size_t buffer_size,
                              const char *function_name,
                              int arity) {
    if (!buffer || buffer_size == 0) {
        return;
    }

    const char *name_text = function_name ? function_name : "(null)";
    size_t max_total = buffer_size - 1;
    size_t pos = 0;

    size_t name_len = strlen(name_text);
    if (name_len > max_total) {
        name_len = max_total;
    }
    memcpy(buffer + pos, name_len ? name_text : "", name_len);
    pos += name_len;

    if (pos < max_total) {
        buffer[pos++] = '#';
        char number_buffer[32];
        sem_uint_to_dec(number_buffer, sizeof number_buffer, (unsigned int)arity);
        size_t num_len   = strlen(number_buffer);
        size_t remaining = max_total - pos;
        if (num_len > remaining) {
            num_len = remaining;
        }
        memcpy(buffer + pos, num_len ? number_buffer : "", num_len);
        pos += num_len;
    }

    buffer[pos] = '\0';
}

/**
 * @brief Compose an "any overload" key as "@name".
 *
 * Used to detect that some signature for a given function name exists,
 * without scanning the whole table.
 */
static void make_function_any_key(char *buffer,
                                  size_t buffer_size,
                                  const char *function_name) {
    if (!buffer || buffer_size == 0) {
        return;
    }

    const char *name_text = function_name ? function_name : "(null)";
    size_t max_total = buffer_size - 1;
    size_t pos = 0;

    if (pos < max_total) {
        buffer[pos++] = '@';
    }

    size_t name_len = strlen(name_text);
    size_t remaining = max_total - pos;
    if (name_len > remaining) {
        name_len = remaining;
    }
    if (name_len > 0) {
        memcpy(buffer + pos, name_text, name_len);
        pos += name_len;
    }

    buffer[pos] = '\0';
}

/**
 * @brief Compose an accessor key as "get:base" or "set:base".
 */
static void make_accessor_key(char *buffer,
                              size_t buffer_size,
                              const char *base_name,
                              bool is_setter) {
    if (!buffer || buffer_size == 0) {
        return;
    }

    const char *prefix    = is_setter ? "set" : "get";
    const char *name_text = base_name ? base_name : "(null)";

    size_t max_total = buffer_size - 1;
    size_t pos = 0;

    size_t prefix_len = strlen(prefix);
    if (prefix_len > max_total) {
        prefix_len = max_total;
    }
    memcpy(buffer + pos, prefix_len ? prefix : "", prefix_len);
    pos += prefix_len;

    if (pos < max_total) {
        buffer[pos++] = ':';
    }

    size_t name_len = strlen(name_text);
    size_t remaining = max_total - pos;
    if (name_len > remaining) {
        name_len = remaining;
    }
    memcpy(buffer + pos, name_len ? name_text : "", name_len);
    pos += name_len;

    buffer[pos] = '\0';
}

/**
 * @brief Count parameters in a singly-linked list of ast_parameter.
 */
static int count_parameters(ast_parameter parameter_list) {
    int parameter_count = 0;
    while (parameter_list) {
        ++parameter_count;
        parameter_list = parameter_list->next;
    }
    return parameter_count;
}

/**
 * @brief Extract the "name" of a parameter from ast_parameter.
 *
 * For your AST, parameter names are stored as:
 *  - value_type == AST_VALUE_IDENTIFIER
 *  - value.string_value == identifier text.
 */
static const char *sem_get_parameter_name(ast_parameter parameter) {
    if (!parameter) {
        return NULL;
    }

    if (parameter->value_type == AST_VALUE_IDENTIFIER ||
        parameter->value_type == AST_VALUE_STRING) {
        return parameter->value.string_value;
    }

    return NULL;
}

/**
 * @brief Obtain the root block of a class (walk up via parent).
 */
static ast_block get_class_root_block(ast_class class_node) {
    if (!class_node) {
        return NULL;
    }
    ast_block current_block = class_node->current;
    while (current_block && current_block->parent) {
        current_block = current_block->parent;
    }
    return current_block;
}

/**
 * @brief Return true if identifier is a special global of the form "__name".
 */
static bool is_magic_global_identifier(const char *identifier_name) {
    return identifier_name &&
           identifier_name[0] == '_' &&
           identifier_name[1] == '_';
}

/**
 * @brief True if there exists an accessor (getter/setter) with given base name.
 */
static bool sem_has_accessor(semantic *semantic_table,
                             const char *base_name,
                             bool is_setter) {
    if (!semantic_table || !semantic_table->funcs || !base_name) {
        return false;
    }

    char key[256];
    make_accessor_key(key, sizeof key, base_name, is_setter);
    return st_find(semantic_table->funcs, (char *)key) != NULL;
}

/**
 * @brief Check LHS of assignment: locals, magic globals and setters are allowed.
 *
 *  - "__foo" is always OK (global variable semantics) and recorded for codegen.
 *  - existing local (var/param) → OK.
 *  - existing setter for given base name → OK.
 *  - otherwise → ERR_DEF (3).
 */
static int sem_check_assignment_lhs(semantic *semantic_table,
                                    const char *name) {
    if (!semantic_table || !name) {
        return SUCCESS;
    }

    // magic globals are always allowed – and we record them for codegen
    if (is_magic_global_identifier(name)) {
        int rc = sem_magic_globals_add(name);
        if (rc != SUCCESS) {
            return rc;
        }
        return SUCCESS;
    }

    // existing local (var/param) in some scope?
    if (scopes_lookup(&semantic_table->scopes, name)) {
        return SUCCESS;
    }

    // setter – LHS "value =" etc.
    if (sem_has_accessor(semantic_table, name, true)) {
        return SUCCESS;
    }

    // unknown identifier on LHS → definition error
    return error(ERR_DEF, "assignment to undefined local variable '%s'", name);
}

/* =========================================================================
 *                        Function-table ops + main()
 * ========================================================================= */

// forward decls for header collection with scope_name propagation
static int collect_headers_from_block(semantic *semantic_table,
                                      ast_block block_node,
                                      const char *class_scope_name);

/**
 * @brief Insert a user function signature (name, arity) into the global table.
 *
 *        - Duplicity se hlídá v rámci jedné třídy:
 *          * stejný (name, arity) + stejná class_scope_name → ERR_REDEF (4)
 *          * stejný (name, arity) v jiné třídě → povoleno (sdílí se záznam)
 *
 *        - Stores scope_name and ID into st_data.
 *        - V Pass-1 to NEvkládá funkci do semantic_table->symtab – to dělá bodies walk (AST_FUNCTION).
 */
static int function_table_insert_signature(semantic *semantic_table,
                                           const char *function_name,
                                           int arity,
                                           const char *class_scope_name) {
    char function_key[256];
    make_function_key(function_key, sizeof function_key, function_name, arity);

    /* Už existuje nějaká funkce se stejným jménem a aritou? */
    st_data *existing = st_get(semantic_table->funcs, (char *)function_key);
    if (existing) {
        const char *existing_scope = NULL;
        if (existing->scope_name &&
            existing->scope_name->data &&
            existing->scope_name->length > 0) {
            existing_scope = existing->scope_name->data;
        }

        const char *new_scope = class_scope_name;

        /* Stejné (name, arity) ve stejné třídě → chyba 4 */
        if (existing_scope && new_scope &&
            strcmp(existing_scope, new_scope) == 0) {
            return error(ERR_REDEF,
                         "duplicate function signature %s in class '%s'",
                         function_key,
                         existing_scope);
        }

        /* Stejné (name, arity) v jiné třídě → povoleno, jen logneme. */
        fprintf(stdout,
                "[sem] function signature %s already exists in class '%s', "
                "new class '%s' allowed (per-class overloading).\n",
                function_key,
                existing_scope ? existing_scope : "(none)",
                new_scope ? new_scope : "(none)");
        return SUCCESS;
    }

    fprintf(stdout,
            "[sem] insert function signature: %s (class=%s)\n",
            function_key,
            class_scope_name ? class_scope_name : "(null)");

    st_insert(semantic_table->funcs, (char *)function_key, ST_FUN, true);
    st_data *function_data = st_get(semantic_table->funcs, (char *)function_key);
    if (!function_data) {
        return error(ERR_INTERNAL,
                     "failed to store function signature: %s",
                     function_key);
    }

    function_data->symbol_type = ST_FUN;
    function_data->param_count = arity;
    function_data->defined     = false;
    function_data->global      = true;

    // store scope_name (class name)
    if (class_scope_name) {
        function_data->scope_name = string_create(0);
        if (!function_data->scope_name ||
            !string_append_literal(function_data->scope_name,
                                   (char *)class_scope_name)) {
            return error(ERR_INTERNAL,
                         "failed to store function scope_name for '%s'",
                         function_name ? function_name : "(null)");
        }
    } else {
        function_data->scope_name = NULL;
    }

    // store ID = function name (without class)
    if (function_name) {
        function_data->ID = string_create(0);
        if (!function_data->ID ||
            !string_append_literal(function_data->ID,
                                   (char *)function_name)) {
            return error(ERR_INTERNAL,
                         "failed to store function name (ID) for '%s'",
                         function_name);
        }
    } else {
        function_data->ID = NULL;
    }

    // For user-defined functions, also insert a sentinel "@name" to mark
    // that some overload for this base name exists (used for ERR_ARGNUM).
    if (function_name && !builtins_is_builtin_qname(function_name)) {
        char any_key[256];
        make_function_any_key(any_key, sizeof any_key, function_name);

        if (!st_find(semantic_table->funcs, (char *)any_key)) {
            st_insert(semantic_table->funcs, (char *)any_key, ST_FUN, true);
            st_data *any_data = st_get(semantic_table->funcs, (char *)any_key);
            if (any_data) {
                any_data->symbol_type = ST_FUN;
                any_data->param_count = 0;
                any_data->defined     = false;
                any_data->global      = true;
                any_data->ID          = NULL;
                any_data->scope_name  = NULL;
            }
        }
    }

    return SUCCESS;
}

/**
 * @brief Insert getter/setter signature.
 *
 *        - V rámci jedné třídy smí být max. jeden getter a jeden setter pro dané base jméno:
 *          * druhý getter/setter ve stejné třídě → ERR_REDEF (4)
 *          * getter/setter se stejným base v jiné třídě → povoleno (sdílí záznam)
 *
 *        - Ukládá scope_name a ID (ID == base).
 *        - NEvkládá do semantic_table->symtab (jen do funcs).
 */
static int function_table_insert_accessor(semantic *semantic_table,
                                          const char *base_name,
                                          bool is_setter,
                                          const char *class_scope_name,
                                          const char *setter_param_opt) {
    (void)setter_param_opt; // not used in Pass-1

    char accessor_key[256];
    make_accessor_key(accessor_key, sizeof accessor_key, base_name, is_setter);

    /* Zkusit najít existující accessor se stejným base jménem. */
    st_data *existing = st_get(semantic_table->funcs, (char *)accessor_key);
    if (existing) {
        const char *existing_scope = NULL;
        if (existing->scope_name &&
            existing->scope_name->data &&
            existing->scope_name->length > 0) {
            existing_scope = existing->scope_name->data;
        }

        const char *new_scope = class_scope_name;

        /* Stejný getter/setter ve stejné třídě → chyba 4. */
        if (existing_scope && new_scope &&
            strcmp(existing_scope, new_scope) == 0) {
            return error(ERR_REDEF,
                         is_setter
                             ? "duplicate setter for '%s' in class '%s'"
                             : "duplicate getter for '%s' in class '%s'",
                         base_name ? base_name : "(null)",
                         existing_scope);
        }

        /* Jiná třída → povoleno, jen logneme. */
        fprintf(stdout,
                "[sem] accessor %s for base '%s' already exists in class '%s', "
                "new class '%s' allowed.\n",
                is_setter ? "setter" : "getter",
                base_name ? base_name : "(null)",
                existing_scope ? existing_scope : "(none)",
                new_scope ? new_scope : "(none)");
        return SUCCESS;
    }

    fprintf(stdout,
            "[sem] insert %s for '%s' as %s (class=%s)\n",
            is_setter ? "setter" : "getter",
            base_name ? base_name : "(null)",
            accessor_key,
            class_scope_name ? class_scope_name : "(null)");

    st_insert(semantic_table->funcs, (char *)accessor_key, ST_FUN, true);
    st_data *accessor_data = st_get(semantic_table->funcs, (char *)accessor_key);
    if (!accessor_data) {
        return error(ERR_INTERNAL,
                     "failed to store accessor signature: %s",
                     accessor_key);
    }

    accessor_data->symbol_type = ST_FUN;
    accessor_data->param_count = is_setter ? 1 : 0;
    accessor_data->defined     = false;
    accessor_data->global      = true;

    if (class_scope_name) {
        accessor_data->scope_name = string_create(0);
        if (!accessor_data->scope_name ||
            !string_append_literal(accessor_data->scope_name,
                                   (char *)class_scope_name)) {
            return error(ERR_INTERNAL,
                         "failed to store accessor scope_name for '%s'",
                         base_name ? base_name : "(null)");
        }
    } else {
        accessor_data->scope_name = NULL;
    }

    if (base_name) {
        accessor_data->ID = string_create(0);
        if (!accessor_data->ID ||
            !string_append_literal(accessor_data->ID,
                                   (char *)base_name)) {
            return error(ERR_INTERNAL,
                         "failed to store accessor base (ID) for '%s'",
                         base_name);
        }
    } else {
        accessor_data->ID = NULL;
    }

    return SUCCESS;
}

/**
 * @brief If the function is main(), verify arity==0 and remember presence.
 */
static int check_and_mark_main_function(semantic *semantic_table,
                                        const char *function_name,
                                        int arity) {
    if (!function_name || strcmp(function_name, "main") != 0) {
        return SUCCESS;
    }

    fprintf(stdout, "[sem] encountered main() with arity=%d\n", arity);
    if (arity != 0) {
        return error(ERR_DEF, "main() must have 0 parameters");
    }
    semantic_table->seen_main = true;
    return SUCCESS;
}

/* =========================================================================
 *                      Calls & literal-only expr checks
 * ========================================================================= */

/**
 * @brief True if a signature (name,arity) exists in the global function table.
 */
static bool function_table_has_signature(semantic *semantic_table,
                                         const char *function_name,
                                         int arity) {
    if (!semantic_table || !semantic_table->funcs || !function_name) {
        return false;
    }

    char function_key[256];
    make_function_key(function_key, sizeof function_key, function_name, arity);
    return st_find(semantic_table->funcs, (char *)function_key) != NULL;
}

/**
 * @brief True if there exists at least one header for `function_name` (any arity).
 *        Implemented via sentinel "@name" inserted when headers are recorded.
 */
static bool function_table_has_any_overload(semantic *semantic_table,
                                            const char *function_name) {
    if (!semantic_table || !semantic_table->funcs || !function_name) {
        return false;
    }

    char any_key[256];
    make_function_any_key(any_key, sizeof any_key, function_name);
    return st_find(semantic_table->funcs, (char *)any_key) != NULL;
}

/**
 * @brief Arity checks for a user function call (built-ins řešíme zvlášť).
 *
 *        - User: if exact header exists, OK; if other arities exist -> ERR_ARGNUM;
 *                otherwise defer to Pass 2.
 */
static int check_function_call_arity(semantic *semantic_table,
                                     const char *function_name,
                                     int arity) {
    fprintf(stdout, "[sem] call: %s(arity=%d)\n",
            function_name ? function_name : "(null)", arity);
    if (!function_name) {
        return SUCCESS;
    }

    // user function
    if (function_table_has_signature(semantic_table, function_name, arity)) {
        // exact header exists -> OK
        return SUCCESS;
    }

    // some overload for this name exists, but not with this arity
    if (function_table_has_any_overload(semantic_table, function_name)) {
        return error(ERR_ARGNUM,
                     "wrong number of arguments for %s (arity=%d)",
                     function_name, arity);
    }

    // header not known yet: defer to Pass 2
    return SUCCESS;
}

typedef enum {
    LITERAL_UNKNOWN = 0,
    LITERAL_NUMERIC,
    LITERAL_STRING
} literal_kind;

/**
 * @brief True if expression is exactly an integer literal.
 */
static bool expression_is_integer_literal(ast_expression expression_node) {
    return expression_node &&
           expression_node->type == AST_VALUE &&
           expression_node->operands.identity.value_type == AST_VALUE_INT;
}

/**
 * @brief Coarse literal-kind of a value expression: number/string/unknown.
 *
 * NOTE: We assume that AST_VALUE_STRING is used for string literals in expressions.
 * If identifiers were ever encoded as AST_VALUE_STRING, this function would have to
 * be adjusted to avoid false ERR_EXPR reports (e.g. for (b < 2)).
 */
static literal_kind get_literal_kind_of_value_expression(ast_expression expression_node) {
    if (!expression_node || expression_node->type != AST_VALUE) {
        return LITERAL_UNKNOWN;
    }
    switch (expression_node->operands.identity.value_type) {
        case AST_VALUE_INT:
        case AST_VALUE_FLOAT:
            return LITERAL_NUMERIC;
        case AST_VALUE_STRING:
            return LITERAL_STRING;
        default:
            return LITERAL_UNKNOWN;
    }
}

/**
 * @brief Recursively compute literal-kind for whole expression subtree.
 *
 *  - LITERAL_NUMERIC ... expression composed only of numeric literals and
 *                        numeric-only operators (+,-,*,/,..., no identifiers)
 *  - LITERAL_STRING  ... expression composed only of string literals and
 *                        string-safe operators (string+string, string*int literal, concat)
 *  - LITERAL_UNKNOWN ... anything involving identifiers, calls, or mixed/unsupported types
 */
static literal_kind get_expression_literal_kind(ast_expression expression_node) {
    if (!expression_node) {
        return LITERAL_UNKNOWN;
    }

    switch (expression_node->type) {
        case AST_VALUE:
            return get_literal_kind_of_value_expression(expression_node);

        case AST_ADD: {
            literal_kind left_kind =
                get_expression_literal_kind(
                    expression_node->operands.binary_op.left);
            literal_kind right_kind =
                get_expression_literal_kind(
                    expression_node->operands.binary_op.right);

            if (!left_kind || !right_kind) {
                return LITERAL_UNKNOWN;
            }

            /* Numeric addition → numeric */
            if (left_kind == LITERAL_NUMERIC &&
                right_kind == LITERAL_NUMERIC) {
                return LITERAL_NUMERIC;
            }

            /* String concatenation using '+' → string */
            if (left_kind == LITERAL_STRING &&
                right_kind == LITERAL_STRING) {
                return LITERAL_STRING;
            }

            return LITERAL_UNKNOWN;
        }

        case AST_SUB:
        case AST_DIV: {
            literal_kind left_kind =
                get_expression_literal_kind(
                    expression_node->operands.binary_op.left);
            literal_kind right_kind =
                get_expression_literal_kind(
                    expression_node->operands.binary_op.right);

            if (left_kind == LITERAL_NUMERIC &&
                right_kind == LITERAL_NUMERIC) {
                return LITERAL_NUMERIC;
            }
            return LITERAL_UNKNOWN;
        }

        case AST_MUL: {
            literal_kind left_kind =
                get_expression_literal_kind(
                    expression_node->operands.binary_op.left);
            literal_kind right_kind =
                get_expression_literal_kind(
                    expression_node->operands.binary_op.right);

            /* Numeric multiply → numeric */
            if (left_kind == LITERAL_NUMERIC &&
                right_kind == LITERAL_NUMERIC) {
                return LITERAL_NUMERIC;
            }

            /* String * integer literal → string (repetition) */
            if (left_kind == LITERAL_STRING &&
                expression_is_integer_literal(
                    expression_node->operands.binary_op.right)) {
                return LITERAL_STRING;
            }

            return LITERAL_UNKNOWN;
        }

        case AST_CONCAT: {
            literal_kind left_kind =
                get_expression_literal_kind(
                    expression_node->operands.binary_op.left);
            literal_kind right_kind =
                get_expression_literal_kind(
                    expression_node->operands.binary_op.right);

            if (left_kind == LITERAL_STRING &&
                right_kind == LITERAL_STRING) {
                return LITERAL_STRING;
            }
            return LITERAL_UNKNOWN;
        }

        /* Relational / logical / ternary / IS – result is bool,
         * we do not propagate a numeric/string literal-kind here.
         * Type collisions across these are still checked in sem_check_literal_binary().
         */
        case AST_EQUALS:
        case AST_NOT_EQUAL:
        case AST_LT:
        case AST_LE:
        case AST_GT:
        case AST_GE:
        case AST_AND:
        case AST_OR:
        case AST_TERNARY:
        case AST_IS:
            return LITERAL_UNKNOWN;

        default:
            /* IDENTIFIER, FUNCTION_CALL, and anything else that isn't
             * pure literal-only arithmetic/concat.
             */
            return LITERAL_UNKNOWN;
    }
}

static int visit_expression_node(semantic *semantic_table,
                                 ast_expression expression_node);

/**
 * @brief Literal-only policy check for a binary-like expression.
 */
static int sem_check_literal_binary(int op,
                                    literal_kind left_kind,
                                    literal_kind right_kind,
                                    ast_expression right_expression) {
    (void)right_expression;

    if (op == AST_ADD) {
        if (left_kind && right_kind) {
            bool ok = (left_kind == LITERAL_NUMERIC &&
                       right_kind == LITERAL_NUMERIC) ||
                      (left_kind == LITERAL_STRING &&
                       right_kind == LITERAL_STRING);
            if (!ok) {
                return error(ERR_EXPR, "invalid literal '+' operands");
            }
        }
    } else if (op == AST_SUB || op == AST_DIV) {
        if (left_kind && right_kind) {
            if (!(left_kind == LITERAL_NUMERIC &&
                  right_kind == LITERAL_NUMERIC)) {
                return error(ERR_EXPR,
                             "invalid literal arithmetic operands");
            }
        }
    } else if (op == AST_MUL) {
        if (left_kind && right_kind) {
            bool ok = (left_kind == LITERAL_NUMERIC &&
                       right_kind == LITERAL_NUMERIC) ||
                      (left_kind == LITERAL_STRING &&
                       expression_is_integer_literal(right_expression));
            if (!ok) {
                return error(ERR_EXPR, "invalid literal '*' operands");
            }
        }
    } else if (op == AST_LT || op == AST_LE ||
               op == AST_GT || op == AST_GE) {
        if (left_kind && right_kind) {
            if (!(left_kind == LITERAL_NUMERIC &&
                  right_kind == LITERAL_NUMERIC)) {
                return error(ERR_EXPR,
                             "relational operators require numeric literals");
            }
        }
    } else if (op == AST_IS) {
        // If RHS was an explicit type name, checks would go here.
    }

    return SUCCESS;
}

/* -------------------------------------------------------------------------
 *                Built-in function calls (Ifj.*) – Pass 1 arity+types
 * ------------------------------------------------------------------------- */

typedef enum {
    PARAM_KIND_UNKNOWN = 0,
    PARAM_KIND_STRING_LITERAL,
    PARAM_KIND_NUMERIC_LITERAL
} param_kind;

/**
 * @brief Základní klasifikace argumentu built-inu podle literálu.
 *
 *  - string literal -> PARAM_KIND_STRING_LITERAL
 *  - int/float literal -> PARAM_KIND_NUMERIC_LITERAL
 *  - cokoliv jiného (identifier, null, složitější výraz) -> PARAM_KIND_UNKNOWN
 */
static param_kind sem_get_param_literal_kind(ast_parameter param) {
    if (!param) {
        return PARAM_KIND_UNKNOWN;
    }

    if ((param->value_type == AST_VALUE_STRING ||
         param->value_type == AST_VALUE_IDENTIFIER) &&
        param->value.string_value &&
        param->value.string_value[0] == '_' &&
        param->value.string_value[1] == '_') {
        return PARAM_KIND_UNKNOWN;
    }

    switch (param->value_type) {
        case AST_VALUE_STRING:
            return PARAM_KIND_STRING_LITERAL;

        case AST_VALUE_INT:
        case AST_VALUE_FLOAT:
            return PARAM_KIND_NUMERIC_LITERAL;

        default:
            return PARAM_KIND_UNKNOWN;
    }
}


/**
 * @brief Basic arity + literal-type checks for selected IFJ built-ins (Ifj.*).
 *
 * Typové kontroly děláme jen tehdy, když je argument doslova literál (string/int/float).
 * Pokud je argument identifier nebo složitější výraz, necháme detailní typovou
 * kontrolu na Pass-2.
 *
 * Špatná arita NEBO staticky špatný typ => ERR_ARGNUM (5).
 *
 * Arita se čte z funkční tabulky (semantic_table->funcs), která je naplněná
 * funkcí builtins_install() z builtins.c.
 *
 * raw_name:
 *  - buď už plně kvalifikované "Ifj.floor",
 *  - nebo krátké "floor"/"length"/... z AST_IFJ_FUNCTION/AST_IFJ_FUNCTION_EXPR.
 */
static int sem_check_builtin_call(semantic *semantic_table,
                                  const char *raw_name,
                                  ast_parameter parameters) {
    if (!semantic_table || !raw_name) {
        return SUCCESS;
    }

    const char *name = raw_name;
    char qname_buffer[64];

    /* Normalize name to "Ifj.<name>" if not already qualified. */
    if (strncmp(raw_name, "Ifj.", 4) != 0) {
        size_t base_len = strlen(raw_name);
        if (base_len + 4 < sizeof qname_buffer) {
            memcpy(qname_buffer, "Ifj.", 4);
            memcpy(qname_buffer + 4, raw_name, base_len);
            qname_buffer[4 + base_len] = '\0';
            name = qname_buffer;
        }
    }

    int arg_count = count_parameters(parameters);

    /* 1) Aritní kontrola přes tabulku funkcí (naplněnou builtins_install). */
    if (!function_table_has_signature(semantic_table, name, arg_count)) {
        return error(ERR_ARGNUM,
                     "wrong number of arguments for builtin %s (arity=%d)",
                     name, arg_count);
    }

    /* 2) Volitelná kontrola typů literálů pro vybrané IFJ builtiny.
     *    (Pouze pokud je argument doslova literál – jinak necháme na Pass-2.)
     */
    ast_parameter p1 = parameters;
    ast_parameter p2 = p1 ? p1->next : NULL;
    ast_parameter p3 = p2 ? p2->next : NULL;

    /* floor(num) */
    if (strcmp(name, "Ifj.floor") == 0) {
        param_kind k1 = sem_get_param_literal_kind(p1);
        if (k1 != PARAM_KIND_UNKNOWN && k1 != PARAM_KIND_NUMERIC_LITERAL) {
            return error(ERR_ARGNUM,
                         "wrong literal type for builtin Ifj.floor");
        }
        return SUCCESS;
    }

    /* length(string) */
    if (strcmp(name, "Ifj.length") == 0) {
        param_kind k1 = sem_get_param_literal_kind(p1);
        if (k1 != PARAM_KIND_UNKNOWN && k1 != PARAM_KIND_STRING_LITERAL) {
            return error(ERR_ARGNUM,
                         "wrong literal type for builtin Ifj.length");
        }
        return SUCCESS;
    }

    /* substring(string, int, int) */
    if (strcmp(name, "Ifj.substring") == 0) {
        param_kind k1 = sem_get_param_literal_kind(p1);
        param_kind k2 = sem_get_param_literal_kind(p2);
        param_kind k3 = sem_get_param_literal_kind(p3);

        if (k1 != PARAM_KIND_UNKNOWN && k1 != PARAM_KIND_STRING_LITERAL) {
            return error(ERR_ARGNUM,
                         "wrong literal type for builtin Ifj.substring(arg1)");
        }
        if (k2 != PARAM_KIND_UNKNOWN && k2 != PARAM_KIND_NUMERIC_LITERAL) {
            return error(ERR_ARGNUM,
                         "wrong literal type for builtin Ifj.substring(arg2)");
        }
        if (k3 != PARAM_KIND_UNKNOWN && k3 != PARAM_KIND_NUMERIC_LITERAL) {
            return error(ERR_ARGNUM,
                         "wrong literal type for builtin Ifj.substring(arg3)");
        }
        return SUCCESS;
    }

    /* strcmp(string, string) */
    if (strcmp(name, "Ifj.strcmp") == 0) {
        param_kind k1 = sem_get_param_literal_kind(p1);
        param_kind k2 = sem_get_param_literal_kind(p2);

        if (k1 != PARAM_KIND_UNKNOWN && k1 != PARAM_KIND_STRING_LITERAL) {
            return error(ERR_ARGNUM,
                         "wrong literal type for builtin Ifj.strcmp(arg1)");
        }
        if (k2 != PARAM_KIND_UNKNOWN && k2 != PARAM_KIND_STRING_LITERAL) {
            return error(ERR_ARGNUM,
                         "wrong literal type for builtin Ifj.strcmp(arg2)");
        }
        return SUCCESS;
    }

    /* ord(string, int) */
    if (strcmp(name, "Ifj.ord") == 0) {
        param_kind k1 = sem_get_param_literal_kind(p1);
        param_kind k2 = sem_get_param_literal_kind(p2);

        if (k1 != PARAM_KIND_UNKNOWN && k1 != PARAM_KIND_STRING_LITERAL) {
            return error(ERR_ARGNUM,
                         "wrong literal type for builtin Ifj.ord(arg1)");
        }
        if (k2 != PARAM_KIND_UNKNOWN && k2 != PARAM_KIND_NUMERIC_LITERAL) {
            return error(ERR_ARGNUM,
                         "wrong literal type for builtin Ifj.ord(arg2)");
        }
        return SUCCESS;
    }

    /* chr(int) */
    if (strcmp(name, "Ifj.chr") == 0) {
        param_kind k1 = sem_get_param_literal_kind(p1);

        if (k1 != PARAM_KIND_UNKNOWN && k1 != PARAM_KIND_NUMERIC_LITERAL) {
            return error(ERR_ARGNUM,
                         "wrong literal type for builtin Ifj.chr");
        }
        return SUCCESS;
    }

    /* Ostatní Ifj.* builtiny – v Pass-1 jen arita přes tabulku,
     * typově (a existence) se dořeší v Pass-2.
     */
    return SUCCESS;
}

/**
 * @brief Visit a function-call expression node (AST_FUNCTION_CALL).
 */
static int sem_visit_call_expr(semantic *semantic_table,
                               ast_expression expression_node) {
    if (!expression_node || !expression_node->operands.function_call) {
        return SUCCESS;
    }

    ast_fun_call call_node = expression_node->operands.function_call;
    const char *called_name = call_node->name;

    if (builtins_is_builtin_qname(called_name)) {
        // Built-in – arita + literály řeší sem_check_builtin_call(), zbytek až Pass-2
        return sem_check_builtin_call(semantic_table,
                                      called_name,
                                      call_node->parameters);
    }

    int parameter_count = count_parameters(call_node->parameters);

    return check_function_call_arity(semantic_table,
                                     called_name,
                                     parameter_count);
}


/**
 * @brief Visit a binary-like expression node (including ternary/IS).
 */
static int sem_visit_binary_expr(semantic *semantic_table,
                                 ast_expression expression_node) {
    ast_expression left_expression =
        expression_node->operands.binary_op.left;
    ast_expression right_expression =
        expression_node->operands.binary_op.right;

    int result_code =
        visit_expression_node(semantic_table, left_expression);
    if (result_code != SUCCESS) {
        return result_code;
    }

    result_code =
        visit_expression_node(semantic_table, right_expression);
    if (result_code != SUCCESS) {
        return result_code;
    }

    // literal-only checks – rekurzivně přes celý podvýraz
    literal_kind left_kind =
        get_expression_literal_kind(left_expression);
    literal_kind right_kind =
        get_expression_literal_kind(right_expression);

    return sem_check_literal_binary((int)expression_node->type,
                                    left_kind,
                                    right_kind,
                                    right_expression);
}

/**
 * @brief Visit an expression (only Pass-1 checks we can do early).
 */
static int visit_expression_node(semantic *semantic_table,
                                 ast_expression expression_node) {
    if (!expression_node) {
        return SUCCESS;
    }

    switch (expression_node->type) {
        case AST_VALUE:
        case AST_NOT:
        case AST_NOT_NULL:
            return SUCCESS;

        case AST_IFJ_FUNCTION_EXPR: {
            ast_ifj_function ifj_call = expression_node->operands.ifj_function;
            if (!ifj_call || !ifj_call->name) {
                return SUCCESS;
            }

            fprintf(stdout,
                    "[sem] IFJ expr call: %s (scope=%s)\n",
                    ifj_call->name,
                    sem_scope_ids_current(&semantic_table->ids));

            return sem_check_builtin_call(semantic_table,
                                          ifj_call->name,
                                          ifj_call->parameters);
        }

        case AST_FUNCTION_CALL:
            return sem_visit_call_expr(semantic_table, expression_node);

        // binary-like shapes
        case AST_ADD:
        case AST_SUB:
        case AST_MUL:
        case AST_DIV:
        case AST_EQUALS:
        case AST_NOT_EQUAL:
        case AST_LT:
        case AST_LE:
        case AST_GT:
        case AST_GE:
        case AST_AND:
        case AST_OR:
        case AST_TERNARY:
        case AST_IS:
            return sem_visit_binary_expr(semantic_table, expression_node);

        default:
            return SUCCESS;
    }
}

/* =========================================================================
 *          Copy IFJ builtins from semantic_table->funcs into symtab (scope=global)
 * ========================================================================= */

// callback for st_foreach over semantic_table->funcs – inserts Ifj.* into symtab
static void sem_copy_builtin_cb(const char *key,
                                st_data *data,
                                void *user_data) {
    semantic *semantic_table = (semantic *)user_data;
    if (!semantic_table || !key || !data) {
        return;
    }

    // only builtins Ifj.*
    if (strncmp(key, "Ifj.", 4) != 0) {
        return;
    }

    // name = part before '#' (Ifj.read_num#0 -> Ifj.read_num)
    const char *hash_position = strchr(key, '#');
    size_t base_name_length =
        hash_position ? (size_t)(hash_position - key) : strlen(key);

    char builtin_name_buffer[128];
    if (base_name_length >= sizeof(builtin_name_buffer)) {
        base_name_length = sizeof(builtin_name_buffer) - 1;
    }
    memcpy(builtin_name_buffer,
           base_name_length ? key : "",
           base_name_length);
    builtin_name_buffer[base_name_length] = '\0';

    int arity = data->param_count;

    // ids.depth is -1 at this point => scope="global"
    (void)symtab_insert_symbol(semantic_table,
                               ST_FUN,
                               builtin_name_buffer,
                               arity);
}

// invokes callback over the whole function table
static void sem_copy_builtins_to_symbol_table(semantic *semantic_table) {
    if (!semantic_table || !semantic_table->funcs || !semantic_table->symtab) {
        return;
    }
    st_foreach(semantic_table->funcs, sem_copy_builtin_cb, semantic_table);
}

/* =========================================================================
 *                       Bodies walk (scopes & nodes) – Pass 1
 * ========================================================================= */

static int visit_block_node(semantic *semantic_table, ast_block block_node);
static int visit_statement_node(semantic *semantic_table, ast_node node);

// small helpers for entering/leaving a lexical scope bound to a block
static void sem_scope_enter_block(semantic *semantic_table) {
    if (semantic_table->ids.depth < 0) {
        sem_scope_ids_enter_root(&semantic_table->ids);
    } else {
        sem_scope_ids_enter_child(&semantic_table->ids);
    }
    scopes_push(&semantic_table->scopes);
}

static int sem_scope_leave_block(semantic *semantic_table,
                                 const char *context) {
    if (!scopes_pop(&semantic_table->scopes)) {
        sem_scope_ids_leave(&semantic_table->ids);
        return error(ERR_INTERNAL,
                     "scope stack underflow in %s",
                     context ? context : "unknown");
    }
    sem_scope_ids_leave(&semantic_table->ids);
    return SUCCESS;
}

/**
 * @brief Declare a function parameter list in the current scope.
 */
static int declare_parameter_list_in_current_scope(semantic *semantic_table,
                                                   ast_parameter parameter_list) {
    for (ast_parameter parameter = parameter_list;
         parameter;
         parameter = parameter->next) {

        const char *param_name = sem_get_parameter_name(parameter);

        fprintf(stdout,
                "[sem] param declare: %s (scope=%s)\n",
                param_name ? param_name : "(null)",
                sem_scope_ids_current(&semantic_table->ids));

        if (!param_name) {
            return error(ERR_INTERNAL,
                         "parameter without name in current scope");
        }

        if (!scopes_declare_local(&semantic_table->scopes,
                                  param_name,
                                  true)) {
            return error(ERR_REDEF,
                         "parameter '%s' redeclared in the same scope",
                         param_name ? param_name : "(null)");
        }

        st_data *parameter_data =
            scopes_lookup_in_current(&semantic_table->scopes,
                                     param_name);
        if (parameter_data) {
            parameter_data->symbol_type = ST_PAR;
        }

        int result_code =
            symtab_insert_symbol(semantic_table,
                                 ST_PAR,
                                 param_name,
                                 0);
        if (result_code != SUCCESS) {
            return result_code;
        }
    }
    return SUCCESS;
}


/**
 * @brief Visit a block: push scope, assign scope-ID, visit all nodes, pop.
 */
static int visit_block_node(semantic *semantic_table,
                            ast_block block_node) {
    if (!block_node) {
        return SUCCESS;
    }

    fprintf(stdout, "[sem] scope PUSH (blk=%p)\n", (void *)block_node);

    sem_scope_enter_block(semantic_table);

    for (ast_node node = block_node->first; node; node = node->next) {
        int result_code = visit_statement_node(semantic_table, node);
        if (result_code != SUCCESS) {
            (void)sem_scope_leave_block(semantic_table,
                                        "visit_block_node (early error)");
            fprintf(stdout,
                    "[sem] scope POP (early error, blk=%p)\n",
                    (void *)block_node);
            return result_code;
        }
    }

    int leave_result =
        sem_scope_leave_block(semantic_table, "visit_block_node");
    fprintf(stdout, "[sem] scope POP (blk=%p)\n", (void *)block_node);
    return leave_result;
}

/* ----------------- per-node handlers to keep visit_statement_node small ----------------- */

static int sem_handle_condition_node(semantic *semantic_table,
                                     ast_node node) {
    int result_code =
        visit_expression_node(semantic_table,
                              node->data.condition.condition);
    if (result_code != SUCCESS) {
        return result_code;
    }

    result_code =
        visit_block_node(semantic_table,
                         node->data.condition.if_branch);
    if (result_code != SUCCESS) {
        return result_code;
    }

    result_code =
        visit_block_node(semantic_table,
                         node->data.condition.else_branch);
    return result_code;
}

static int sem_handle_while_node(semantic *semantic_table,
                                 ast_node node) {
    int result_code =
        visit_expression_node(semantic_table,
                              node->data.while_loop.condition);
    if (result_code != SUCCESS) {
        return result_code;
    }

    semantic_table->loop_depth++;
    fprintf(stdout,
            "[sem] while enter (depth=%d, scope=%s)\n",
            semantic_table->loop_depth,
            sem_scope_ids_current(&semantic_table->ids));

    result_code =
        visit_block_node(semantic_table,
                         node->data.while_loop.body);

    fprintf(stdout,
            "[sem] while leave (depth=%d, scope=%s)\n",
            semantic_table->loop_depth,
            sem_scope_ids_current(&semantic_table->ids));
    semantic_table->loop_depth--;
    return result_code;
}

/**
 * @brief Handle function body – parameters and top-level statements share one scope.
 */
static int sem_handle_function_node(semantic *semantic_table,
                                    ast_node node) {
    ast_function function_node = node->data.function;
    const char *fname =
        function_node->name ? function_node->name : "(null)";

    fprintf(stdout,
            "[sem] function body: %s (scope=%s)\n",
            fname,
            sem_scope_ids_current(&semantic_table->ids));

    int result_code;
    int arity = count_parameters(function_node->parameters);

    // Register function symbol in global symtab under current scope (class).
    result_code =
        symtab_insert_symbol(semantic_table,
                             ST_FUN,
                             function_node->name,
                             arity);
    if (result_code != SUCCESS) {
        return result_code;
    }

    // Enter function frame scope – parameters and top-level body share this scope.
    fprintf(stdout, "[sem] scope PUSH (func=%s)\n", fname);
    sem_scope_enter_block(semantic_table);

    // Declare parameters in this function scope.
    result_code =
        declare_parameter_list_in_current_scope(semantic_table,
                                               function_node->parameters);
    if (result_code != SUCCESS) {
        (void)sem_scope_leave_block(semantic_table,
                                    "sem_handle_function_node (params)");
        fprintf(stdout,
                "[sem] scope POP (func=%s, early error)\n",
                fname);
        return result_code;
    }

    // Traverse function body *without* creating an extra block scope.
    if (function_node->code) {
        for (ast_node stmt = function_node->code->first;
             stmt;
             stmt = stmt->next) {
            result_code =
                visit_statement_node(semantic_table, stmt);
            if (result_code != SUCCESS) {
                (void)sem_scope_leave_block(
                    semantic_table,
                    "sem_handle_function_node (body)");
                fprintf(stdout,
                        "[sem] scope POP (func=%s, early error)\n",
                        fname);
                return result_code;
            }
        }
    }

    result_code =
        sem_scope_leave_block(semantic_table,
                              "sem_handle_function_node");
    fprintf(stdout, "[sem] scope POP (func=%s)\n", fname);
    return result_code;
}

/**
 * @brief Handle getter body – all statements share a single scope.
 */
static int sem_handle_getter_node(semantic *semantic_table,
                                  ast_node node) {
    const char *name  = node->data.getter.name;
    const char *gname = name ? name : "(null)";

    fprintf(stdout,
            "[sem] getter body: %s (scope=%s)\n",
            gname,
            sem_scope_ids_current(&semantic_table->ids));

    int result_code =
        symtab_insert_accessor_symbol(semantic_table,
                                      false,
                                      node->data.getter.name,
                                      0);
    if (result_code != SUCCESS) {
        return result_code;
    }

    fprintf(stdout, "[sem] scope PUSH (getter=%s)\n", gname);
    sem_scope_enter_block(semantic_table);

    // Traverse body without another block-level scope for the outer block node.
    if (node->data.getter.body) {
        for (ast_node stmt = node->data.getter.body->first;
             stmt;
             stmt = stmt->next) {
            result_code =
                visit_statement_node(semantic_table, stmt);
            if (result_code != SUCCESS) {
                (void)sem_scope_leave_block(
                    semantic_table,
                    "sem_handle_getter_node (body)");
                fprintf(stdout,
                        "[sem] scope POP (getter=%s, early error)\n",
                        gname);
                return result_code;
            }
        }
    }

    result_code =
        sem_scope_leave_block(semantic_table,
                              "sem_handle_getter_node");
    fprintf(stdout, "[sem] scope POP (getter=%s)\n", gname);
    return result_code;
}

/**
 * @brief Handle setter body – parameter and top-level statements share one scope.
 */
static int sem_handle_setter_node(semantic *semantic_table,
                                  ast_node node) {
    const char *base_name  = node->data.setter.name;
    const char *param_name = node->data.setter.param;
    const char *sname      = base_name ? base_name : "(null)";

    fprintf(stdout,
            "[sem] setter body: %s (scope=%s)\n",
            sname,
            sem_scope_ids_current(&semantic_table->ids));

    int result_code =
        symtab_insert_accessor_symbol(semantic_table,
                                      true,
                                      base_name,
                                      1);
    if (result_code != SUCCESS) {
        return result_code;
    }

    fprintf(stdout, "[sem] scope PUSH (setter=%s)\n", sname);
    sem_scope_enter_block(semantic_table);

    // Declare setter parameter in this scope.
    if (!scopes_declare_local(&semantic_table->scopes,
                              param_name,
                              true)) {
        (void)sem_scope_leave_block(semantic_table,
                                    "sem_handle_setter_node (param)");
        fprintf(stdout,
                "[sem] scope POP (setter=%s, param redeclared)\n",
                sname);
        return error(ERR_REDEF,
                     "setter parameter redeclared: %s",
                     param_name ? param_name : "(null)");
    }

    st_data *setter_param_data =
        scopes_lookup_in_current(&semantic_table->scopes,
                                 param_name);
    if (setter_param_data) {
        setter_param_data->symbol_type = ST_PAR;
    }

    result_code =
        symtab_insert_symbol(semantic_table,
                             ST_PAR,
                             param_name,
                             0);
    if (result_code != SUCCESS) {
        (void)sem_scope_leave_block(
            semantic_table,
            "sem_handle_setter_node (param symtab)");
        fprintf(stdout,
                "[sem] scope POP (setter=%s, early error)\n",
                sname);
        return result_code;
    }

    // Traverse body without injecting an extra outer block scope.
    if (node->data.setter.body) {
        for (ast_node stmt = node->data.setter.body->first;
             stmt;
             stmt = stmt->next) {
            result_code =
                visit_statement_node(semantic_table, stmt);
            if (result_code != SUCCESS) {
                (void)sem_scope_leave_block(
                    semantic_table,
                    "sem_handle_setter_node (body)");
                fprintf(stdout,
                        "[sem] scope POP (setter=%s, early error)\n",
                        sname);
                return result_code;
            }
        }
    }

    result_code =
        sem_scope_leave_block(semantic_table,
                              "sem_handle_setter_node");
    fprintf(stdout, "[sem] scope POP (setter=%s)\n", sname);
    return result_code;
}

/**
 * @brief Visit a single AST node in statement position. (Pass 1)
 */
static int visit_statement_node(semantic *semantic_table,
                                ast_node node) {
    if (!node) {
        return SUCCESS;
    }

    switch (node->type) {
        case AST_BLOCK:
            return visit_block_node(semantic_table,
                                    node->data.block);

        case AST_CONDITION:
            return sem_handle_condition_node(semantic_table,
                                             node);

        case AST_WHILE_LOOP:
            return sem_handle_while_node(semantic_table,
                                         node);

        case AST_BREAK:
            if (semantic_table->loop_depth <= 0) {
                return error(ERR_SEM, "break outside of loop");
            }
            fprintf(stdout,
                    "[sem] break (ok, depth=%d, scope=%s)\n",
                    semantic_table->loop_depth,
                    sem_scope_ids_current(&semantic_table->ids));
            return SUCCESS;

        case AST_CONTINUE:
            if (semantic_table->loop_depth <= 0) {
                return error(ERR_SEM, "continue outside of loop");
            }
            fprintf(stdout,
                    "[sem] continue (ok, depth=%d, scope=%s)\n",
                    semantic_table->loop_depth,
                    sem_scope_ids_current(&semantic_table->ids));
            return SUCCESS;

        case AST_EXPRESSION:
            return visit_expression_node(semantic_table,
                                         node->data.expression);

        case AST_VAR_DECLARATION: {
            const char *variable_name = node->data.declaration.name;
            fprintf(stdout,
                    "[sem] var declare: %s (scope=%s)\n",
                    variable_name ? variable_name : "(null)",
                    sem_scope_ids_current(&semantic_table->ids));

            if (!scopes_declare_local(&semantic_table->scopes,
                                      variable_name,
                                      true)) {
                return error(ERR_REDEF,
                             "variable '%s' already declared in this scope",
                             variable_name ? variable_name : "(null)");
            }

            st_data *variable_data =
                scopes_lookup_in_current(&semantic_table->scopes,
                                         variable_name);
            if (variable_data) {
                variable_data->symbol_type = ST_VAR;
            }

            int result_code =
                symtab_insert_symbol(semantic_table,
                                     ST_VAR,
                                     variable_name,
                                     0);
            if (result_code != SUCCESS) {
                return result_code;
            }

            return SUCCESS;
        }

        case AST_ASSIGNMENT: {
            const char *assigned_name =
                node->data.assignment.name;
            fprintf(stdout,
                    "[sem] assign to: %s (scope=%s)\n",
                    assigned_name ? assigned_name : "(null)",
                    sem_scope_ids_current(&semantic_table->ids));

            int result_code =
                sem_check_assignment_lhs(semantic_table,
                                         assigned_name);
            if (result_code != SUCCESS) {
                return result_code;
            }

            return visit_expression_node(
                semantic_table,
                node->data.assignment.value);
        }

        case AST_FUNCTION:
            return sem_handle_function_node(semantic_table,
                                            node);

        case AST_IFJ_FUNCTION: {
            ast_ifj_function ifj_call = node->data.ifj_function;
            if (!ifj_call || !ifj_call->name) {
                return SUCCESS;
            }

            fprintf(stdout,
                    "[sem] IFJ stmt call: %s (scope=%s)\n",
                    ifj_call->name,
                    sem_scope_ids_current(&semantic_table->ids));

            return sem_check_builtin_call(semantic_table,
                                          ifj_call->name,
                                          ifj_call->parameters);
        }

        case AST_CALL_FUNCTION: {
            ast_fun_call call_node = node->data.function_call;
            int parameter_count    = count_parameters(call_node->parameters);

            if (builtins_is_builtin_qname(call_node->name)) {
                return sem_check_builtin_call(semantic_table,
                                              call_node->name,
                                              call_node->parameters);
            }

            return check_function_call_arity(semantic_table,
                                             call_node->name,
                                             parameter_count);
        }

        case AST_RETURN:
            return visit_expression_node(
                semantic_table,
                node->data.return_expr.output);

        case AST_GETTER:
            return sem_handle_getter_node(semantic_table, node);

        case AST_SETTER:
            return sem_handle_setter_node(semantic_table, node);
    }

    return SUCCESS;
}

/* =========================================================================
 *                      Header collection (recursive) – Pass 1
 * ========================================================================= */

/**
 * @brief Collect function/getter/setter headers anywhere inside the class block tree.
 *        (Recurses into nested blocks; does NOT descend into function bodies.)
 *        Propagates class scope name into the stored st_data.scope_name.
 *
 * Functions/accessors are recorded into semantic_table->funcs, but NOT into
 * semantic_table->symtab yet.
 */
static int collect_headers(semantic *semantic_table,
                           ast syntax_tree) {
    for (ast_class class_node = syntax_tree->class_list;
         class_node;
         class_node = class_node->next) {
        const char *class_name =
            class_node->name ? class_node->name : "(anonymous)";
        ast_block root_block = get_class_root_block(class_node);
        if (!root_block) {
            continue;
        }

        int result_code =
            collect_headers_from_block(semantic_table,
                                       root_block,
                                       class_name);
        if (result_code != SUCCESS) {
            return result_code;
        }
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
static int collect_headers_from_block(semantic *semantic_table,
                                      ast_block block_node,
                                      const char *class_scope_name) {
    if (!block_node) {
        return SUCCESS;
    }

    for (ast_node node = block_node->first;
         node;
         node = node->next) {
        switch (node->type) {
            case AST_FUNCTION: {
                ast_function function_node = node->data.function;
                const char *function_name  = function_node->name;

                fprintf(stdout,
                        "[sem] header: %s params=",
                        function_name ? function_name : "(null)");
                const char *separator = "";
                for (ast_parameter parameter = function_node->parameters;
                     parameter;
                     parameter = parameter->next) {

                    const char *param_name = sem_get_parameter_name(parameter);

                    fprintf(stdout, "%s%s",
                            separator,
                            param_name ? param_name : "(null)");
                    separator = ", ";
                }
                fprintf(stdout, "\n");

                int arity =
                    count_parameters(function_node->parameters);
                int result_code =
                    function_table_insert_signature(
                        semantic_table,
                        function_name,
                        arity,
                        class_scope_name);
                if (result_code != SUCCESS) {
                    return result_code;
                }

                result_code =
                    check_and_mark_main_function(
                        semantic_table,
                        function_name,
                        arity);
                if (result_code != SUCCESS) {
                    return result_code;
                }
                break;
            }
            case AST_GETTER: {
                int result_code =
                    function_table_insert_accessor(
                        semantic_table,
                        node->data.getter.name,
                        false,
                        class_scope_name,
                        NULL);
                if (result_code != SUCCESS) {
                    return result_code;
                }
                break;
            }
            case AST_SETTER: {
                const char *setter_param =
                    node->data.setter.param;
                int result_code =
                    function_table_insert_accessor(
                        semantic_table,
                        node->data.setter.name,
                        true,
                        class_scope_name,
                        setter_param);
                if (result_code != SUCCESS) {
                    return result_code;
                }
                break;
            }
            case AST_BLOCK: {
                int result_code =
                    collect_headers_from_block(
                        semantic_table,
                        node->data.block,
                        class_scope_name);
                if (result_code != SUCCESS) {
                    return result_code;
                }
                break;
            }
            default:
                // other nodes ignored in header collection
                break;
        }
    }
    return SUCCESS;
}

/* =========================================================================
 *                  Pretty-print of symtab grouped by scopes
 * ========================================================================= */

typedef struct {
    const char *scope;   // e.g. "1.1.1.1" or "global"
    const char *name;    // identifier without "<scope>::" prefix
    symbol_type kind;    // ST_FUN / ST_VAR / ST_PAR / ...
    int arity;           // for functions/accessors; otherwise 0
} sem_row;

typedef struct {
    sem_row *rows;
    size_t capacity;
    size_t count;
} sem_row_accumulator;

// callback for st_foreach – collect rows in two passes: first count, then fill
static void sem_collect_rows_cb(const char *key,
                                st_data *data,
                                void *user_data) {
    sem_row_accumulator *accumulator =
        (sem_row_accumulator *)user_data;
    if (!accumulator) {
        return;
    }

    // 1st pass – just count
    if (!accumulator->rows) {
        accumulator->count++;
        return;
    }

    // 2nd pass – fill the array
    if (accumulator->count >= accumulator->capacity) {
        return;
    }

    // scope: from data->scope_name->data, or "global" if missing
    const char *scope_string = "global";
    if (data && data->scope_name &&
        data->scope_name->data &&
        data->scope_name->length > 0) {
        scope_string = data->scope_name->data;
    }

    // name: prefer data->ID (e.g. "value" for getter/setter), otherwise substring after "::" in key
    const char *name_string = NULL;
    if (data && data->ID &&
        data->ID->data &&
        data->ID->length > 0) {
        name_string = data->ID->data;
    } else {
        name_string = key;
        const char *separator = strstr(key, "::");
        if (separator && separator[2] != '\0') {
            name_string = separator + 2;
        }
    }

    sem_row *row = &accumulator->rows[accumulator->count++];
    row->scope = scope_string;
    row->name  = name_string;
    row->kind  = data ? data->symbol_type : ST_VAR;
    row->arity = data ? data->param_count : 0;
}

// ordering: by scope, then name, then kind+arity
static int sem_row_compare(const void *a, const void *b) {
    const sem_row *row_a = (const sem_row *)a;
    const sem_row *row_b = (const sem_row *)b;

    int scope_cmp = strcmp(row_a->scope, row_b->scope);
    if (scope_cmp != 0) {
        return scope_cmp;
    }

    int name_cmp = strcmp(row_a->name, row_b->name);
    if (name_cmp != 0) {
        return name_cmp;
    }

    if ((int)row_a->kind != (int)row_b->kind) {
        return (int)row_a->kind - (int)row_b->kind;
    }
    return row_a->arity - row_b->arity;
}

static const char *sem_kind_to_str(symbol_type symbol_kind) {
    switch (symbol_kind) {
        case ST_VAR:    return "var";
        case ST_CONST:  return "const";
        case ST_FUN:    return "function";
        case ST_PAR:    return "param";
        case ST_GLOB:   return "global";
        case ST_GETTER: return "getter";
        case ST_SETTER: return "setter";
        default:        return "symbol";
    }
}

/**
 * @brief Pretty-print semantic_table->symtab grouped by scope.
 */
static void sem_pretty_print_symbol_table(semantic *semantic_table) {
    if (!semantic_table || !semantic_table->symtab) {
        fprintf(stdout, "==== SYMBOL TABLE (no table) ====\n");
        return;
    }

    // determine number of entries using st_foreach
    sem_row_accumulator accumulator =
        (sem_row_accumulator){ .rows = NULL,
                               .capacity = 0,
                               .count = 0 };
    st_foreach(semantic_table->symtab,
               sem_collect_rows_cb,
               &accumulator);

    if (accumulator.count == 0) {
        fprintf(stdout, "==== SYMBOL TABLE (empty) ====\n");
        return;
    }

    // allocate array and fill
    sem_row *rows =
        calloc(accumulator.count, sizeof *rows);
    if (!rows) {
        fprintf(stdout,
                "==== SYMBOL TABLE (allocation failed, cannot pretty-print) ====\n");
        return;
    }

    accumulator.rows     = rows;
    accumulator.capacity = accumulator.count;
    accumulator.count    = 0;

    st_foreach(semantic_table->symtab,
               sem_collect_rows_cb,
               &accumulator);

    qsort(rows, accumulator.count,
          sizeof *rows, sem_row_compare);

    fprintf(stdout,
            "===========================================================\n");
    fprintf(stdout,
            "SYMBOL TABLE AFTER semantic_pass1\n");
    fprintf(stdout,
            "===========================================================\n\n");

    const char *current_scope = NULL;
    for (size_t i = 0; i < accumulator.count; ++i) {
        sem_row *row = &rows[i];

        if (!current_scope ||
            strcmp(current_scope, row->scope) != 0) {
            // new scope – print heading
            current_scope = row->scope;
            fprintf(stdout,
                    "-----------------------------------------------------------\n");
            fprintf(stdout, "Scope: %s\n", current_scope);
            fprintf(stdout,
                    "-----------------------------------------------------------\n");
            fprintf(stdout,
                    "%-20s %-12s %-5s\n",
                    "Name", "Kind", "Arity");
            fprintf(stdout,
                    "%-20s %-12s %-5s\n",
                    "--------------------",
                    "------------",
                    "-----");
        }

        const char *kind_string = sem_kind_to_str(row->kind);
        fprintf(stdout, "%-20s %-12s %-5d\n",
                row->name,
                kind_string,
                row->arity);
    }

    fprintf(stdout,
            "-----------------------------------------------------------\n");

    free(rows);
}

/* =========================================================================
 *                              Entry point – Pass 1
 * ========================================================================= */

/**
 * @brief Run the first semantic pass over the AST, then Pass 2.
 */
int semantic_pass1(ast syntax_tree) {
    semantic semantic_table;
    memset(&semantic_table, 0, sizeof semantic_table);

    /* reset registry of magic globals for this compilation */
    sem_magic_globals_reset();

    semantic_table.funcs = st_init();
    if (!semantic_table.funcs) {
        return error(ERR_INTERNAL,
                     "failed to init global function table");
    }

    semantic_table.symtab = st_init();
    if (!semantic_table.symtab) {
        int rc =
            error(ERR_INTERNAL,
                  "failed to init global symbol table");
        st_free(semantic_table.funcs);
        return rc;
    }

    scopes_init(&semantic_table.scopes);
    sem_scope_ids_init(&semantic_table.ids);

    semantic_table.loop_depth = 0;
    semantic_table.seen_main  = false;

    fprintf(stdout, "[sem] seeding IFJ built-ins...\n");
    builtins_config builtins_configuration =
        (builtins_config){ .ext_boolthen = false,
                           .ext_statican = false };
    if (!builtins_install(semantic_table.funcs,
                          builtins_configuration)) {
        int rc = error(ERR_INTERNAL,
                       "failed to install built-ins");
        st_free(semantic_table.funcs);
        st_free(semantic_table.symtab);
        return rc;
    }
    fprintf(stdout, "[sem] built-ins seeded.\n");

    /* Zkopírujeme builtiny do symtab (scope=global) pro účely výpisu. */
    sem_copy_builtins_to_symbol_table(&semantic_table);

    /* 1. průchod – hlavičky funkcí / getterů / setterů */
    int result_code =
        collect_headers(&semantic_table, syntax_tree);
    if (result_code != SUCCESS) {
        st_free(semantic_table.funcs);
        st_free(semantic_table.symtab);
        return result_code;
    }

    if (!semantic_table.seen_main) {
        int rc = error(ERR_DEF,
                       "missing main() with 0 parameters");
        st_free(semantic_table.funcs);
        st_free(semantic_table.symtab);
        return rc;
    }

    /* 2. část Pass-1 – walk přes těla programů/funkcí */
    for (ast_class class_node = syntax_tree->class_list;
         class_node;
         class_node = class_node->next) {
        ast_block root_block =
            get_class_root_block(class_node);
        if (!root_block) {
            continue;
        }

        result_code =
            visit_block_node(&semantic_table, root_block);
        if (result_code != SUCCESS) {
            st_free(semantic_table.funcs);
            st_free(semantic_table.symtab);
            return result_code;
        }
    }

    // Final symbol table dump after Pass-1
    sem_pretty_print_symbol_table(&semantic_table);
    sem_debug_print_magic_globals();

    // Run Pass-2 (identifier + call resolution) with the same semantic_table.
    int pass2_result = semantic_pass2(&semantic_table, syntax_tree);

    st_free(semantic_table.funcs);
    st_free(semantic_table.symtab);

    return pass2_result;
}

/* =========================================================================
 *                              Pass 2
 * ========================================================================= */

static int sem2_visit_block(semantic *table, ast_block blk);

/* -------------------------------------------------------------------------
 *  Identifier resolver (with rich debug)
 * ------------------------------------------------------------------------- */
static int sem2_resolve_identifier(semantic *cxt, const char *name)
{
    if (!name) return SUCCESS;

    printf("[sem2][ID] Resolving '%s' at scope=%s\n",
           name, sem_scope_ids_current(&cxt->ids));

    /* Magic globals */
    if (is_magic_global_identifier(name)) {
        printf("[sem2][ID] → magic global OK\n");
        return SUCCESS;
    }

    /* Local var/param */
    st_data *local = scopes_lookup(&cxt->scopes, name);
    if (local) {
        printf("[sem2][ID] → local OK (symbol_type=%d)\n", local->symbol_type);
        return SUCCESS;
    }

    /* Accessor check */
    char key_get[256], key_set[256];
    make_accessor_key(key_get, sizeof key_get, name, false);
    make_accessor_key(key_set, sizeof key_set, name, true);

    printf("[sem2][ID] Trying accessor keys: get='%s', set='%s'\n",
           key_get, key_set);

    bool has_getter = (st_find(cxt->funcs, key_get) != NULL);
    bool has_setter = (st_find(cxt->funcs, key_set) != NULL);

    if (has_getter) {
        printf("[sem2][ID] → getter OK\n");
        return SUCCESS;
    }

    if (has_setter) {
        printf("[sem2][ID] → setter exists but no getter → ERROR\n");
        return error(ERR_DEF,
                     "use of setter-only property '%s' without getter",
                     name);
    }

    printf("[sem2][ID] → ERROR undefined identifier\n");
    return error(ERR_DEF,
                 "use of undefined identifier '%s'", name);
}

/* -------------------------------------------------------------------------
 *  Function call checker
 * ------------------------------------------------------------------------- */
static int sem2_check_function_call(semantic *cxt,
                                    const char *name,
                                    int arity)
{
    printf("[sem2][CALL] Checking %s(%d) at scope=%s\n",
           name ? name : "(null)",
           arity,
           sem_scope_ids_current(&cxt->ids));

    if (!name) {
        printf("[sem2][CALL] Name null → skipping\n");
        return SUCCESS;
    }

    /* Builtins */
    if (builtins_is_builtin_qname(name)) {
        char key[256];
        make_function_key(key, sizeof key, name, arity);

        printf("[sem2][CALL] → builtin, key='%s'\n", key);

        if (!st_find(cxt->funcs, (char *)key)) {
            printf("[sem2][CALL] → ERROR builtin wrong arity\n");
            return error(ERR_ARGNUM,
                         "wrong number of arguments for builtin %s(%d)",
                         name, arity);
        }

        printf("[sem2][CALL] → builtin OK\n");
        return SUCCESS;
    }

    /* User functions */
    char sig_key[256];
    make_function_key(sig_key, sizeof sig_key, name, arity);
    printf("[sem2][CALL] → user key='%s'\n", sig_key);

    if (function_table_has_signature(cxt, name, arity)) {
        printf("[sem2][CALL] → user OK exact match\n");
        return SUCCESS;
    }

    if (function_table_has_any_overload(cxt, name)) {
        printf("[sem2][CALL] → overload exists but wrong arity → ERROR\n");
        return error(ERR_ARGNUM,
                     "wrong number of arguments for %s (arity=%d)",
                     name, arity);
    }

    printf("[sem2][CALL] → no such function → ERROR\n");
    return error(ERR_DEF, "call to undefined function '%s'", name);
}

/* -------------------------------------------------------------------------
 *  Expression visitor (recursive)
 * ------------------------------------------------------------------------- */
static int sem2_visit_expr(semantic *cxt, ast_expression e)
{
    if (!e) return SUCCESS;

    printf("[sem2][EXPR] Visiting expr type=%d at scope=%s\n",
           e->type, sem_scope_ids_current(&cxt->ids));

    switch (e->type) {

        case AST_IDENTIFIER:
            printf("[sem2][EXPR] → IDENT '%s'\n", e->operands.identifier.value);
            return sem2_resolve_identifier(cxt, e->operands.identifier.value);

        case AST_VALUE:
            printf("[sem2][EXPR] → literal value OK\n");
            return SUCCESS;

        case AST_FUNCTION_CALL: {
            ast_fun_call call = e->operands.function_call;
            if (!call) {
                printf("[sem2][EXPR] → null function call\n");
                return SUCCESS;
            }

            int ar = count_parameters(call->parameters);
            printf("[sem2][EXPR] → FUNCTION_CALL '%s' arity=%d\n",
                   call->name, ar);

            int rc = sem2_check_function_call(cxt, call->name, ar);
            if (rc != SUCCESS) return rc;

            /* Resolve parameters */
            for (ast_parameter p = call->parameters; p; p = p->next) {
                printf("[sem2][EXPR] → param value_type=%d\n", p->value_type);
                if (p->value_type == AST_VALUE_IDENTIFIER) {
                    rc = sem2_resolve_identifier(cxt, p->value.string_value);
                    if (rc != SUCCESS) return rc;
                }
            }
            return SUCCESS;
        }

        case AST_IFJ_FUNCTION_EXPR: {
            ast_ifj_function call = e->operands.ifj_function;
            if (!call) {
                printf("[sem2][EXPR] → IFJ FUNCTION (null)\n");
                return SUCCESS;
            }

            int ar = count_parameters(call->parameters);

            char qname[128];
            const char *name = call->name ? call->name : "(null)";

            if (call->name) {
                size_t len = strlen(call->name);
                if (len + 4 < sizeof qname) {
                    memcpy(qname, "Ifj.", 4);
                    memcpy(qname + 4, call->name, len);
                    qname[4 + len] = '\0';
                    name = qname;
                }
            }

            printf("[sem2][EXPR] → IFJ FUNCTION '%s' arity=%d\n",
                   name, ar);

            int rc = sem2_check_function_call(cxt, name, ar);
            if (rc != SUCCESS) return rc;

            /* resolve identifier parameters */
            for (ast_parameter p = call->parameters; p; p = p->next) {
                printf("[sem2][EXPR] → IFJ param value_type=%d\n",
                       p->value_type);
                if (p->value_type == AST_VALUE_IDENTIFIER) {
                    rc = sem2_resolve_identifier(cxt, p->value.string_value);
                    if (rc != SUCCESS) return rc;
                }
            }

            return SUCCESS;
        }

        case AST_NOT:
        case AST_NOT_NULL:
            printf("[sem2][EXPR] → unary\n");
            return sem2_visit_expr(cxt, e->operands.unary_op.expression);

        /* all binary ops */
        case AST_ADD: case AST_SUB: case AST_MUL: case AST_DIV:
        case AST_EQUALS: case AST_NOT_EQUAL:
        case AST_LT: case AST_LE: case AST_GT: case AST_GE:
        case AST_AND: case AST_OR:
        case AST_TERNARY: case AST_IS: case AST_CONCAT: {
            printf("[sem2][EXPR] → binary\n");
            int rc = sem2_visit_expr(cxt, e->operands.binary_op.left);
            if (rc != SUCCESS) return rc;
            return sem2_visit_expr(cxt, e->operands.binary_op.right);
        }

        case AST_NONE:
        case AST_NIL:
            printf("[sem2][EXPR] → NIL/NONE\n");
            return SUCCESS;

        default:
            printf("[sem2][EXPR] → unhandled type\n");
            return SUCCESS;
    }
}

/* -------------------------------------------------------------------------
 *  Statement visitor
 * ------------------------------------------------------------------------- */
static int sem2_visit_statement_node(semantic *table, ast_node node)
{
    if (!node) return SUCCESS;

    printf("[sem2][STMT] Node type=%d at scope=%s\n",
           node->type, sem_scope_ids_current(&table->ids));

    switch (node->type) {

        case AST_BLOCK:
            printf("[sem2][STMT] → BLOCK\n");
            return sem2_visit_block(table, node->data.block);

        case AST_CONDITION: {
            printf("[sem2][STMT] → IF condition\n");
            int rc = sem2_visit_expr(table, node->data.condition.condition);
            if (rc != SUCCESS) return rc;

            printf("[sem2][STMT] → IF branch\n");
            rc = sem2_visit_block(table, node->data.condition.if_branch);
            if (rc != SUCCESS) return rc;

            printf("[sem2][STMT] → ELSE branch\n");
            return sem2_visit_block(table, node->data.condition.else_branch);
        }

        case AST_WHILE_LOOP: {
            printf("[sem2][STMT] → WHILE\n");
            int rc = sem2_visit_expr(table, node->data.while_loop.condition);
            if (rc != SUCCESS) return rc;
            return sem2_visit_block(table, node->data.while_loop.body);
        }

        case AST_EXPRESSION:
            printf("[sem2][STMT] → EXPRESSION\n");
            return sem2_visit_expr(table, node->data.expression);

        case AST_VAR_DECLARATION: {
            const char *name = node->data.declaration.name;
            printf("[sem2] DECLARE var '%s' (scope=%s)\n",
                   name,
                   sem_scope_ids_current(&table->ids));

            // Insert variable into the current scope, same as Pass 1
            if (!scopes_declare_local(&table->scopes, name, true)) {
                return error(ERR_REDEF,
                             "variable '%s' already declared in this scope",
                             name);
            }
            return SUCCESS;
        }

        case AST_ASSIGNMENT: {
            const char *lhs = node->data.assignment.name;

            printf("[sem2] ASSIGN → %s (scope=%s)\n",
                   lhs, sem_scope_ids_current(&table->ids));

            // MUST resolve LHS variable
            int rc = sem2_resolve_identifier(table, lhs);
            if (rc != SUCCESS) return rc;

            return sem2_visit_expr(table, node->data.assignment.value);
        }

        case AST_FUNCTION: {
            printf("[sem2][STMT] → FUNCTION\n");

            // enter function scope
            sem_scope_enter_block(table);
            printf("[sem2][FUNC] scope=%s\n",
                   sem_scope_ids_current(&table->ids));

            // declare parameters "arg", "val", etc.
            declare_parameter_list_in_current_scope(table, node->data.function->parameters);

            // now visit statements of the function body
            int rc = sem2_visit_block(table, node->data.function->code);

            sem_scope_leave_block(table, "function body");
            return rc;
        }

        case AST_GETTER:
            printf("[sem2][STMT] → GETTER\n");
            return sem2_visit_block(table, node->data.getter.body);

        case AST_SETTER:
            printf("[sem2][STMT] → SETTER\n");
            return sem2_visit_block(table, node->data.setter.body);

        case AST_CALL_FUNCTION: {
            printf("[sem2][STMT] → CALL_FUNCTION\n");
            /* Same as expression call */
            ast_fun_call call = node->data.function_call;
            if (!call) return SUCCESS;
            int ar = count_parameters(call->parameters);
            int rc = sem2_check_function_call(table, call->name, ar);
            if (rc != SUCCESS) return rc;

            /* Resolve parameters identifiers */
            for (ast_parameter p = call->parameters; p; p = p->next) {
                if (p->value_type == AST_VALUE_IDENTIFIER) {
                    rc = sem2_resolve_identifier(table, p->value.string_value);
                    if (rc != SUCCESS) return rc;
                }
            }
            return SUCCESS;
        }

        case AST_RETURN:
            printf("[sem2][STMT] → RETURN\n");
            return sem2_visit_expr(table, node->data.return_expr.output);

        case AST_BREAK:
        case AST_CONTINUE:
            printf("[sem2][STMT] → BREAK/CONTINUE (ignored in Pass 2)\n");
            return SUCCESS;

        case AST_IFJ_FUNCTION: {
            printf("[sem2][STMT] → IFJ_FUNCTION\n");
            ast_ifj_function call = node->data.ifj_function;
            if (!call) return SUCCESS;

            int ar = count_parameters(call->parameters);

            char qname[128];
            const char *name = call->name ? call->name : "(null)";

            if (call->name) {
                size_t len = strlen(call->name);
                if (len + 4 < sizeof qname) {
                    memcpy(qname, "Ifj.", 4);
                    memcpy(qname + 4, call->name, len);
                    qname[4 + len] = '\0';
                    name = qname;
                }
            }

            int rc = sem2_check_function_call(table, name, ar);
            if (rc != SUCCESS) return rc;

            /* Resolve identifier arguments */
            for (ast_parameter p = call->parameters; p; p = p->next) {
                if (p->value_type == AST_VALUE_IDENTIFIER) {
                    rc = sem2_resolve_identifier(table, p->value.string_value);
                    if (rc != SUCCESS) return rc;
                }
            }
            return SUCCESS;
        }
    }

    printf("[sem2][STMT] → unhandled\n");
    return SUCCESS;
}

/* -------------------------------------------------------------------------
 *  Block visitor
 * ------------------------------------------------------------------------- */
static int sem2_visit_block(semantic *table, ast_block blk)
{
    if (!blk) return SUCCESS;

    printf("[sem2][BLK] ENTER block (current scope=%s)\n",
           sem_scope_ids_current(&table->ids));

    sem_scope_enter_block(table);

    printf("[sem2][BLK] NEW scope=%s\n",
           sem_scope_ids_current(&table->ids));

    for (ast_node n = blk->first; n; n = n->next) {
        printf("[sem2][BLK] Visiting node...\n");
        int rc = sem2_visit_statement_node(table, n);
        if (rc != SUCCESS) {
            printf("[sem2][BLK] ERROR inside block\n");
            sem_scope_leave_block(table, "sem2_visit_block");
            return rc;
        }
    }

    printf("[sem2][BLK] LEAVE scope=%s\n",
           sem_scope_ids_current(&table->ids));

    sem_scope_leave_block(table, "sem2_visit_block");
    return SUCCESS;
}

/* -------------------------------------------------------------------------
 *  Pass 2 entry point
 * ------------------------------------------------------------------------- */
int semantic_pass2(semantic *table, ast syntax_tree)
{
    printf("[sem2] =========================================\n");
    printf("[sem2] Starting Pass 2\n");
    printf("[sem2] =========================================\n");

    /* Nová sada scope rámců a scope ID pro Pass 2. */
    scopes_init(&table->scopes);
    sem_scope_ids_init(&table->ids);
    table->loop_depth = 0;

    for (ast_class c = syntax_tree->class_list; c; c = c->next) {
        ast_block root = get_class_root_block(c);
        printf("[sem2] CLASS → '%s'\n",
               c->name ? c->name : "(anonymous)");

        if (!root) {
            printf("[sem2]   (no root block)\n");
            continue;
        }

        int rc = sem2_visit_block(table, root);
        if (rc != SUCCESS) {
            printf("[sem2] Pass 2 FAILED\n");
            return rc;
        }
    }

    printf("[sem2] Pass 2 completed successfully.\n");
    return SUCCESS;
}

/* =========================================================================
 *      Public API: export list of magic globals ("__name") for codegen
 * ========================================================================= */

int semantic_get_magic_globals(char ***out_globals, size_t *out_count)
{
    if (!out_globals || !out_count) {
        return error(ERR_INTERNAL,
                     "semantic_get_magic_globals: NULL output pointer");
    }

    /* allocate a deep copy of the array of char*;
     * caller will own both the array and the strings.
     */
    if (g_magic_globals.count == 0) {
        *out_globals = NULL;
        *out_count   = 0;
        return SUCCESS;
    }

    char **copy = malloc(g_magic_globals.count * sizeof(char *));
    if (!copy) {
        return error(ERR_INTERNAL,
                     "semantic_get_magic_globals: allocation failed");
    }

    for (size_t i = 0; i < g_magic_globals.count; ++i) {
        size_t len = strlen(g_magic_globals.items[i]);
        copy[i] = malloc(len + 1);
        if (!copy[i]) {
            /* clean up already allocated strings */
            for (size_t j = 0; j < i; ++j) {
                free(copy[j]);
            }
            free(copy);
            return error(ERR_INTERNAL,
                         "semantic_get_magic_globals: allocation failed (string)");
        }
        memcpy(copy[i], g_magic_globals.items[i], len + 1);
    }

    *out_globals = copy;
    *out_count   = g_magic_globals.count;
    return SUCCESS;
}
