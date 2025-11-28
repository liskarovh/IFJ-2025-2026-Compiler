/**
 * @file semantic.c
 * @brief IFJ25 semantic analysis – Pass 1 (headers + bodies) and Pass 2 (identifier & call checks).
 *
 * Pass 1:
 *  - seeds IFJ built-ins (arity only) into the global function table via builtins_install(),
 *  - collects user function/getter/setter signatures (overload-by-arity) recursively across nested blocks,
 *  - verifies that main() with arity 0 exists,
 *  - walks bodies with a scope stack (locals & parameters),
 *  - maintains a textual scope-ID stack ("1", "1.1", "1.1.2", ...),
 *  - inserts all declared symbols (functions, params, locals, accessors) into a global symtab
 *    with keys "<scope>::<name>",
 *  - checks local redeclare and break/continue context,
 *  - performs arity checks for known user functions (if header seen),
 *  - performs literal-only expression checks for arithmetic/relational collisions,
 *  - checks assignment LHS in Pass 1: unknown identifiers cause ERR_DEF (3),
 *    except for magic globals "__name" and known setters,
 *  - performs Pass 1 arity and literal-argument checks for built-in calls (Ifj.*) using the builtins table;
 *    when all arguments are literals, incompatible arg types cause ERR_ARGNUM (5).
 *
 * Pass 2:
 *  - performs a second traversal over the AST with a fresh scope stack:
 *    - resolves identifiers (locals, parameters, accessor properties, magic globals),
 *    - checks calls to user functions and Ifj.* built-ins using the function table,
 *    - performs additional ERR_DEF/ERR_ARGNUM checks according to the specification,
 *    - infers approximate data types of locals and magic globals from assignments for later expression checks.
 *
 * Error codes (error.h):
 *  - ERR_DEF      (3)  : main() arity must be 0; use-before-declare of a local; unknown LHS; undefined identifier or call
 *  - ERR_REDEF    (4)  : duplicate (name, arity) in one class; duplicate getter/setter in one class; local redeclare
 *  - ERR_ARGNUM   (5)  : wrong number or literal types of arguments in function/builtin call
 *  - ERR_EXPR     (6)  : literal-only type error in an expression
 *  - ERR_SEM      (10) : break/continue outside of loop
 *  - ERR_INTERNAL (99) : internal failure (allocation, symbol-table access, etc.)
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
 *                      Basic type helper predicates
 * ========================================================================= */

/**
 * @brief Returns true if the given data_type is ST_UNKNOWN.
 */
static bool sem_is_unknown_type(data_type t) {
    return t == ST_UNKNOWN;
}

/**
 * @brief Returns true if the given data_type is numeric (ST_INT or ST_DOUBLE).
 */
static bool sem_is_numeric_type(data_type t) {
    return t == ST_INT || t == ST_DOUBLE;
}

/**
 * @brief Returns true if the given data_type is a string type.
 */
static bool sem_is_string_type(data_type t) {
    return t == ST_STRING;
}

/**
 * @brief Returns true if the given data_type is a boolean type.
 */
static bool sem_is_bool_type(data_type t) {
    return t == ST_BOOL;
}

/**
 * @brief Returns a unified numeric type for two numeric operands.
 *
 * If any operand is ST_DOUBLE, the result is ST_DOUBLE; otherwise ST_INT.
 * Returns ST_UNKNOWN if one of the operands is not numeric.
 */
static data_type sem_unify_numeric_type(data_type a, data_type b) {
    if (!sem_is_numeric_type(a) || !sem_is_numeric_type(b)) {
        return ST_UNKNOWN;
    }
    if (a == ST_DOUBLE || b == ST_DOUBLE) {
        return ST_DOUBLE;
    }
    return ST_INT;
}

/* =========================================================================
 *          Global magic-identifier registry for code generator
 * ========================================================================= */

/**
 * @brief Debug helper that prints the current list of magic global variables.
 *
 * The function calls semantic_get_magic_globals(), prints all entries to stdout
 * and frees the returned array. The internal g_magic_globals registry remains unchanged.

static void sem_debug_print_magic_globals(void) {
    char **globals = NULL;
    size_t count = 0;

    int rc = semantic_get_magic_globals(&globals, &count);
    if (rc != SUCCESS) {
       //fprintf(stdout, "[sem] magic globals: semantic_get_magic_globals failed (rc=%d)\n", rc);
        return;
    }

   //fprintf(stdout, "[sem] magic globals (%zu):\n", count);
    for (size_t i = 0; i < count; ++i) {
       //fprintf(stdout, "  - %s\n", globals[i] ? globals[i] : "(null)");
        free(globals[i]);
    }
    free(globals);
}
*/

/**
 * @brief Registry storing all magic global names ("__name") seen as assignment LHS.
 *
 * This registry is process-global because semantic_pass1() owns the entire semantic_ctx
 * and destroys it before returning to main(). A single global registry is sufficient
 * for this compilation model.
 */
typedef struct {
    char  **items;
    size_t count;
    size_t capacity;
} sem_magic_globals;

static sem_magic_globals g_magic_globals = { NULL, 0, 0 };

/**
 * @brief Internal entry describing a learned type for a magic global variable.
 *
 * This table is used only during semantic analysis to track an approximate type
 * of magic globals such as "__foo". Code generation does not depend on it.
 */
typedef struct {
    char     *name;
    data_type type;
} sem_magic_global_type;

static sem_magic_global_type *g_magic_global_types = NULL;
static size_t g_magic_global_types_count = 0;
static size_t g_magic_global_types_cap = 0;

/**
 * @brief Resets the learned type registry for magic globals.
 */
static void sem_magic_global_types_reset(void) {
    if (g_magic_global_types) {
        for (size_t i = 0; i < g_magic_global_types_count; ++i) {
            free(g_magic_global_types[i].name);
        }
        free(g_magic_global_types);
    }
    g_magic_global_types = NULL;
    g_magic_global_types_count = 0;
    g_magic_global_types_cap = 0;
}

/**
 * @brief Returns the current learned type of a magic global variable.
 *
 * If no information is available or the stored type is VOID/UNKNOWN,
 * the function returns ST_UNKNOWN so that the variable behaves as a dynamic one.
 *
 * @param name Magic global name.
 * @return Learned data_type or ST_UNKNOWN.
 */
static data_type sem_magic_global_type_get(const char *name) {
    if (!name) {
        return ST_UNKNOWN;
    }

    for (size_t i = 0; i < g_magic_global_types_count; ++i) {
        if (strcmp(g_magic_global_types[i].name, name) == 0) {
            data_type t = g_magic_global_types[i].type;
            if (t == ST_VOID || sem_is_unknown_type(t)) {
                return ST_UNKNOWN;
            }
            return t;
        }
    }
    return ST_UNKNOWN;
}

/**
 * @brief Learns the type of a magic global based on an assignment RHS.
 *
 * Used only in Pass 2 when processing assignments of the form "__gX = <expr>".
 *
 * - RHS type UNKNOWN/VOID is ignored (no new information).
 * - First meaningful assignment sets the type.
 * - Numeric types are unified via sem_unify_numeric_type().
 * - Conflicting types degrade the stored type to ST_UNKNOWN.
 *
 * @param name Magic global name.
 * @param rhs_type Data type of the right-hand side expression.
 */
static void sem_magic_global_type_learn(const char *name, data_type rhs_type) {
    if (!name) {
        return;
    }

    if (rhs_type == ST_VOID || sem_is_unknown_type(rhs_type)) {
        return;
    }

    for (size_t i = 0; i < g_magic_global_types_count; ++i) {
        if (strcmp(g_magic_global_types[i].name, name) == 0) {
            data_type old_t = g_magic_global_types[i].type;
            data_type new_t = old_t;

            if (sem_is_unknown_type(old_t) || old_t == ST_VOID || old_t == ST_NULL) {
                new_t = rhs_type;
            } else if (sem_is_numeric_type(old_t) && sem_is_numeric_type(rhs_type)) {
                new_t = sem_unify_numeric_type(old_t, rhs_type);
            } else if (old_t == rhs_type) {
                new_t = old_t;
            } else {
                new_t = ST_UNKNOWN;
            }

            g_magic_global_types[i].type = new_t;
            return;
        }
    }

    if (g_magic_global_types_count == g_magic_global_types_cap) {
        size_t new_cap = g_magic_global_types_cap ? g_magic_global_types_cap * 2 : 8;
        sem_magic_global_type *new_arr = realloc(g_magic_global_types, new_cap * sizeof *new_arr);
        if (!new_arr) {
            return;
        }
        g_magic_global_types = new_arr;
        g_magic_global_types_cap = new_cap;
    }

    size_t len = strlen(name);
    char *copy = malloc(len + 1);
    if (!copy) {
        return;
    }
    memcpy(copy, name, len + 1);

    g_magic_global_types[g_magic_global_types_count].name = copy;
    g_magic_global_types[g_magic_global_types_count].type = rhs_type;
    g_magic_global_types_count++;
}

/**
 * @brief Resets the magic-global name registry (called before starting semantic_pass1()).
 */
static void sem_magic_globals_reset(void) {
    if (g_magic_globals.items) {
        for (size_t i = 0; i < g_magic_globals.count; ++i) {
            free(g_magic_globals.items[i]);
        }
        free(g_magic_globals.items);
    }
    g_magic_globals.items = NULL;
    g_magic_globals.count = 0;
    g_magic_globals.capacity = 0;
}

/**
 * @brief Inserts a magic-global name into the registry if not already present.
 *
 * The function stores a heap-allocated copy of the name.
 *
 * @param name Magic global name to insert.
 * @return SUCCESS on success; ERR_INTERNAL on allocation failure.
 */
static int sem_magic_globals_add(const char *name) {
    if (!name) {
        return SUCCESS;
    }

    if (name[0] != '_' || name[1] != '_') {
        return SUCCESS;
    }

    for (size_t i = 0; i < g_magic_globals.count; ++i) {
        if (strcmp(g_magic_globals.items[i], name) == 0) {
            return SUCCESS;
        }
    }

    if (g_magic_globals.count == g_magic_globals.capacity) {
        size_t new_cap = (g_magic_globals.capacity == 0) ? 8 : g_magic_globals.capacity * 2;
        char **new_items = realloc(g_magic_globals.items, new_cap * sizeof(char *));
        if (!new_items) {
            return error(ERR_INTERNAL, "semantic: failed to grow magic globals array");
        }
        g_magic_globals.items = new_items;
        g_magic_globals.capacity = new_cap;
    }

    size_t len = strlen(name);
    char *copy = malloc(len + 1);
    if (!copy) {
        return error(ERR_INTERNAL, "semantic: failed to allocate magic global name");
    }
    memcpy(copy, name, len + 1);

    g_magic_globals.items[g_magic_globals.count++] = copy;
    return SUCCESS;
}

/* =========================================================================
 *                  Small numeric helper
 * ========================================================================= */

/**
 * @brief Converts an unsigned integer to its decimal representation.
 *
 * The result is written to the given buffer and always null-terminated
 * (provided buffer_size > 0).
 *
 * @param buffer Target buffer.
 * @param buffer_size Size of the target buffer.
 * @param value Value to convert.
 */
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

/**
 * @brief Initializes the scope ID stack to an empty state.
 */
static void sem_scope_ids_init(sem_scope_id_stack *scope_id_stack) {
    if (!scope_id_stack) {
        return;
    }
    scope_id_stack->depth = -1;
}

/**
 * @brief Enters the root scope and creates scope "1".
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
 * @brief Enters a child scope: creates "P.N" where P is parent path and N is next child index.
 */
static void sem_scope_ids_enter_child(sem_scope_id_stack *scope_id_stack) {
    if (!scope_id_stack) {
        return;
    }

    if (scope_id_stack->depth < 0) {
        sem_scope_ids_enter_root(scope_id_stack);
        return;
    }

    if (scope_id_stack->depth + 1 >= SEM_MAX_SCOPE_DEPTH) {
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
 * @brief Leaves the current scope-ID frame (moves one level up).
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
 * @brief Returns textual ID of the current scope ("1", "1.1", ...), or "global" if not set.
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
 * @brief Inserts a symbol with a prepared key into the global symbol table.
 *
 * This helper fills st_data fields:
 *  - symbol_type
 *  - param_count
 *  - defined/global flags
 *  - ID (identifier name)
 *  - scope_name (scope string or "global")
 *
 * @param semantic_table Semantic context.
 * @param symbol_key Complete symbol key "<scope>::<name>".
 * @param symbol_kind Type of symbol (function, variable, parameter, etc.).
 * @param identifier_name Base identifier name.
 * @param scope_string Textual scope such as "1.2.3" or "global".
 * @param arity Arity for functions/accessors.
 * @param dtype Symbol data type (if available).
 * @return SUCCESS or ERR_INTERNAL on failure.
 */
static int symtab_insert_with_key(semantic *semantic_table, const char *symbol_key, symbol_type symbol_kind, const char *identifier_name, const char *scope_string, int arity, data_type dtype) {
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
    symbol_data->data_type = dtype;

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
 * @brief Inserts a symbol into the global symbol table using the current textual scope.
 *
 * Symtab key format: "<scope>::<name>"
 *  - scope: sem_scope_ids_current(), e.g. "1.2.3" or "global"
 *  - name : identifier name (function, variable, parameter, accessor base)
 *
 * @param semantic_table Semantic context.
 * @param symbol_kind Symbol type.
 * @param identifier_name Identifier name to insert.
 * @param arity Arity for functions/accessors.
 * @param dtype Data type of the symbol.
 * @return SUCCESS or ERR_INTERNAL on failure.
 */
static int symtab_insert_symbol(semantic *semantic_table, symbol_type symbol_kind, const char *identifier_name, int arity, data_type dtype) {
    if (!semantic_table || !semantic_table->symtab || !identifier_name) {
        return SUCCESS;
    }

    const char *current_scope = sem_scope_ids_current(&semantic_table->ids);
    const char *scope_text = current_scope ? current_scope : "global";
    const char *name_text = identifier_name ? identifier_name : "(null)";

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

    return symtab_insert_with_key(semantic_table, symbol_key, symbol_kind, identifier_name, scope_text, arity, dtype);
}

/**
 * @brief Inserts an accessor symbol (getter/setter) into the global symbol table.
 *
 * Symtab key: "<scope>::<base>@get" or "<scope>::<base>@set"
 *  - scope: e.g. "1" (class Program)
 *  - base : accessor base name, e.g. "value"
 *  - ID   : base (what is printed as the name)
 *
 * @param semantic_table Semantic context.
 * @param is_setter True for setter, false for getter.
 * @param base_name Base name of the accessor.
 * @param arity Arity of the accessor (0 for getter, 1 for setter).
 * @param dtype Data type of the accessor.
 * @return SUCCESS or ERR_INTERNAL on failure.
 */
static int symtab_insert_accessor_symbol(semantic *semantic_table, bool is_setter, const char *base_name, int arity, data_type dtype) {
    if (!semantic_table || !semantic_table->symtab || !base_name) {
        return SUCCESS;
    }

    const char *current_scope = sem_scope_ids_current(&semantic_table->ids);
    const char *scope_text = current_scope ? current_scope : "global";
    const char *suffix = is_setter ? "set" : "get";

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
    return symtab_insert_with_key(semantic_table, symbol_key, accessor_symbol_type, base_name, scope_text, arity, dtype);
}

/* =========================================================================
 *                             Small helpers
 * ========================================================================= */

/**
 * @brief Composes a function signature key as "name#arity".
 *
 * @param buffer Output buffer.
 * @param buffer_size Size of output buffer.
 * @param function_name Base function name.
 * @param arity Arity of the function.
 */
static void make_function_key(char *buffer, size_t buffer_size, const char *function_name, int arity) {
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
        size_t num_len = strlen(number_buffer);
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
 * @brief Composes a sentinel key for "any overload" as "@name".
 *
 * The sentinel entry is used to mark that some signature for the given function name exists
 * without iterating over the whole function table.
 *
 * @param buffer Output buffer.
 * @param buffer_size Size of output buffer.
 * @param function_name Base function name.
 */
static void make_function_any_key(char *buffer, size_t buffer_size, const char *function_name) {
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
 * @brief Composes an accessor key as "get:base" or "set:base".
 *
 * @param buffer Output buffer.
 * @param buffer_size Size of buffer.
 * @param base_name Base accessor name.
 * @param is_setter True for setter, false for getter.
 */
static void make_accessor_key(char *buffer, size_t buffer_size, const char *base_name, bool is_setter) {
    if (!buffer || buffer_size == 0) {
        return;
    }

    const char *prefix = is_setter ? "set" : "get";
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
 * @brief Counts the number of parameters in a singly-linked ast_parameter list.
 *
 * @param parameter_list Parameter list head.
 * @return Number of parameters.
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
 * @brief Returns the name of a parameter stored in an ast_parameter node.
 *
 * Parameters are stored as AST_VALUE_IDENTIFIER or AST_VALUE_STRING, with
 * value.string_value holding the text.
 *
 * @param parameter Parameter node.
 * @return Pointer to parameter name or NULL.
 */
static const char *sem_get_parameter_name(ast_parameter parameter) {
    if (!parameter) {
        return NULL;
    }

    if (parameter->value_type == AST_VALUE_IDENTIFIER || parameter->value_type == AST_VALUE_STRING) {
        return parameter->value.string_value;
    }

    return NULL;
}

/**
 * @brief Obtains the root block of a class by walking parent pointers.
 *
 * @param class_node Class node.
 * @return Root ast_block or NULL.
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
 * @brief Returns true if an identifier name matches the magic global pattern "__name".
 *
 * @param identifier_name Identifier name to check.
 * @return True if the name is a magic global.
 */
static bool is_magic_global_identifier(const char *identifier_name) {
    return identifier_name && identifier_name[0] == '_' && identifier_name[1] == '_';
}

/**
 * @brief Returns true if an accessor (getter/setter) with given base name exists.
 *
 * @param semantic_table Semantic context.
 * @param base_name Base accessor name.
 * @param is_setter True for setter, false for getter.
 * @return True if such accessor exists in the function table.
 */
static bool sem_has_accessor(semantic *semantic_table, const char *base_name, bool is_setter) {
    if (!semantic_table || !semantic_table->funcs || !base_name) {
        return false;
    }

    char key[256];
    make_accessor_key(key, sizeof key, base_name, is_setter);
    return st_find(semantic_table->funcs, (char *)key) != NULL;
}

/**
 * @brief Checks the left-hand side of an assignment expression.
 *
 * LHS rules:
 *  - Existing local variable or parameter → OK (even if it starts with "__").
 *  - Setter for the given name → OK.
 *  - Name "__foo" without local/setter → magic global, allowed and registered.
 *  - Anything else → ERR_DEF (3).
 *
 * @param semantic_table Semantic context.
 * @param name Identifier on the left-hand side.
 * @return SUCCESS or an error code (ERR_DEF, ERR_INTERNAL).
 */
static int sem_check_assignment_lhs(semantic *semantic_table, const char *name) {
    if (!semantic_table || !name) {
        return SUCCESS;
    }

    if (scopes_lookup(&semantic_table->scopes, name)) {
        return SUCCESS;
    }

    if (sem_has_accessor(semantic_table, name, true)) {
        return SUCCESS;
    }

    if (is_magic_global_identifier(name)) {
        int rc = sem_magic_globals_add(name);
        if (rc != SUCCESS) {
            return rc;
        }
        return SUCCESS;
    }

    return error(ERR_DEF, "assignment to undefined local variable '%s'", name);
}

/* =========================================================================
 *                        Function-table operations + main()
 * ========================================================================= */

static int collect_headers_from_block(semantic *semantic_table, ast_block block_node, const char *class_scope_name);

/**
 * @brief Inserts a user function signature (name, arity) into the global table.
 *
 * Duplicate handling inside one class:
 *  - same (name, arity) and same class_scope_name → ERR_REDEF (4),
 *  - same (name, arity) in different classes → allowed; entries share the same record.
 *
 * The function stores scope_name (containing the class) and ID (function name) into st_data.
 * In Pass 1, this function does not insert the symbol into semantic_table->symtab;
 * the bodies walk (AST_FUNCTION) inserts function symbols into the symtab.
 *
 * @param semantic_table Semantic context.
 * @param function_name Function name.
 * @param arity Number of parameters.
 * @param class_scope_name Name of the class scope.
 * @return SUCCESS or an error code (ERR_REDEF, ERR_INTERNAL).
 */
static int function_table_insert_signature(semantic *semantic_table, const char *function_name, int arity, const char *class_scope_name) {
    char function_key[256];
    make_function_key(function_key, sizeof function_key, function_name, arity);

    st_data *existing = st_get(semantic_table->funcs, (char *)function_key);
    if (existing) {
        const char *existing_scope = NULL;
        if (existing->scope_name && existing->scope_name->data && existing->scope_name->length > 0) {
            existing_scope = existing->scope_name->data;
        }

        const char *new_scope = class_scope_name;

        if (existing_scope && new_scope && strcmp(existing_scope, new_scope) == 0) {
            return error(ERR_REDEF, "duplicate function signature %s in class '%s'", function_key, existing_scope);
        }

        //fprintf(stdout, "[sem] function signature %s already exists in class '%s', new class '%s' allowed (per-class overloading).\n", function_key, existing_scope ? existing_scope : "(none)", new_scope ? new_scope : "(none)");
        return SUCCESS;
    }

    //fprintf(stdout, "[sem] insert function signature: %s (class=%s)\n", function_key, class_scope_name ? class_scope_name : "(null)");

    st_insert(semantic_table->funcs, (char *)function_key, ST_FUN, true);
    st_data *function_data = st_get(semantic_table->funcs, (char *)function_key);
    if (!function_data) {
        return error(ERR_INTERNAL, "failed to store function signature: %s", function_key);
    }

    function_data->symbol_type = ST_FUN;
    function_data->param_count = arity;
    function_data->defined = false;
    function_data->global = true;

    if (class_scope_name) {
        function_data->scope_name = string_create(0);
        if (!function_data->scope_name || !string_append_literal(function_data->scope_name, (char *)class_scope_name)) {
            return error(ERR_INTERNAL, "failed to store function scope_name for '%s'", function_name ? function_name : "(null)");
        }
    } else {
        function_data->scope_name = NULL;
    }

    if (function_name) {
        function_data->ID = string_create(0);
        if (!function_data->ID || !string_append_literal(function_data->ID, (char *)function_name)) {
            return error(ERR_INTERNAL, "failed to store function name (ID) for '%s'", function_name);
        }
    } else {
        function_data->ID = NULL;
    }

    if (function_name && !builtins_is_builtin_qname(function_name)) {
        char any_key[256];
        make_function_any_key(any_key, sizeof any_key, function_name);

        if (!st_find(semantic_table->funcs, (char *)any_key)) {
            st_insert(semantic_table->funcs, (char *)any_key, ST_FUN, true);
            st_data *any_data = st_get(semantic_table->funcs, (char *)any_key);
            if (any_data) {
                any_data->symbol_type = ST_FUN;
                any_data->param_count = 0;
                any_data->defined = false;
                any_data->global = true;
                any_data->ID = NULL;
                any_data->scope_name = NULL;
            }
        }
    }

    return SUCCESS;
}

/**
 * @brief Inserts a getter/setter signature into the function table.
 *
 * Per-class rules:
 *  - at most one getter and one setter per base name in a given class,
 *  - second getter/setter with the same base in the same class → ERR_REDEF (4),
 *  - getter/setter with the same base in another class → allowed (shared record).
 *
 * The function stores scope_name and ID (base name) into st_data.
 * The symbol is not inserted into semantic_table->symtab at this stage.
 *
 * @param semantic_table Semantic context.
 * @param base_name Base name (property name).
 * @param is_setter True for setter, false for getter.
 * @param class_scope_name Name of the class scope.
 * @param setter_param_opt Optional name of the setter parameter (unused in Pass 1).
 * @return SUCCESS or an error code (ERR_REDEF, ERR_INTERNAL).
 */
static int function_table_insert_accessor(semantic *semantic_table, const char *base_name, bool is_setter, const char *class_scope_name, const char *setter_param_opt) {
    (void)setter_param_opt;

    char accessor_key[256];
    make_accessor_key(accessor_key, sizeof accessor_key, base_name, is_setter);

    st_data *existing = st_get(semantic_table->funcs, (char *)accessor_key);
    if (existing) {
        const char *existing_scope = NULL;
        if (existing->scope_name && existing->scope_name->data && existing->scope_name->length > 0) {
            existing_scope = existing->scope_name->data;
        }

        const char *new_scope = class_scope_name;

        if (existing_scope && new_scope && strcmp(existing_scope, new_scope) == 0) {
            return error(ERR_REDEF, is_setter ? "duplicate setter for '%s' in class '%s'" : "duplicate getter for '%s' in class '%s'", base_name ? base_name : "(null)", existing_scope);
        }

        //fprintf(stdout, "[sem] accessor %s for base '%s' already exists in class '%s', new class '%s' allowed.\n", is_setter ? "setter" : "getter", base_name ? base_name : "(null)", existing_scope ? existing_scope : "(none)", new_scope ? new_scope : "(none)");
        return SUCCESS;
    }

    //fprintf(stdout, "[sem] insert %s for '%s' as %s (class=%s)\n", is_setter ? "setter" : "getter", base_name ? base_name : "(null)", accessor_key, class_scope_name ? class_scope_name : "(null)");

    st_insert(semantic_table->funcs, (char *)accessor_key, ST_FUN, true);
    st_data *accessor_data = st_get(semantic_table->funcs, (char *)accessor_key);
    if (!accessor_data) {
        return error(ERR_INTERNAL, "failed to store accessor signature: %s", accessor_key);
    }

    accessor_data->symbol_type = ST_FUN;
    accessor_data->param_count = is_setter ? 1 : 0;
    accessor_data->defined = false;
    accessor_data->global = true;

    if (class_scope_name) {
        accessor_data->scope_name = string_create(0);
        if (!accessor_data->scope_name || !string_append_literal(accessor_data->scope_name, (char *)class_scope_name)) {
            return error(ERR_INTERNAL, "failed to store accessor scope_name for '%s'", base_name ? base_name : "(null)");
        }
    } else {
        accessor_data->scope_name = NULL;
    }

    if (base_name) {
        accessor_data->ID = string_create(0);
        if (!accessor_data->ID || !string_append_literal(accessor_data->ID, (char *)base_name)) {
            return error(ERR_INTERNAL, "failed to store accessor base (ID) for '%s'", base_name);
        }
    } else {
        accessor_data->ID = NULL;
    }

    return SUCCESS;
}

/**
 * @brief Checks if a function is main() and verifies that arity equals 0.
 *
 * When main() with zero parameters is detected, semantic_table->seen_main is set.
 *
 * @param semantic_table Semantic context.
 * @param function_name Function name.
 * @param arity Arity of the function.
 * @return SUCCESS or ERR_DEF if main() has wrong arity.
 */
static int check_and_mark_main_function(semantic *semantic_table, const char *function_name, int arity) {
    if (!function_name || strcmp(function_name, "main") != 0) {
        return SUCCESS;
    }

    //fprintf(stdout, "[sem] encountered main() with arity=%d\n", arity);
    if (arity != 0) {
        return error(ERR_DEF, "main() must have 0 parameters");
    }
    semantic_table->seen_main = true;
    return SUCCESS;
}

/* =========================================================================
 *                      Calls & literal-only expression checks
 * ========================================================================= */

/**
 * @brief Checks if an exact function signature (name, arity) exists in the function table.
 *
 * @param semantic_table Semantic context.
 * @param function_name Function name.
 * @param arity Arity to check.
 * @return True if such signature exists.
 */
static bool function_table_has_signature(semantic *semantic_table, const char *function_name, int arity) {
    if (!semantic_table || !semantic_table->funcs || !function_name) {
        return false;
    }

    char function_key[256];
    make_function_key(function_key, sizeof function_key, function_name, arity);
    return st_find(semantic_table->funcs, (char *)function_key) != NULL;
}

/**
 * @brief Checks if at least one overload (any arity) for a function name exists.
 *
 * Implemented via sentinel "@name" entries inserted when headers are recorded.
 *
 * @param semantic_table Semantic context.
 * @param function_name Function name.
 * @return True if any overload exists.
 */
static bool function_table_has_any_overload(semantic *semantic_table, const char *function_name) {
    if (!semantic_table || !semantic_table->funcs || !function_name) {
        return false;
    }

    char any_key[256];
    make_function_any_key(any_key, sizeof any_key, function_name);
    return st_find(semantic_table->funcs, (char *)any_key) != NULL;
}

/**
 * @brief Performs Pass 1 arity checks for user function calls.
 *
 * Rules:
 *  - If an exact header (name, arity) exists → call is valid.
 *  - If another overload for the same name exists but not with this arity → ERR_ARGNUM.
 *  - If no header for this name is known yet → decision is deferred to Pass 2.
 *
 * @param semantic_table Semantic context.
 * @param function_name Function name.
 * @param arity Argument count.
 * @return SUCCESS or ERR_ARGNUM.
 */
static int check_function_call_arity(semantic *semantic_table, const char *function_name, int arity) {
    //fprintf(stdout, "[sem] call: %s(arity=%d)\n", function_name ? function_name : "(null)", arity);
    if (!function_name) {
        return SUCCESS;
    }

    if (function_table_has_signature(semantic_table, function_name, arity)) {
        return SUCCESS;
    }

    if (function_table_has_any_overload(semantic_table, function_name)) {
        return error(ERR_ARGNUM, "wrong number of arguments for %s (arity=%d)", function_name, arity);
    }

    return SUCCESS;
}

typedef enum {
    LITERAL_UNKNOWN = 0,
    LITERAL_NUMERIC,
    LITERAL_STRING
} literal_kind;

/**
 * @brief Returns true if the expression is exactly an integer literal.
 *
 * @param expression_node Expression node.
 * @return True if the expression is an integer literal.
 */
static bool expression_is_integer_literal(ast_expression expression_node) {
    return expression_node && expression_node->type == AST_VALUE && expression_node->operands.identity.value_type == AST_VALUE_INT;
}

/**
 * @brief Determines literal-kind of a value expression (number, string, or unknown).
 *
 * Treats AST_VALUE_STRING as a string literal and AST_VALUE_INT/AST_VALUE_FLOAT as numeric.
 * All other forms return LITERAL_UNKNOWN.
 *
 * @param expression_node Expression node.
 * @return Literal kind classification.
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
 * @brief Recursively computes literal-kind for an expression subtree.
 *
 * Results:
 *  - LITERAL_NUMERIC: numeric-only literals/operators.
 *  - LITERAL_STRING : string-safe literals/operators (string+string, string*int literal, concat).
 *  - LITERAL_UNKNOWN: involves identifiers, calls, or mixed/unsupported combinations.
 *
 * @param expression_node Expression node.
 * @return Literal kind classification.
 */
static literal_kind get_expression_literal_kind(ast_expression expression_node) {
    if (!expression_node) {
        return LITERAL_UNKNOWN;
    }

    switch (expression_node->type) {
        case AST_VALUE:
            return get_literal_kind_of_value_expression(expression_node);

        case AST_ADD: {
            literal_kind left_kind = get_expression_literal_kind(expression_node->operands.binary_op.left);
            literal_kind right_kind = get_expression_literal_kind(expression_node->operands.binary_op.right);

            if (!left_kind || !right_kind) {
                return LITERAL_UNKNOWN;
            }

            if (left_kind == LITERAL_NUMERIC && right_kind == LITERAL_NUMERIC) {
                return LITERAL_NUMERIC;
            }

            if (left_kind == LITERAL_STRING && right_kind == LITERAL_STRING) {
                return LITERAL_STRING;
            }

            return LITERAL_UNKNOWN;
        }

        case AST_SUB:
        case AST_DIV: {
            literal_kind left_kind = get_expression_literal_kind(expression_node->operands.binary_op.left);
            literal_kind right_kind = get_expression_literal_kind(expression_node->operands.binary_op.right);

            if (left_kind == LITERAL_NUMERIC && right_kind == LITERAL_NUMERIC) {
                return LITERAL_NUMERIC;
            }
            return LITERAL_UNKNOWN;
        }

        case AST_MUL: {
            literal_kind left_kind = get_expression_literal_kind(expression_node->operands.binary_op.left);
            literal_kind right_kind = get_expression_literal_kind(expression_node->operands.binary_op.right);

            if (left_kind == LITERAL_NUMERIC && right_kind == LITERAL_NUMERIC) {
                return LITERAL_NUMERIC;
            }

            if (left_kind == LITERAL_STRING && expression_is_integer_literal(expression_node->operands.binary_op.right)) {
                return LITERAL_STRING;
            }

            return LITERAL_UNKNOWN;
        }

        case AST_CONCAT: {
            literal_kind left_kind = get_expression_literal_kind(expression_node->operands.binary_op.left);
            literal_kind right_kind = get_expression_literal_kind(expression_node->operands.binary_op.right);

            if (left_kind == LITERAL_STRING && right_kind == LITERAL_STRING) {
                return LITERAL_STRING;
            }
            return LITERAL_UNKNOWN;
        }

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
            return LITERAL_UNKNOWN;
    }
}

static int visit_expression_node(semantic *semantic_table, ast_expression expression_node);

/**
 * @brief Performs literal-only policy checks for a binary-like expression.
 *
 * The function receives literal-kind summaries for left and right operands
 * and verifies whether a given operator combination is allowed at compile time.
 *
 * @param op AST operator type.
 * @param left_kind Literal kind of left operand.
 * @param right_kind Literal kind of right operand.
 * @param right_expression Right operand expression (needed for string * int).
 * @return SUCCESS or ERR_EXPR on literal-only policy violation.
 */
static int sem_check_literal_binary(int op, literal_kind left_kind, literal_kind right_kind, ast_expression right_expression) {
    (void)right_expression;

    if (op == AST_ADD) {
        if (left_kind && right_kind) {
            bool ok = (left_kind == LITERAL_NUMERIC && right_kind == LITERAL_NUMERIC) || (left_kind == LITERAL_STRING && right_kind == LITERAL_STRING);
            if (!ok) {
                return error(ERR_EXPR, "invalid literal '+' operands");
            }
        }
    } else if (op == AST_SUB || op == AST_DIV) {
        if (left_kind && right_kind) {
            if (!(left_kind == LITERAL_NUMERIC && right_kind == LITERAL_NUMERIC)) {
                return error(ERR_EXPR, "invalid literal arithmetic operands");
            }
        }
    } else if (op == AST_MUL) {
        if (left_kind && right_kind) {
            bool ok = (left_kind == LITERAL_NUMERIC && right_kind == LITERAL_NUMERIC) || (left_kind == LITERAL_STRING && expression_is_integer_literal(right_expression));
            if (!ok) {
                return error(ERR_EXPR, "invalid literal '*' operands");
            }
        }
    } else if (op == AST_LT || op == AST_LE || op == AST_GT || op == AST_GE) {
        if (left_kind && right_kind) {
            if (!(left_kind == LITERAL_NUMERIC && right_kind == LITERAL_NUMERIC)) {
                return error(ERR_EXPR, "relational operators require numeric literals");
            }
        }
    } else if (op == AST_IS) {
        /* Literal-only checks for 'is' are handled elsewhere if needed. */
    }

    return SUCCESS;
}

/* -------------------------------------------------------------------------
 *                Built-in function calls (Ifj.*) – Pass 1 arity + types
 * ------------------------------------------------------------------------- */

typedef enum {
    PARAM_KIND_UNKNOWN = 0,
    PARAM_KIND_STRING_LITERAL,
    PARAM_KIND_NUMERIC_LITERAL
} param_kind;

/**
 * @brief Basic literal classification of a built-in argument.
 *
 * Returns:
 *  - PARAM_KIND_STRING_LITERAL for string literals,
 *  - PARAM_KIND_NUMERIC_LITERAL for int/float literals,
 *  - PARAM_KIND_UNKNOWN for identifiers, null, complex expressions, or magic globals.
 *
 * @param param Parameter node.
 * @return Parameter literal kind.
 */
static param_kind sem_get_param_literal_kind(ast_parameter param) {
    if (!param) {
        return PARAM_KIND_UNKNOWN;
    }

    if ((param->value_type == AST_VALUE_STRING || param->value_type == AST_VALUE_IDENTIFIER) && param->value.string_value && param->value.string_value[0] == '_' && param->value.string_value[1] == '_') {
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
 * @brief Pass 1 arity + literal-type checks for selected IFJ built-ins (Ifj.*).
 *
 * Typing rules are applied only when arguments are plain literals (string/int/float).
 * When an argument is an identifier or complex expression, type checks are deferred to Pass 2.
 *
 * Wrong arity or statically incompatible literal argument types yield ERR_ARGNUM (5).
 * Arity constraints are taken from the builtins table (semantic_table->funcs) filled by builtins_install().
 *
 * @param semantic_table Semantic context.
 * @param raw_name Either fully qualified "Ifj.floor" or short "floor"/"length" name from AST.
 * @param parameters Parameter list.
 * @return SUCCESS or ERR_ARGNUM.
 */
static int sem_check_builtin_call(semantic *semantic_table, const char *raw_name, ast_parameter parameters) {
    if (!semantic_table || !raw_name) {
        return SUCCESS;
    }

    const char *name = raw_name;
    char qname_buffer[64];

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

    if (!function_table_has_signature(semantic_table, name, arg_count)) {
        return error(ERR_ARGNUM, "wrong number of arguments for builtin %s (arity=%d)", name, arg_count);
    }

    ast_parameter p1 = parameters;
    ast_parameter p2 = p1 ? p1->next : NULL;
    ast_parameter p3 = p2 ? p2->next : NULL;

    if (strcmp(name, "Ifj.floor") == 0) {
        param_kind k1 = sem_get_param_literal_kind(p1);
        if (k1 != PARAM_KIND_UNKNOWN && k1 != PARAM_KIND_NUMERIC_LITERAL) {
            return error(ERR_ARGNUM, "wrong literal type for builtin Ifj.floor");
        }
        return SUCCESS;
    }

    if (strcmp(name, "Ifj.length") == 0) {
        param_kind k1 = sem_get_param_literal_kind(p1);
        if (k1 != PARAM_KIND_UNKNOWN && k1 != PARAM_KIND_STRING_LITERAL) {
            return error(ERR_ARGNUM, "wrong literal type for builtin Ifj.length");
        }
        return SUCCESS;
    }

    if (strcmp(name, "Ifj.substring") == 0) {
        param_kind k1 = sem_get_param_literal_kind(p1);
        param_kind k2 = sem_get_param_literal_kind(p2);
        param_kind k3 = sem_get_param_literal_kind(p3);

        if (k1 != PARAM_KIND_UNKNOWN && k1 != PARAM_KIND_STRING_LITERAL) {
            return error(ERR_ARGNUM, "wrong literal type for builtin Ifj.substring(arg1)");
        }
        if (k2 != PARAM_KIND_UNKNOWN && k2 != PARAM_KIND_NUMERIC_LITERAL) {
            return error(ERR_ARGNUM, "wrong literal type for builtin Ifj.substring(arg2)");
        }
        if (k3 != PARAM_KIND_UNKNOWN && k3 != PARAM_KIND_NUMERIC_LITERAL) {
            return error(ERR_ARGNUM, "wrong literal type for builtin Ifj.substring(arg3)");
        }
        return SUCCESS;
    }

    if (strcmp(name, "Ifj.strcmp") == 0) {
        param_kind k1 = sem_get_param_literal_kind(p1);
        param_kind k2 = sem_get_param_literal_kind(p2);

        if (k1 != PARAM_KIND_UNKNOWN && k1 != PARAM_KIND_STRING_LITERAL) {
            return error(ERR_ARGNUM, "wrong literal type for builtin Ifj.strcmp(arg1)");
        }
        if (k2 != PARAM_KIND_UNKNOWN && k2 != PARAM_KIND_STRING_LITERAL) {
            return error(ERR_ARGNUM, "wrong literal type for builtin Ifj.strcmp(arg2)");
        }
        return SUCCESS;
    }

    if (strcmp(name, "Ifj.ord") == 0) {
        param_kind k1 = sem_get_param_literal_kind(p1);
        param_kind k2 = sem_get_param_literal_kind(p2);

        if (k1 != PARAM_KIND_UNKNOWN && k1 != PARAM_KIND_STRING_LITERAL) {
            return error(ERR_ARGNUM, "wrong literal type for builtin Ifj.ord(arg1)");
        }
        if (k2 != PARAM_KIND_UNKNOWN && k2 != PARAM_KIND_NUMERIC_LITERAL) {
            return error(ERR_ARGNUM, "wrong literal type for builtin Ifj.ord(arg2)");
        }
        return SUCCESS;
    }

    if (strcmp(name, "Ifj.chr") == 0) {
        param_kind k1 = sem_get_param_literal_kind(p1);

        if (k1 != PARAM_KIND_UNKNOWN && k1 != PARAM_KIND_NUMERIC_LITERAL) {
            return error(ERR_ARGNUM, "wrong literal type for builtin Ifj.chr");
        }
        return SUCCESS;
    }

    return SUCCESS;
}

/**
 * @brief Handles a function-call expression node (AST_FUNCTION_CALL) in Pass 1.
 *
 * Built-in calls are forwarded to sem_check_builtin_call(), and user calls
 * are checked using header information in the function table.
 *
 * @param semantic_table Semantic context.
 * @param expression_node Expression node representing the call.
 * @return SUCCESS or an error code.
 */
static int sem_visit_call_expr(semantic *semantic_table, ast_expression expression_node) {
    if (!expression_node || !expression_node->operands.function_call) {
        return SUCCESS;
    }

    ast_fun_call call_node = expression_node->operands.function_call;
    const char *called_name = call_node->name;

    if (builtins_is_builtin_qname(called_name)) {
        return sem_check_builtin_call(semantic_table, called_name, call_node->parameters);
    }

    int parameter_count = count_parameters(call_node->parameters);
    return check_function_call_arity(semantic_table, called_name, parameter_count);
}

/**
 * @brief Handles a binary-like expression (including relational, logical, ternary, and 'is').
 *
 * The function visits both operands and then performs literal-only expression checks.
 *
 * @param semantic_table Semantic context.
 * @param expression_node Expression node.
 * @return SUCCESS or error code.
 */
static int sem_visit_binary_expr(semantic *semantic_table, ast_expression expression_node) {
    ast_expression left_expression = expression_node->operands.binary_op.left;
    ast_expression right_expression = expression_node->operands.binary_op.right;

    int result_code = visit_expression_node(semantic_table, left_expression);
    if (result_code != SUCCESS) {
        return result_code;
    }

    result_code = visit_expression_node(semantic_table, right_expression);
    if (result_code != SUCCESS) {
        return result_code;
    }

    literal_kind left_kind = get_expression_literal_kind(left_expression);
    literal_kind right_kind = get_expression_literal_kind(right_expression);

    return sem_check_literal_binary((int)expression_node->type, left_kind, right_kind, right_expression);
}

/**
 * @brief Visits an expression node in Pass 1 and performs early checks.
 *
 * For most expressions this pass focuses on calls and literal-only arithmetic/relational policies.
 *
 * @param semantic_table Semantic context.
 * @param expression_node Expression node.
 * @return SUCCESS or error code.
 */
static int visit_expression_node(semantic *semantic_table, ast_expression expression_node) {
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

            //fprintf(stdout, "[sem] IFJ expr call: %s (scope=%s)\n", ifj_call->name, sem_scope_ids_current(&semantic_table->ids));
            return sem_check_builtin_call(semantic_table, ifj_call->name, ifj_call->parameters);
        }

        case AST_FUNCTION_CALL:
            return sem_visit_call_expr(semantic_table, expression_node);

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

/**
 * @brief st_foreach callback: copies IFJ built-in entries from funcs into symtab.
 *
 * Only "Ifj.*#arity" entries are considered; they are inserted into the global
 * symbol table as functions, suitable for later symbol-table dumps.
 *
 * @param key Function key (e.g. "Ifj.read_num#0").
 * @param data Associated st_data.
 * @param user_data Semantic context.
 */
static void sem_copy_builtin_cb(const char *key, st_data *data, void *user_data) {
    semantic *semantic_table = (semantic *)user_data;
    if (!semantic_table || !key || !data) {
        return;
    }

    if (strncmp(key, "Ifj.", 4) != 0) {
        return;
    }

    const char *hash_position = strchr(key, '#');
    size_t base_name_length = hash_position ? (size_t)(hash_position - key) : strlen(key);

    char builtin_name_buffer[128];
    if (base_name_length >= sizeof(builtin_name_buffer)) {
        base_name_length = sizeof(builtin_name_buffer) - 1;
    }
    memcpy(builtin_name_buffer, base_name_length ? key : "", base_name_length);
    builtin_name_buffer[base_name_length] = '\0';

    int arity = data->param_count;

    symtab_insert_symbol(semantic_table, ST_FUN, builtin_name_buffer, arity, data->data_type);
}

/**
 * @brief Copies all IFJ built-ins from the function table into the global symbol table.
 *
 * This pass is mainly used for debug printing of the symbol table after Pass 1.
 *
 * @param semantic_table Semantic context.
 */
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

/**
 * @brief Enters a block scope in Pass 1 (updates scope IDs and local scopes).
 *
 * @param semantic_table Semantic context.
 */
static void sem_scope_enter_block(semantic *semantic_table) {
    if (semantic_table->ids.depth < 0) {
        sem_scope_ids_enter_root(&semantic_table->ids);
    } else {
        sem_scope_ids_enter_child(&semantic_table->ids);
    }
    scopes_push(&semantic_table->scopes);
}

/**
 * @brief Leaves a block scope in Pass 1 and checks for scope stack consistency.
 *
 * @param semantic_table Semantic context.
 * @param context Diagnostic context string (for error messages).
 * @return SUCCESS or ERR_INTERNAL on underflow.
 */
static int sem_scope_leave_block(semantic *semantic_table, const char *context) {
    if (!scopes_pop(&semantic_table->scopes)) {
        sem_scope_ids_leave(&semantic_table->ids);
        return error(ERR_INTERNAL, "scope stack underflow in %s", context ? context : "unknown");
    }
    sem_scope_ids_leave(&semantic_table->ids);
    return SUCCESS;
}

/**
 * @brief Declares a function parameter list in the current Pass 1 scope.
 *
 * The function inserts parameters into the local scope and global symbol table,
 * marking them as ST_PAR with unknown data type.
 *
 * @param semantic_table Semantic context.
 * @param parameter_list Head of the parameter list.
 * @return SUCCESS or an error code.
 */
static int declare_parameter_list_in_current_scope(semantic *semantic_table, ast_parameter parameter_list) {
    for (ast_parameter parameter = parameter_list; parameter; parameter = parameter->next) {
        const char *param_name = sem_get_parameter_name(parameter);

        //fprintf(stdout, "[sem] param declare: %s (scope=%s)\n", param_name ? param_name : "(null)", sem_scope_ids_current(&semantic_table->ids));

        if (!param_name) {
            return error(ERR_INTERNAL, "parameter without name in current scope");
        }

        if (!scopes_declare_local(&semantic_table->scopes, param_name, true)) {
            return error(ERR_REDEF, "parameter '%s' redeclared in the same scope", param_name ? param_name : "(null)");
        }

        st_data *parameter_data = scopes_lookup_in_current(&semantic_table->scopes, param_name);
        if (parameter_data) {
            parameter_data->symbol_type = ST_PAR;
            parameter_data->data_type = ST_UNKNOWN;
        }

        int result_code = symtab_insert_symbol(semantic_table, ST_PAR, param_name, 0, ST_UNKNOWN);
        if (result_code != SUCCESS) {
            return result_code;
        }
    }
    return SUCCESS;
}

/**
 * @brief Visits a block in Pass 1: pushes scope, walks all nodes, then pops scope.
 *
 * @param semantic_table Semantic context.
 * @param block_node Block node.
 * @return SUCCESS or an error code.
 */
static int visit_block_node(semantic *semantic_table, ast_block block_node) {
    if (!block_node) {
        return SUCCESS;
    }

    //fprintf(stdout, "[sem] scope PUSH (blk=%p)\n", (void *)block_node);
    sem_scope_enter_block(semantic_table);

    for (ast_node node = block_node->first; node; node = node->next) {
        int result_code = visit_statement_node(semantic_table, node);
        if (result_code != SUCCESS) {
            sem_scope_leave_block(semantic_table, "visit_block_node (early error)");
            //fprintf(stdout, "[sem] scope POP (early error, blk=%p)\n", (void *)block_node);
            return result_code;
        }
    }

    int leave_result = sem_scope_leave_block(semantic_table, "visit_block_node");
    //fprintf(stdout, "[sem] scope POP (blk=%p)\n", (void *)block_node);
    return leave_result;
}

/**
 * @brief Handles an AST_CONDITION node (if/else) in Pass 1.
 *
 * @param semantic_table Semantic context.
 * @param node AST node of type AST_CONDITION.
 * @return SUCCESS or an error code.
 */
static int sem_handle_condition_node(semantic *semantic_table, ast_node node) {
    int result_code = visit_expression_node(semantic_table, node->data.condition.condition);
    if (result_code != SUCCESS) {
        return result_code;
    }

    result_code = visit_block_node(semantic_table, node->data.condition.if_branch);
    if (result_code != SUCCESS) {
        return result_code;
    }

    result_code = visit_block_node(semantic_table, node->data.condition.else_branch);
    return result_code;
}

/**
 * @brief Handles an AST_WHILE_LOOP node in Pass 1.
 *
 * The function checks the loop condition and traverses the loop body with updated loop_depth.
 *
 * @param semantic_table Semantic context.
 * @param node AST node of type AST_WHILE_LOOP.
 * @return SUCCESS or an error code.
 */
static int sem_handle_while_node(semantic *semantic_table, ast_node node) {
    int result_code = visit_expression_node(semantic_table, node->data.while_loop.condition);
    if (result_code != SUCCESS) {
        return result_code;
    }

    semantic_table->loop_depth++;
    //fprintf(stdout, "[sem] while enter (depth=%d, scope=%s)\n", semantic_table->loop_depth, sem_scope_ids_current(&semantic_table->ids));

    result_code = visit_block_node(semantic_table, node->data.while_loop.body);

    //fprintf(stdout, "[sem] while leave (depth=%d, scope=%s)\n", semantic_table->loop_depth, sem_scope_ids_current(&semantic_table->ids));
    semantic_table->loop_depth--;
    return result_code;
}

/**
 * @brief Handles a function body in Pass 1 (parameters and body share one scope).
 *
 * The function registers the function symbol into the global symtab, enters a function
 * scope that holds both parameters and top-level statements, and traverses the body.
 *
 * @param semantic_table Semantic context.
 * @param node AST node of type AST_FUNCTION.
 * @return SUCCESS or an error code.
 */
static int sem_handle_function_node(semantic *semantic_table, ast_node node) {
    ast_function function_node = node->data.function;
    //const char *fname = function_node->name ? function_node->name : "(null)";

    //fprintf(stdout, "[sem] function body: %s (scope=%s)\n", fname, sem_scope_ids_current(&semantic_table->ids));

    int result_code;
    int arity = count_parameters(function_node->parameters);

    result_code = symtab_insert_symbol(semantic_table, ST_FUN, function_node->name, arity, ST_UNKNOWN);
    if (result_code != SUCCESS) {
        return result_code;
    }

    //fprintf(stdout, "[sem] scope PUSH (func=%s)\n", fname);
    sem_scope_enter_block(semantic_table);

    result_code = declare_parameter_list_in_current_scope(semantic_table, function_node->parameters);
    if (result_code != SUCCESS) {
        (void)sem_scope_leave_block(semantic_table, "sem_handle_function_node (params)");
        //fprintf(stdout, "[sem] scope POP (func=%s, early error)\n", fname);
        return result_code;
    }

    if (function_node->code) {
        for (ast_node stmt = function_node->code->first; stmt; stmt = stmt->next) {
            result_code = visit_statement_node(semantic_table, stmt);
            if (result_code != SUCCESS) {
                (void)sem_scope_leave_block(semantic_table, "sem_handle_function_node (body)");
                //fprintf(stdout, "[sem] scope POP (func=%s, early error)\n", fname);
                return result_code;
            }
        }
    }

    result_code = sem_scope_leave_block(semantic_table, "sem_handle_function_node");
    //fprintf(stdout, "[sem] scope POP (func=%s)\n", fname);
    return result_code;
}

/**
 * @brief Handles a getter body in Pass 1; all statements share a single scope.
 *
 * The getter is inserted into the global symbol table via symtab_insert_accessor_symbol(),
 * and then its body is traversed inside one scope frame.
 *
 * @param semantic_table Semantic context.
 * @param node AST node of type AST_GETTER.
 * @return SUCCESS or an error code.
 */
static int sem_handle_getter_node(semantic *semantic_table, ast_node node) {
    //const char *name = node->data.getter.name;
    //const char *gname = name ? name : "(null)";

   //fprintf(stdout, "[sem] getter body: %s (scope=%s)\n", gname, sem_scope_ids_current(&semantic_table->ids));

    int result_code = symtab_insert_accessor_symbol(semantic_table, false, node->data.getter.name, 0, ST_VOID);
    if (result_code != SUCCESS) {
        return result_code;
    }

   //fprintf(stdout, "[sem] scope PUSH (getter=%s)\n", gname);
    sem_scope_enter_block(semantic_table);

    if (node->data.getter.body) {
        for (ast_node stmt = node->data.getter.body->first; stmt; stmt = stmt->next) {
            result_code = visit_statement_node(semantic_table, stmt);
            if (result_code != SUCCESS) {
                (void)sem_scope_leave_block(semantic_table, "sem_handle_getter_node (body)");
               //fprintf(stdout, "[sem] scope POP (getter=%s, early error)\n", gname);
                return result_code;
            }
        }
    }

    result_code = sem_scope_leave_block(semantic_table, "sem_handle_getter_node");
   //fprintf(stdout, "[sem] scope POP (getter=%s)\n", gname);
    return result_code;
}

/**
 * @brief Handles a setter body in Pass 1; the parameter and body share a single scope.
 *
 * The setter is inserted into the global symbol table, and its parameter is declared
 * in the same scope that holds the body statements.
 *
 * @param semantic_table Semantic context.
 * @param node AST node of type AST_SETTER.
 * @return SUCCESS or an error code.
 */
static int sem_handle_setter_node(semantic *semantic_table, ast_node node) {
    const char *base_name = node->data.setter.name;
    const char *param_name = node->data.setter.param;
    //const char *sname = base_name ? base_name : "(null)";

   //fprintf(stdout, "[sem] setter body: %s (scope=%s)\n", sname, sem_scope_ids_current(&semantic_table->ids));

    int result_code = symtab_insert_accessor_symbol(semantic_table, true, base_name, 1, ST_VOID);
    if (result_code != SUCCESS) {
        return result_code;
    }

   //fprintf(stdout, "[sem] scope PUSH (setter=%s)\n", sname);
    sem_scope_enter_block(semantic_table);

    if (!scopes_declare_local(&semantic_table->scopes, param_name, true)) {
        (void)sem_scope_leave_block(semantic_table, "sem_handle_setter_node (param)");
       //fprintf(stdout, "[sem] scope POP (setter=%s, param redeclared)\n", sname);
        return error(ERR_REDEF, "setter parameter redeclared: %s", param_name ? param_name : "(null)");
    }

    st_data *setter_param_data = scopes_lookup_in_current(&semantic_table->scopes, param_name);
    if (setter_param_data) {
        setter_param_data->symbol_type = ST_PAR;
    }

    result_code = symtab_insert_symbol(semantic_table, ST_PAR, param_name, 0, ST_VOID);
    if (result_code != SUCCESS) {
        (void)sem_scope_leave_block(semantic_table, "sem_handle_setter_node (param symtab)");
       //fprintf(stdout, "[sem] scope POP (setter=%s, early error)\n", sname);
        return result_code;
    }

    if (node->data.setter.body) {
        for (ast_node stmt = node->data.setter.body->first; stmt; stmt = stmt->next) {
            result_code = visit_statement_node(semantic_table, stmt);
            if (result_code != SUCCESS) {
                (void)sem_scope_leave_block(semantic_table, "sem_handle_setter_node (body)");
               //fprintf(stdout, "[sem] scope POP (setter=%s, early error)\n", sname);
                return result_code;
            }
        }
    }

    result_code = sem_scope_leave_block(semantic_table, "sem_handle_setter_node");
   //fprintf(stdout, "[sem] scope POP (setter=%s)\n", sname);
    return result_code;
}

/**
 * @brief Visits a single AST node in statement position during Pass 1.
 *
 * The function dispatches handling to node-type-specific helpers and performs
 * checks for loops, assignments, declarations, calls, and expressions.
 *
 * @param semantic_table Semantic context.
 * @param node AST node to visit.
 * @return SUCCESS or an error code.
 */
static int visit_statement_node(semantic *semantic_table, ast_node node) {
    if (!node) {
        return SUCCESS;
    }

    switch (node->type) {
        case AST_BLOCK:
            return visit_block_node(semantic_table, node->data.block);

        case AST_CONDITION:
            return sem_handle_condition_node(semantic_table, node);

        case AST_WHILE_LOOP:
            return sem_handle_while_node(semantic_table, node);

        case AST_BREAK:
            if (semantic_table->loop_depth <= 0) {
                return error(ERR_SEM, "break outside of loop");
            }
           //fprintf(stdout, "[sem] break (ok, depth=%d, scope=%s)\n", semantic_table->loop_depth, sem_scope_ids_current(&semantic_table->ids));
            return SUCCESS;

        case AST_CONTINUE:
            if (semantic_table->loop_depth <= 0) {
                return error(ERR_SEM, "continue outside of loop");
            }
           //fprintf(stdout, "[sem] continue (ok, depth=%d, scope=%s)\n", semantic_table->loop_depth, sem_scope_ids_current(&semantic_table->ids));
            return SUCCESS;

        case AST_EXPRESSION:
            return visit_expression_node(semantic_table, node->data.expression);

        case AST_VAR_DECLARATION: {
            const char *variable_name = node->data.declaration.name;
           //fprintf(stdout, "[sem] var declare: %s (scope=%s)\n", variable_name ? variable_name : "(null)", sem_scope_ids_current(&semantic_table->ids));

            if (!scopes_declare_local(&semantic_table->scopes, variable_name, true)) {
                return error(ERR_REDEF, "variable '%s' already declared in this scope", variable_name ? variable_name : "(null)");
            }

            st_data *variable_data = scopes_lookup_in_current(&semantic_table->scopes, variable_name);
            if (variable_data) {
                variable_data->symbol_type = ST_VAR;
            }

            int result_code = symtab_insert_symbol(semantic_table, ST_VAR, variable_name, 0, ST_VOID);
            if (result_code != SUCCESS) {
                return result_code;
            }

            return SUCCESS;
        }

        case AST_ASSIGNMENT: {
            const char *assigned_name = node->data.assignment.name;
           //fprintf(stdout, "[sem] assign to: %s (scope=%s)\n", assigned_name ? assigned_name : "(null)", sem_scope_ids_current(&semantic_table->ids));

            int result_code = sem_check_assignment_lhs(semantic_table, assigned_name);
            if (result_code != SUCCESS) {
                return result_code;
            }

            return visit_expression_node(semantic_table, node->data.assignment.value);
        }

        case AST_FUNCTION:
            return sem_handle_function_node(semantic_table, node);

        case AST_IFJ_FUNCTION: {
            ast_ifj_function ifj_call = node->data.ifj_function;
            if (!ifj_call || !ifj_call->name) {
                return SUCCESS;
            }

           //fprintf(stdout, "[sem] IFJ stmt call: %s (scope=%s)\n", ifj_call->name, sem_scope_ids_current(&semantic_table->ids));
            return sem_check_builtin_call(semantic_table, ifj_call->name, ifj_call->parameters);
        }

        case AST_CALL_FUNCTION: {
            ast_fun_call call_node = node->data.function_call;
            int parameter_count = count_parameters(call_node->parameters);

            if (builtins_is_builtin_qname(call_node->name)) {
                return sem_check_builtin_call(semantic_table, call_node->name, call_node->parameters);
            }

            return check_function_call_arity(semantic_table, call_node->name, parameter_count);
        }

        case AST_RETURN:
            return visit_expression_node(semantic_table, node->data.return_expr.output);

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
 * @brief Collects function/getter/setter headers inside all class blocks.
 *
 * The function iterates over all classes in the AST, obtains each class root block,
 * and invokes collect_headers_from_block() to record all headers. Collected headers
 * are stored in semantic_table->funcs but not in semantic_table->symtab.
 *
 * @param semantic_table Semantic context.
 * @param syntax_tree AST root.
 * @return SUCCESS or an error code.
 */
static int collect_headers(semantic *semantic_table, ast syntax_tree) {
    for (ast_class class_node = syntax_tree->class_list; class_node; class_node = class_node->next) {
        const char *class_name = class_node->name ? class_node->name : "(anonymous)";
        ast_block root_block = get_class_root_block(class_node);
        if (!root_block) {
            continue;
        }

        int result_code = collect_headers_from_block(semantic_table, root_block, class_name);
        if (result_code != SUCCESS) {
            return result_code;
        }
    }
    return SUCCESS;
}

/**
 * @brief Recursively walks a block to collect function/getter/setter headers.
 *
 * This function:
 *  - collects AST_FUNCTION / AST_GETTER / AST_SETTER headers,
 *  - descends into nested AST_BLOCK nodes,
 *  - does not traverse into function bodies (headers only),
 *  - associates each header with a class_scope_name.
 *
 * @param semantic_table Semantic context.
 * @param block_node Root block node to scan.
 * @param class_scope_name Name of the current class scope.
 * @return SUCCESS or an error code.
 */
static int collect_headers_from_block(semantic *semantic_table, ast_block block_node, const char *class_scope_name) {
    if (!block_node) {
        return SUCCESS;
    }

    for (ast_node node = block_node->first; node; node = node->next) {
        switch (node->type) {
            case AST_FUNCTION: {
                ast_function function_node = node->data.function;
                const char *function_name = function_node->name;

               //fprintf(stdout, "[sem] header: %s params=", function_name ? function_name : "(null)");
                //const char *separator = "";
                for (ast_parameter parameter = function_node->parameters; parameter; parameter = parameter->next) {
                    //const char *param_name = sem_get_parameter_name(parameter);
                   //fprintf(stdout, "%s%s", separator, param_name ? param_name : "(null)");
                    //separator = ", ";
                }
               //fprintf(stdout, "\n");

                int arity = count_parameters(function_node->parameters);
                int result_code = function_table_insert_signature(semantic_table, function_name, arity, class_scope_name);
                if (result_code != SUCCESS) {
                    return result_code;
                }

                result_code = check_and_mark_main_function(semantic_table, function_name, arity);
                if (result_code != SUCCESS) {
                    return result_code;
                }
                break;
            }
            case AST_GETTER: {
                int result_code = function_table_insert_accessor(semantic_table, node->data.getter.name, false, class_scope_name, NULL);
                if (result_code != SUCCESS) {
                    return result_code;
                }
                break;
            }
            case AST_SETTER: {
                const char *setter_param = node->data.setter.param;
                int result_code = function_table_insert_accessor(semantic_table, node->data.setter.name, true, class_scope_name, setter_param);
                if (result_code != SUCCESS) {
                    return result_code;
                }
                break;
            }
            case AST_BLOCK: {
                int result_code = collect_headers_from_block(semantic_table, node->data.block, class_scope_name);
                if (result_code != SUCCESS) {
                    return result_code;
                }
                break;
            }
            default:
                break;
        }
    }
    return SUCCESS;
}

/* =========================================================================
 *                  Pretty-print of symtab grouped by scopes
 * ========================================================================= */

typedef struct {
    const char *scope;
    const char *name;
    symbol_type kind;
    int arity;
    data_type data_type;
} sem_row;

typedef struct {
    sem_row *rows;
    size_t capacity;
    size_t count;
} sem_row_accumulator;

/**
 * @brief st_foreach callback that collects symbol table rows for later printing.
 *
 * In the first pass (rows == NULL), it only increments the count.
 * In the second pass it fills the pre-allocated array.
 *
 * @param key Symbol key.
 * @param data Symbol data.
 * @param user_data Row accumulator.

static void sem_collect_rows_cb(const char *key, st_data *data, void *user_data) {
    sem_row_accumulator *accumulator = (sem_row_accumulator *)user_data;
    if (!accumulator) {
        return;
    }

    if (!accumulator->rows) {
        accumulator->count++;
        return;
    }

    if (accumulator->count >= accumulator->capacity) {
        return;
    }

    const char *scope_string = "global";
    if (data && data->scope_name && data->scope_name->data && data->scope_name->length > 0) {
        scope_string = data->scope_name->data;
    }

    const char *name_string = NULL;
    if (data && data->ID && data->ID->data && data->ID->length > 0) {
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
    row->name = name_string;
    row->kind = data ? data->symbol_type : ST_VAR;
    row->arity = data ? data->param_count : 0;
    row->data_type = data ? data->data_type : ST_NULL;
}
*/

/**
 * @brief Comparator for sem_row used by qsort, orders by scope, then name, then kind and arity.
 *
 * @param a Pointer to sem_row a.
 * @param b Pointer to sem_row b.
 * @return Negative, zero, or positive according to ordering.

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
*/
/**
 * @brief Converts a data_type to a human-readable string.
 *
 * @param t Data type.
 * @return String representation.

static const char *sem_data_type_to_str(data_type t) {
    switch (t) {
        case ST_NULL:   return "Null";
        case ST_INT:    return "Int";
        case ST_DOUBLE: return "Double";
        case ST_STRING: return "String";
        case ST_BOOL:   return "Bool";
        case ST_VOID:   return "Void";
        case ST_U8:     return "U8";
        case ST_UNKNOWN:return "Unknown";
        default:        return "?";
    }
}
*/

/**
 * @brief Converts a symbol_type to a human-readable string.
 *
 * @param symbol_kind Symbol type.
 * @return String representation.

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
*/
/**
 * @brief Prints the contents of semantic_table->symtab grouped by scopes.
 *
 * The output is intended for debug and inspection after semantic_pass1().
 *
 * @param semantic_table Semantic context.

static void sem_pretty_print_symbol_table(semantic *semantic_table) {
    if (!semantic_table || !semantic_table->symtab) {
       //fprintf(stdout, "==== SYMBOL TABLE (no table) ====\n");
        return;
    }

    sem_row_accumulator accumulator = (sem_row_accumulator){ .rows = NULL, .capacity = 0, .count = 0 };
    st_foreach(semantic_table->symtab, sem_collect_rows_cb, &accumulator);

    if (accumulator.count == 0) {
       //fprintf(stdout, "==== SYMBOL TABLE (empty) ====\n");
        return;
    }

    sem_row *rows = calloc(accumulator.count, sizeof *rows);
    if (!rows) {
       //fprintf(stdout, "==== SYMBOL TABLE (allocation failed, cannot pretty-print) ====\n");
        return;
    }

    accumulator.rows = rows;
    accumulator.capacity = accumulator.count;
    accumulator.count = 0;

    st_foreach(semantic_table->symtab, sem_collect_rows_cb, &accumulator);

    qsort(rows, accumulator.count, sizeof *rows, sem_row_compare);

   //fprintf(stdout, "===========================================================\n");
   //fprintf(stdout, "SYMBOL TABLE AFTER semantic_pass1\n");
   //fprintf(stdout, "===========================================================\n\n");

    const char *current_scope = NULL;
    for (size_t i = 0; i < accumulator.count; ++i) {
        sem_row *row = &rows[i];

        if (!current_scope || strcmp(current_scope, row->scope) != 0) {
            current_scope = row->scope;
           //fprintf(stdout, "-----------------------------------------------------------\n");
           //fprintf(stdout, "Scope: %s\n", current_scope);
           //fprintf(stdout, "-----------------------------------------------------------\n");
           //fprintf(stdout, "%-20s %-12s %-5s %-8s\n", "Name", "Kind", "Arity", "Type");
           //fprintf(stdout, "%-20s %-12s %-5s %-8s\n", "--------------------", "------------", "-----", "--------");
        }

        const char *kind_string = sem_kind_to_str(row->kind);
        const char *type_string = sem_data_type_to_str(row->data_type);

       //fprintf(stdout, "%-20s %-12s %-5d %-8s\n", row->name, kind_string, row->arity, type_string);
    }

   //fprintf(stdout, "-----------------------------------------------------------\n");
    free(rows);
}
*/

/* =========================================================================
 *                              Entry point – Pass 1
 * ========================================================================= */

/**
 * @brief Runs the first semantic pass over the AST and then Pass 2.
 *
 * Steps:
 *  - initializes semantic tables and registries,
 *  - installs IFJ built-ins into the function table and copies them into the symtab,
 *  - collects function/getter/setter headers (Pass 1, step 1),
 *  - checks presence and arity of main(),
 *  - walks bodies and fills scopes and the global symbol table (Pass 1, step 2),
 *  - prints the final symbol table,
 *  - runs semantic_pass2() with the same semantic context,
 *  - frees all internal tables and returns the result of Pass 2.
 *
 * @param syntax_tree AST root.
 * @return SUCCESS or the first error encountered.
 */
int semantic_pass1(ast syntax_tree) {
    semantic semantic_table;
    memset(&semantic_table, 0, sizeof semantic_table);

    sem_magic_globals_reset();
    sem_magic_global_types_reset();

    semantic_table.funcs = st_init();
    if (!semantic_table.funcs) {
        return error(ERR_INTERNAL, "failed to init global function table");
    }

    semantic_table.symtab = st_init();
    if (!semantic_table.symtab) {
        int rc = error(ERR_INTERNAL, "failed to init global symbol table");
        st_free(semantic_table.funcs);
        return rc;
    }

    scopes_init(&semantic_table.scopes);
    sem_scope_ids_init(&semantic_table.ids);

    semantic_table.loop_depth = 0;
    semantic_table.seen_main = false;

   //fprintf(stdout, "[sem] seeding IFJ built-ins...\n");
    builtins_config builtins_configuration = (builtins_config){ .ext_boolthen = false, .ext_statican = false };
    if (!builtins_install(semantic_table.funcs, builtins_configuration)) {
        int rc = error(ERR_INTERNAL, "failed to install built-ins");
        st_free(semantic_table.funcs);
        st_free(semantic_table.symtab);
        return rc;
    }
   //fprintf(stdout, "[sem] built-ins seeded.\n");

    sem_copy_builtins_to_symbol_table(&semantic_table);

    int result_code = collect_headers(&semantic_table, syntax_tree);
    if (result_code != SUCCESS) {
        st_free(semantic_table.funcs);
        st_free(semantic_table.symtab);
        return result_code;
    }

    if (!semantic_table.seen_main) {
        int rc = error(ERR_DEF, "missing main() with 0 parameters");
        st_free(semantic_table.funcs);
        st_free(semantic_table.symtab);
        return rc;
    }

    for (ast_class class_node = syntax_tree->class_list; class_node; class_node = class_node->next) {
        ast_block root_block = get_class_root_block(class_node);
        if (!root_block) {
            continue;
        }

        result_code = visit_block_node(&semantic_table, root_block);
        if (result_code != SUCCESS) {
            st_free(semantic_table.funcs);
            st_free(semantic_table.symtab);
            return result_code;
        }
    }

    //sem_pretty_print_symbol_table(&semantic_table);

    int pass2_result = semantic_pass2(&semantic_table, syntax_tree);
    //sem_debug_print_magic_globals();

    st_free(semantic_table.funcs);
    st_free(semantic_table.symtab);

    return pass2_result;
}

/* =========================================================================
 *                              Pass 2
 * ========================================================================= */

static int sem2_visit_block(semantic *table, ast_block blk);

/**
 * @brief Debug helper that prints local variables in the current Pass 2 scope.
 *
 * This function is currently marked as unused but kept for possible diagnostics.
 *
 * @param cxt Semantic context.
 * @param msg Message to print as a header.

__attribute__((unused)) static void sem2_debug_print_scope_locals(semantic *cxt, const char *msg) {
    const char *scope = sem_scope_ids_current(&cxt->ids);

    //printf("\n====== %s (scope=%s) ======\n", msg, scope);

    char prefix[64];
    size_t max_total = sizeof(prefix) - 1;
    size_t len = strlen(scope);
    if (len > max_total - 2) {
        len = max_total - 2;
    }

    memcpy(prefix, scope, len);
    size_t pos = len;

    if (pos < max_total) {
        prefix[pos++] = ':';
    }
    if (pos < max_total) {
        prefix[pos++] = ':';
    }
    prefix[pos] = '\0';

    for (int i = 0; i < SYMTABLE_SIZE; i++) {
        st_symbol *s = &cxt->symtab->table[i];
        if (!s->occupied || !s->data) {
            continue;
        }

        if (strncmp(s->key, prefix, strlen(prefix)) != 0) {
            continue;
        }

        st_data *d = s->data;

        if (d->symbol_type == ST_VAR && !d->global) {
            const char *cg = "(none)";

            if (d->decl_node && d->decl_node->type == AST_VAR_DECLARATION) {
                cg = d->decl_node->data.declaration.cg_name;
                if (!cg) {
                    cg = "(none)";
                }
            }

           //printf("LOCAL %-12s  cg_name = %-12s\n", s->key, cg);
        }
    }

   //printf("========================================\n\n");
}
*/

/* -------------------------------------------------------------------------
 *  Identifier resolver (with debug output)
 * ------------------------------------------------------------------------- */

/**
 * @brief Resolves an identifier in Pass 2, checking locals, accessors, and magic globals.
 *
 * Resolution order:
 *  1) Local variable/parameter,
 *  2) Accessor (getter or setter),
 *  3) Magic global "__name" (implicitly allowed),
 *  4) Otherwise ERR_DEF.
 *
 * @param cxt Semantic context.
 * @param name Identifier name.
 * @return SUCCESS or ERR_DEF/ERR_INTERNAL.
 */
static int sem2_resolve_identifier(semantic *cxt, const char *name) {
    if (!name) {
        return SUCCESS;
    }

   //printf("[sem2][ID] Resolving '%s' at scope=%s\n", name, sem_scope_ids_current(&cxt->ids));

    st_data *local = scopes_lookup(&cxt->scopes, name);
    if (local) {
       //printf("[sem2][ID] → local OK (symbol_type=%d)\n", local->symbol_type);
        return SUCCESS;
    }

    char key_get[256], key_set[256];
    make_accessor_key(key_get, sizeof key_get, name, false);
    make_accessor_key(key_set, sizeof key_set, name, true);

   //printf("[sem2][ID] Trying accessor keys: get='%s', set='%s'\n", key_get, key_set);

    bool has_getter = (st_find(cxt->funcs, key_get) != NULL);
    bool has_setter = (st_find(cxt->funcs, key_set) != NULL);

    if (has_getter) {
       //printf("[sem2][ID] → getter OK\n");
        return SUCCESS;
    }

    if (has_setter) {
       //printf("[sem2][ID] → setter exists but no getter → ERROR\n");
        return error(ERR_DEF, "use of setter-only property '%s' without getter", name);
    }

    if (is_magic_global_identifier(name)) {
        int rc = sem_magic_globals_add(name);
        if (rc != SUCCESS) {
            return rc;
        }
       //printf("[sem2][ID] → magic global OK\n");
        return SUCCESS;
    }

   //printf("[sem2][ID] → ERROR undefined identifier\n");
    return error(ERR_DEF, "use of undefined identifier '%s'", name);
}

/* -------------------------------------------------------------------------
 *  Function call checker (Pass 2)
 * ------------------------------------------------------------------------- */

/**
 * @brief Checks a function call in Pass 2, including built-ins and user functions.
 *
 * Built-in calls:
 *  - checks exact arity via the function table; wrong arity → ERR_ARGNUM.
 *
 * User functions:
 *  - if exact signature exists → OK,
 *  - if any overload exists but not with this arity → ERR_ARGNUM,
 *  - otherwise → ERR_DEF (undefined function).
 *
 * @param cxt Semantic context.
 * @param name Function name (possibly fully qualified for built-ins).
 * @param arity Argument count.
 * @return SUCCESS or an error code.
 */
static int sem2_check_function_call(semantic *cxt, const char *name, int arity) {
   //printf("[sem2][CALL] Checking %s(%d) at scope=%s\n", name ? name : "(null)", arity, sem_scope_ids_current(&cxt->ids));

    if (!name) {
       //printf("[sem2][CALL] Name null → skipping\n");
        return SUCCESS;
    }

    if (builtins_is_builtin_qname(name)) {
        char key[256];
        make_function_key(key, sizeof key, name, arity);

       //printf("[sem2][CALL] → builtin, key='%s'\n", key);

        if (!st_find(cxt->funcs, (char *)key)) {
           //printf("[sem2][CALL] → ERROR builtin wrong arity\n");
            return error(ERR_ARGNUM, "wrong number of arguments for builtin %s(%d)", name, arity);
        }

       //printf("[sem2][CALL] → builtin OK\n");
        return SUCCESS;
    }

    char sig_key[256];
    make_function_key(sig_key, sizeof sig_key, name, arity);
   //printf("[sem2][CALL] → user key='%s'\n", sig_key);

    if (function_table_has_signature(cxt, name, arity)) {
       //printf("[sem2][CALL] → user OK exact match\n");
        return SUCCESS;
    }

    if (function_table_has_any_overload(cxt, name)) {
       //printf("[sem2][CALL] → overload exists but wrong arity → ERROR\n");
        return error(ERR_ARGNUM, "wrong number of arguments for %s (arity=%d)", name, arity);
    }

   //printf("[sem2][CALL] → no such function → ERROR\n");
    return error(ERR_DEF, "call to undefined function '%s'", name);
}

/**
 * @brief Visits an expression in Pass 2 and infers its approximate data type.
 *
 * The visitor resolves identifiers, checks function calls, and applies type
 * rules for operators. It stores the inferred type in out_type if provided.
 * Unknown/void types on either side of operations suppress ERR_EXPR in order
 * to avoid over-constraining non-literal expressions.
 *
 * @param cxt Semantic context.
 * @param e Expression node.
 * @param out_type Optional output for inferred type.
 * @return SUCCESS or an error code.
 */
static int sem2_visit_expr(semantic *cxt, ast_expression e, data_type *out_type) {
    if (!e) {
        return SUCCESS;
    }

    if (out_type) {
        *out_type = ST_UNKNOWN;
    }

   //printf("[sem2][EXPR] Visiting expr type=%d at scope=%s\n", e->type, sem_scope_ids_current(&cxt->ids));

    switch (e->type) {
        case AST_IDENTIFIER: {
            const char *name = e->operands.identifier.value;
           //printf("[sem2][EXPR] → IDENT '%s'\n", name ? name : "(null)");

            int rc = sem2_resolve_identifier(cxt, name);
            if (rc != SUCCESS) {
                return rc;
            }

             st_data *sym = scopes_lookup(&cxt->scopes, name);

    
    if (!sym) {
        char scopedKey[128];
        snprintf(scopedKey, sizeof scopedKey, "%s::%s",
                 sem_scope_ids_current(&cxt->ids), name);

        sym = scopes_lookup(&cxt->scopes, scopedKey);
    }
   
    
     
    if (sym && sym->decl_node &&
        sym->decl_node->type == AST_VAR_DECLARATION) {

        e->operands.identifier.cg_name =
            sym->decl_node->data.declaration.cg_name;

        //printf("[sem2][ID] bind IDENT '%s' -> cg_name='%s'\n",name,e->operands.identifier.cg_name ?e->operands.identifier.cg_name : "(none)");
    }
    else if (sym && sym->params) {   // param_ptr: add this to st_data
    e->operands.identifier.cg_name = sym->decl_node->data.function->parameters->cg_name;

    //printf("[sem2][ID] bind IDENT '%s' -> param cg_name='%s'\n",name,e->operands.identifier.cg_name);
}

            if (out_type && name) {
                data_type t = ST_UNKNOWN;

                if (sym) {
                    t = sym->data_type;
                    if (t == ST_VOID || t == ST_NULL || sem_is_unknown_type(t)) {
                        *out_type = ST_UNKNOWN;
                    } else {
                        *out_type = t;
                    }
                } else if (is_magic_global_identifier(name)) {
                    t = sem_magic_global_type_get(name);
                    if (t == ST_VOID || t == ST_NULL || sem_is_unknown_type(t)) {
                        *out_type = ST_UNKNOWN;
                    } else {
                        *out_type = t;
                    }
                }
            }

            return SUCCESS;
        }

        case AST_VALUE: {
           //printf("[sem2][EXPR] → literal value OK\n");

            if (out_type) {
                switch (e->operands.identity.value_type) {
                    case AST_VALUE_INT:
                        *out_type = ST_INT;
                        break;
                    case AST_VALUE_FLOAT:
                        *out_type = ST_DOUBLE;
                        break;
                    case AST_VALUE_STRING:
                        *out_type = ST_STRING;
                        break;
                    case AST_VALUE_NULL:
                        *out_type = ST_NULL;
                        break;
                    default:
                        *out_type = ST_UNKNOWN;
                        break;
                }
            }

            return SUCCESS;
        }

        case AST_FUNCTION_CALL: {
            ast_fun_call call = e->operands.function_call;
            if (!call) {
               //printf("[sem2][EXPR] → null function call\n");
                return SUCCESS;
            }

            int ar = count_parameters(call->parameters);
           //printf("[sem2][EXPR] → FUNCTION_CALL '%s' arity=%d\n", call->name ? call->name : "(null)", ar);

            int rc = sem2_check_function_call(cxt, call->name, ar);
            if (rc != SUCCESS) {
                return rc;
            }

            for (ast_parameter p = call->parameters; p; p = p->next) {
               //printf("[sem2][EXPR] → param value_type=%d\n", p->value_type);
                if (p->value_type == AST_VALUE_IDENTIFIER) {
                    rc = sem2_resolve_identifier(cxt, p->value.string_value);
                    if (rc != SUCCESS) {
                        return rc;
                    }
                }
            }

            if (out_type) {
                data_type ret = ST_UNKNOWN;

                if (call->name && builtins_is_builtin_qname(call->name)) {
                    char key[256];
                    make_function_key(key, sizeof key, call->name, ar);
                    st_data *fn = st_get(cxt->funcs, (char *)key);
                    if (fn) {
                        ret = fn->data_type;
                    }
                }

                *out_type = ret;
            }

            return SUCCESS;
        }

        case AST_IFJ_FUNCTION_EXPR: {
            ast_ifj_function call = e->operands.ifj_function;
            if (!call) {
               //printf("[sem2][EXPR] → IFJ FUNCTION (null)\n");
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

           //printf("[sem2][EXPR] → IFJ FUNCTION '%s' arity=%d\n", name, ar);

            int rc = sem2_check_function_call(cxt, name, ar);
            if (rc != SUCCESS) {
                return rc;
            }

            for (ast_parameter p = call->parameters; p; p = p->next) {
               //printf("[sem2][EXPR] → IFJ param value_type=%d\n", p->value_type);
                if (p->value_type == AST_VALUE_IDENTIFIER) {
                    rc = sem2_resolve_identifier(cxt, p->value.string_value);
                    if (rc != SUCCESS) {
                        return rc;
                    }
                }
            }

            if (out_type) {
                data_type ret = ST_UNKNOWN;
                char key[256];
                make_function_key(key, sizeof key, name, ar);
                st_data *fn = st_get(cxt->funcs, (char *)key);
                if (fn) {
                    ret = fn->data_type;
                }
                *out_type = ret;
            }

            return SUCCESS;
        }

        case AST_NOT:
        case AST_NOT_NULL: {
           //printf("[sem2][EXPR] → unary\n");
            data_type inner;
            int rc = sem2_visit_expr(cxt, e->operands.unary_op.expression, &inner);
            if (rc != SUCCESS) {
                return rc;
            }

            if (out_type) {
                *out_type = ST_BOOL;
            }
            return SUCCESS;
        }

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
        case AST_CONCAT: {
           //printf("[sem2][EXPR] → binary op=%d\n", e->type);

            data_type lt, rt;
            int rc = sem2_visit_expr(cxt, e->operands.binary_op.left, &lt);
            if (rc != SUCCESS) {
                return rc;
            }

            rc = sem2_visit_expr(cxt, e->operands.binary_op.right, &rt);
            if (rc != SUCCESS) {
                return rc;
            }

            int lhs_unknownish = (lt == ST_UNKNOWN || lt == ST_VOID || sem_is_unknown_type(lt));
            int rhs_unknownish = (rt == ST_UNKNOWN || rt == ST_VOID || sem_is_unknown_type(rt));

            if (lhs_unknownish || rhs_unknownish) {
                if (out_type) {
                    if (e->type == AST_EQUALS || e->type == AST_NOT_EQUAL || e->type == AST_LT || e->type == AST_LE || e->type == AST_GT || e->type == AST_GE || e->type == AST_AND || e->type == AST_OR) {
                        *out_type = ST_BOOL;
                    } else {
                        *out_type = ST_UNKNOWN;
                    }
                }
                return SUCCESS;
            }

            switch (e->type) {
                case AST_ADD:
                    if (sem_is_numeric_type(lt) && sem_is_numeric_type(rt)) {
                        if (out_type) {
                            *out_type = sem_unify_numeric_type(lt, rt);
                        }
                        return SUCCESS;
                    }
                    if (sem_is_string_type(lt) && sem_is_string_type(rt)) {
                        if (out_type) {
                            *out_type = ST_STRING;
                        }
                        return SUCCESS;
                    }
                    return error(ERR_EXPR, "invalid operands for '+'");

                case AST_SUB:
                case AST_DIV:
                    if (sem_is_numeric_type(lt) && sem_is_numeric_type(rt)) {
                        if (out_type) {
                            *out_type = sem_unify_numeric_type(lt, rt);
                        }
                        return SUCCESS;
                    }
                    return error(ERR_EXPR, "invalid operands for arithmetic operator");

                case AST_MUL:
                    if (sem_is_numeric_type(lt) && sem_is_numeric_type(rt)) {
                        if (out_type) {
                            *out_type = sem_unify_numeric_type(lt, rt);
                        }
                        return SUCCESS;
                    }
                    if ((sem_is_string_type(lt) && rt == ST_INT) || (sem_is_string_type(rt) && lt == ST_INT)) {
                        if (out_type) {
                            *out_type = ST_STRING;
                        }
                        return SUCCESS;
                    }
                    return error(ERR_EXPR, "invalid operands for '*'");

                case AST_CONCAT:
                    if (sem_is_string_type(lt) && sem_is_string_type(rt)) {
                        if (out_type) {
                            *out_type = ST_STRING;
                        }
                        return SUCCESS;
                    }
                    return error(ERR_EXPR, "invalid operands for concat operator");

                case AST_LT:
                case AST_LE:
                case AST_GT:
                case AST_GE:
                    if (sem_is_numeric_type(lt) && sem_is_numeric_type(rt)) {
                        if (out_type) {
                            *out_type = ST_BOOL;
                        }
                        return SUCCESS;
                    }
                    return error(ERR_EXPR, "relational operators require numeric operands");

                case AST_EQUALS:
                case AST_NOT_EQUAL:
                    if (out_type) {
                        *out_type = ST_BOOL;
                    }
                    return SUCCESS;

                case AST_AND:
                case AST_OR:
                    if (sem_is_bool_type(lt) && sem_is_bool_type(rt)) {
                        if (out_type) {
                            *out_type = ST_BOOL;
                        }
                        return SUCCESS;
                    }
                    return error(ERR_EXPR, "logical operators require bool operands");

                default:
                    break;
            }

            return SUCCESS;
        }

        case AST_TERNARY: {
           //printf("[sem2][EXPR] → TERNARY\n");
            data_type lt, rt;
            int rc = sem2_visit_expr(cxt, e->operands.binary_op.left, &lt);
            if (rc != SUCCESS) {
                return rc;
            }
            rc = sem2_visit_expr(cxt, e->operands.binary_op.right, &rt);
            if (rc != SUCCESS) {
                return rc;
            }
            if (out_type) {
                *out_type = ST_UNKNOWN;
            }
            return SUCCESS;
        }

        case AST_IS: {
           //printf("[sem2][EXPR] → IS\n");

            data_type lhs_type;
            int rc = sem2_visit_expr(cxt, e->operands.binary_op.left, &lhs_type);
            if (rc != SUCCESS) {
                return rc;
            }

            ast_expression rhs = e->operands.binary_op.right;
            const char *type_name = NULL;

            if (rhs && rhs->type == AST_IDENTIFIER && rhs->operands.identifier.value) {
                type_name = rhs->operands.identifier.value;
            }

            if (!type_name) {
                return error(ERR_EXPR, "invalid right-hand side of 'is' operator");
            }

            if (strcmp(type_name, "Num") != 0 && strcmp(type_name, "String") != 0 && strcmp(type_name, "Null") != 0) {
                return error(ERR_EXPR, "invalid type '%s' on right-hand side of 'is' (expected Num, String or Null)", type_name);
            }

           //printf("[sem2][EXPR] → IS '%s' OK\n", type_name);

            if (out_type) {
                *out_type = ST_BOOL;
            }
            return SUCCESS;
        }

        case AST_NONE:
        case AST_NIL:
           //printf("[sem2][EXPR] → NIL/NONE\n");
            if (out_type) {
                *out_type = ST_NULL;
            }
            return SUCCESS;

        default:
           //printf("[sem2][EXPR] → unhandled type\n");
            return SUCCESS;
    }
}

/* -------------------------------------------------------------------------
 *  Statement visitor (Pass 2)
 * ------------------------------------------------------------------------- */

/**
 * @brief Visits a statement node in Pass 2.
 *
 * The visitor dispatches on node type, performing identifier resolution,
 * function-call checks, and type learning for assignments.
 *
 * @param table Semantic context.
 * @param node AST node.
 * @return SUCCESS or an error code.
 */
static int sem2_visit_statement_node(semantic *table, ast_node node) {
    if (!node) {
        return SUCCESS;
    }

   //printf("[sem2][STMT] Node type=%d at scope=%s\n", node->type, sem_scope_ids_current(&table->ids));

    switch (node->type) {
        case AST_BLOCK:
           //printf("[sem2][STMT] → BLOCK\n");
            return sem2_visit_block(table, node->data.block);

        case AST_CONDITION: {
           //printf("[sem2][STMT] → IF condition\n");
            data_type cond_type = ST_UNKNOWN;

            if (node->data.condition.condition) {
                int rc = sem2_visit_expr(table, node->data.condition.condition, &cond_type);
                if (rc != SUCCESS) {
                    return rc;
                }
            }

           //printf("[sem2][STMT] → IF branch\n");
            int rc = sem2_visit_block(table, node->data.condition.if_branch);
            if (rc != SUCCESS) {
                return rc;
            }

           //printf("[sem2][STMT] → ELSE branch\n");
            return sem2_visit_block(table, node->data.condition.else_branch);
        }

        case AST_WHILE_LOOP: {
           //printf("[sem2][STMT] → WHILE\n");
            data_type cond_type = ST_UNKNOWN;

            if (node->data.while_loop.condition) {
                int rc = sem2_visit_expr(table, node->data.while_loop.condition, &cond_type);
                if (rc != SUCCESS) {
                    return rc;
                }
            }

            return sem2_visit_block(table, node->data.while_loop.body);
        }

        case AST_EXPRESSION: {
           //printf("[sem2][STMT] → EXPRESSION\n");
            data_type dummy = ST_UNKNOWN;
            if (!node->data.expression) {
                return SUCCESS;
            }
            return sem2_visit_expr(table, node->data.expression, &dummy);
        }

        case AST_VAR_DECLARATION: {
            const char *name = node->data.declaration.name;
            if (!name) {
                return error(ERR_INTERNAL, "variable declaration without name in Pass 2");
            }

            if (!scopes_declare_local(&table->scopes, name, true)) {
                return error(ERR_REDEF, "variable '%s' already declared in this scope", name);
            }

            st_data *sym = scopes_lookup(&table->scopes, name);
            if (!sym) {
                return error(ERR_INTERNAL, "scope lookup failed for '%s'", name);
            }
            // creating cg_name
            
            char scope_raw[64];
            const char *scope_src = sem_scope_ids_current(&table->ids);
            size_t scope_len = strlen(scope_src);
            if (scope_len >= sizeof(scope_raw)) {
                scope_len = sizeof(scope_raw) - 1;
            }
            memcpy(scope_raw, scope_src, scope_len);
            scope_raw[scope_len] = '\0';

            char scope_clean[64];
            int j = 0;
            for (int i = 0; scope_raw[i] != '\0'; i++) {
                if (scope_raw[i] != '.') {
                    if (j + 1 >= (int)sizeof scope_clean -1) {
                        break;
                    }
                    scope_clean[j++] = scope_raw[i];
                }
            }
            scope_clean[j] = '\0';

            char final[128];
            size_t max_total = sizeof(final) - 1;
            size_t pos = 0;

            size_t name_len = strlen(name);
            if (name_len > max_total) {
                name_len = max_total;
            }
            memcpy(final + pos, name, name_len);
            pos += name_len;

            if (pos < max_total) {
                final[pos++] = '_';
            }

            size_t scope_clean_len = strlen(scope_clean);
            size_t remaining = max_total - pos;
            if (scope_clean_len > remaining) {
                scope_clean_len = remaining;
            }
            if (scope_clean_len > 0) {
                memcpy(final + pos, scope_clean, scope_clean_len);
                pos += scope_clean_len;
            }

            final[pos] = '\0';

            if (node->data.declaration.cg_name) {
            free(node->data.declaration.cg_name);
             }
            node->data.declaration.cg_name = my_strdup(final);
            if (!node->data.declaration.cg_name) {
            return error(ERR_INTERNAL, "memory allocation failed for cg_name");
        }
            sym->decl_node = node;

           //printf("[sem2] new local '%s' -> cg_name='%s'\n", name, node->data.declaration.cg_name ? node->data.declaration.cg_name : "(null)");

            return SUCCESS;
        }

        case AST_ASSIGNMENT: {
            const char *lhs = node->data.assignment.name;

           //printf("[sem2] ASSIGN → %s (scope=%s)\n", lhs ? lhs : "(null)", sem_scope_ids_current(&table->ids));

            if (!lhs) {
                return error(ERR_INTERNAL, "assignment without LHS name");
            }

            int is_magic = is_magic_global_identifier(lhs);

            if (!is_magic) {
                int rc = sem2_resolve_identifier(table, lhs);
                if (rc != SUCCESS) {
                    return rc;
                }
            }

            data_type rhs_type = ST_UNKNOWN;
            int rc = sem2_visit_expr(table, node->data.assignment.value, &rhs_type);
            if (rc != SUCCESS) {
                return rc;
            }

            if (is_magic) {
                sem_magic_global_type_learn(lhs, rhs_type);
            } else {
                st_data *sym = scopes_lookup(&table->scopes, lhs);
                //cg_name  
                if (sym && sym->decl_node && sym->decl_node->type == AST_VAR_DECLARATION){
                    
                    const char *decl_cg = sym->decl_node->data.declaration.cg_name;
                    if (decl_cg) {
                        
                        if (node->data.assignment.cg_name) {
                            free(node->data.assignment.cg_name);
                        }
                        
                        node->data.assignment.cg_name = my_strdup(decl_cg);
                        if (!node->data.assignment.cg_name) {
                            return error(ERR_INTERNAL, "memory allocation failed for assignment cg_name");
                        }
                    }
                }
                if (sym && (sym->symbol_type == ST_VAR || sym->symbol_type == ST_PAR)) {
                    data_type old_t = sym->data_type;
                    data_type new_t = old_t;

                    if (!sem_is_unknown_type(rhs_type) && rhs_type != ST_VOID) {
                        if (sem_is_unknown_type(old_t) || old_t == ST_VOID || old_t == ST_NULL) {
                            new_t = rhs_type;
                        } else if (sem_is_numeric_type(old_t) && sem_is_numeric_type(rhs_type)) {
                            new_t = sem_unify_numeric_type(old_t, rhs_type);
                        } else if (old_t == rhs_type) {
                            new_t = old_t;
                        } else {
                            new_t = ST_UNKNOWN;
                        }

                        sym->data_type = new_t;
                       //printf("[sem2][ASSIGN] '%s' type old=%d new=%d\n", lhs, old_t, sym->data_type);
                    }
                }
            }

            return SUCCESS;
        }

        case AST_FUNCTION: {
           //printf("[sem2][STMT] → FUNCTION\n");

            ast_function fn = node->data.function;
            if (!fn) {
                return SUCCESS;
            }

            sem_scope_enter_block(table);
           //printf("[sem2][FUNC] scope=%s\n", sem_scope_ids_current(&table->ids));

            int rc = declare_parameter_list_in_current_scope(table, fn->parameters);
            if (rc != SUCCESS) {
                sem_scope_leave_block(table, "function params");
                return rc;
            }
            //cg_name
            const char *scope_str = sem_scope_ids_current(&table->ids);
            char clean[64];
            int j = 0;
            for (int i = 0; scope_str[i] != '\0'; i++) {
                if (scope_str[i] != '.' && j < 63)
                    clean[j++] = scope_str[i];
            }
            clean[j] = '\0';

            for (ast_parameter p = fn->parameters; p; p = p->next) {

                if (p->value_type != AST_VALUE_IDENTIFIER)
                    continue;

                const char *pname = p->value.string_value;
                if (!pname)
                    continue;

                st_data *sym = scopes_lookup(&table->scopes, pname);
                if (!sym || !sym->decl_node)
                    continue;

               
                char final[128];
                snprintf(final, sizeof(final), "%s_%s", pname, clean);

            
                p->cg_name = my_strdup(final);

              
                sym->decl_node->data.declaration.cg_name = p->cg_name;

                //printf("[sem2] param '%s' -> cg_name='%s'\n", pname, p->cg_name);
            }

            if (fn->code) {
                for (ast_node stmt = fn->code->first; stmt; stmt = stmt->next) {
                    rc = sem2_visit_statement_node(table, stmt);
                    if (rc != SUCCESS) {
                        sem_scope_leave_block(table, "function body");
                        return rc;
                    }
                }
            }

            sem_scope_leave_block(table, "function body");
            return SUCCESS;
        }

        case AST_GETTER: {
           //printf("[sem2][STMT] → GETTER\n");

            ast_block body = node->data.getter.body;

            sem_scope_enter_block(table);
           //printf("[sem2][GETTER] scope=%s\n", sem_scope_ids_current(&table->ids));

            int rc = SUCCESS;
            if (body) {
                for (ast_node stmt = body->first; stmt; stmt = stmt->next) {
                    rc = sem2_visit_statement_node(table, stmt);
                    if (rc != SUCCESS) {
                        sem_scope_leave_block(table, "getter body");
                        return rc;
                    }
                }
            }

            sem_scope_leave_block(table, "getter body");
            return SUCCESS;
        }

        case AST_SETTER: {
           //printf("[sem2][STMT] → SETTER\n");

            const char *param_name = node->data.setter.param;
            ast_block body = node->data.setter.body;

            sem_scope_enter_block(table);
           //printf("[sem2][SETTER] scope=%s\n", sem_scope_ids_current(&table->ids));

            if (param_name) {
               //printf("[sem2][SETTER] declare param '%s'\n", param_name);

                if (!scopes_declare_local(&table->scopes, param_name, true)) {
                    sem_scope_leave_block(table, "setter header");
                    return error(ERR_REDEF, "setter parameter redeclared: %s", param_name);
                }

                st_data *param_data = scopes_lookup_in_current(&table->scopes, param_name);
                if (param_data) {
                    param_data->symbol_type = ST_PAR;
                }
            }

            int rc = SUCCESS;
            if (body) {
                for (ast_node stmt = body->first; stmt; stmt = stmt->next) {
                    rc = sem2_visit_statement_node(table, stmt);
                    if (rc != SUCCESS) {
                        sem_scope_leave_block(table, "setter body");
                        return rc;
                    }
                }
            }

            sem_scope_leave_block(table, "setter body");
            return SUCCESS;
        }

        case AST_CALL_FUNCTION: {
           //printf("[sem2][STMT] → CALL_FUNCTION\n");

            ast_fun_call call = node->data.function_call;
            if (!call) {
                return SUCCESS;
            }

            int ar = count_parameters(call->parameters);
            int rc = sem2_check_function_call(table, call->name, ar);
            if (rc != SUCCESS) {
                return rc;
            }

            for (ast_parameter p = call->parameters; p; p = p->next) {
                if (p->value_type == AST_VALUE_IDENTIFIER) {
                    rc = sem2_resolve_identifier(table, p->value.string_value);
                    if (rc != SUCCESS) {
                        return rc;
                    }
                }
            }
            return SUCCESS;
        }

        case AST_RETURN: {
           //printf("[sem2][STMT] → RETURN\n");
            data_type ret_type = ST_VOID;

            if (node->data.return_expr.output) {
                return sem2_visit_expr(table, node->data.return_expr.output, &ret_type);
            }

            return SUCCESS;
        }

        case AST_BREAK:
        case AST_CONTINUE:
           //printf("[sem2][STMT] → BREAK/CONTINUE (ignored in Pass 2)\n");
            return SUCCESS;

        case AST_IFJ_FUNCTION: {
           //printf("[sem2][STMT] → IFJ_FUNCTION\n");
            ast_ifj_function call = node->data.ifj_function;
            if (!call) {
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

            int rc = sem2_check_function_call(table, name, ar);
            if (rc != SUCCESS) {
                return rc;
            }
            //cg_name
            for (ast_parameter p = call->parameters; p; p = p->next) {

    // Only IDENTIFIER parameters have a name!
    if (p->value_type != AST_VALUE_IDENTIFIER)
        continue;

    const char *pname = p->value.string_value;
    if (!pname) 
        continue;

    
    st_data *sym = scopes_lookup(&table->scopes, pname);
    if (!sym) 
        continue;

    
    const char *scope_str = sem_scope_ids_current(&table->ids);

    char clean[64];
    int j = 0;
    for (int i = 0; scope_str[i] != '\0'; i++) {
        if (scope_str[i] != '.' && j < 63)
            clean[j++] = scope_str[i];
    }
    clean[j] = '\0';

    char final[128];
    snprintf(final, sizeof(final), "%s_%s", pname, clean);

    p->cg_name = my_strdup(final);
}
    
            for (ast_parameter p = call->parameters; p; p = p->next) {
                if (p->value_type == AST_VALUE_IDENTIFIER) {
                    rc = sem2_resolve_identifier(table, p->value.string_value);
                    if (rc != SUCCESS) {
                        return rc;
                    }
                }
            }
            return SUCCESS;
        }
    }
    
   //printf("[sem2][STMT] → unhandled\n");
    return SUCCESS;
}

/* -------------------------------------------------------------------------
 *  Block visitor (Pass 2)
 * ------------------------------------------------------------------------- */

/**
 * @brief Visits a block in Pass 2, managing scope entry/exit and visiting all statements.
 *
 * @param table Semantic context.
 * @param blk Block node.
 * @return SUCCESS or an error code.
 */
static int sem2_visit_block(semantic *table, ast_block blk) {
    if (!blk) {
        return SUCCESS;
    }

   //printf("[sem2][BLK] ENTER block (current scope=%s)\n", sem_scope_ids_current(&table->ids));

    sem_scope_enter_block(table);

   //printf("[sem2][BLK] NEW scope=%s\n", sem_scope_ids_current(&table->ids));

    for (ast_node n = blk->first; n; n = n->next) {
       //printf("[sem2][BLK] Visiting node...\n");
        int rc = sem2_visit_statement_node(table, n);
        if (rc != SUCCESS) {
           //printf("[sem2][BLK] ERROR inside block\n");
            sem_scope_leave_block(table, "sem2_visit_block");
            return rc;
        }
    }

   //printf("[sem2][BLK] LEAVE scope=%s\n", sem_scope_ids_current(&table->ids));

    sem_scope_leave_block(table, "sem2_visit_block");
    return SUCCESS;
}

/* -------------------------------------------------------------------------
 *  Pass 2 entry point
 * ------------------------------------------------------------------------- */

/**
 * @brief Entry point for Pass 2 semantic analysis.
 *
 * The function reinitializes scopes and scope IDs, then traverses all class
 * root blocks, performing identifier resolution, call checking and type learning.
 *
 * @param table Semantic context initialized by Pass 1.
 * @param syntax_tree AST root.
 * @return SUCCESS or the first error encountered.
 */
int semantic_pass2(semantic *table, ast syntax_tree) {
   //printf("[sem2] =========================================\n");
   //printf("[sem2] Starting Pass 2\n");
   //printf("[sem2] =========================================\n");

    scopes_init(&table->scopes);
    sem_scope_ids_init(&table->ids);
    table->loop_depth = 0;

    for (ast_class c = syntax_tree->class_list; c; c = c->next) {
        ast_block root = get_class_root_block(c);
       //printf("[sem2] CLASS → '%s'\n", c->name ? c->name : "(anonymous)");

        if (!root) {
           //printf("[sem2]   (no root block)\n");
            continue;
        }

        int rc = sem2_visit_block(table, root);
        if (rc != SUCCESS) {
           //printf("[sem2] Pass 2 FAILED\n");
            return rc;
        }
    }

   //printf("[sem2] Pass 2 completed successfully.\n");
    return SUCCESS;
}

/* =========================================================================
 *      Public API: export list of magic globals ("__name") for codegen
 * ========================================================================= */

/**
 * @brief Returns a deep copy of all magic global names for the code generator.
 *
 * The function allocates an array of char* and copies each magic-global name.
 * The caller is responsible for freeing all strings and the array.
 *
 * @param out_globals Output pointer to the allocated array of strings.
 * @param out_count Output pointer to the number of strings.
 * @return SUCCESS or ERR_INTERNAL on allocation failure.
 */
int semantic_get_magic_globals(char ***out_globals, size_t *out_count) {
    if (!out_globals || !out_count) {
        return error(ERR_INTERNAL, "semantic_get_magic_globals: NULL output pointer");
    }

    if (g_magic_globals.count == 0) {
        *out_globals = NULL;
        *out_count = 0;
        return SUCCESS;
    }

    char **copy = malloc(g_magic_globals.count * sizeof(char *));
    if (!copy) {
        return error(ERR_INTERNAL, "semantic_get_magic_globals: allocation failed");
    }

    for (size_t i = 0; i < g_magic_globals.count; ++i) {
        size_t len = strlen(g_magic_globals.items[i]);
        copy[i] = malloc(len + 1);
        if (!copy[i]) {
            for (size_t j = 0; j < i; ++j) {
                free(copy[j]);
            }
            free(copy);
            return error(ERR_INTERNAL, "semantic_get_magic_globals: allocation failed (string)");
        }
        memcpy(copy[i], g_magic_globals.items[i], len + 1);
    }

    *out_globals = copy;
    *out_count = g_magic_globals.count;
    return SUCCESS;
}
