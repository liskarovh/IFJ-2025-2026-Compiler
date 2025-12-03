/**
* @file semantic.c
 * @brief Two-pass semantic analysis for the IFJ25 compiler.
 *
 * @details
 *   - Pass 1:
 *       -  built-in functions into the global function table,
 *       - collects function / getter / setter headers,
 *       - verifies main() with zero parameters,
 *       - declares locals, checks redeclarations etc
 *   - Pass 2:
 *       - resolves identifiers,
 *       - checks function calls,
 *       - type check for expressions
 *
 * @authors
 *  - Hana Liškařová (xliskah00)
 *  - Maťej Kurta (xkurtam00)
 */

#include <stdio.h>
#include <string.h>   // strlen, memcpy, strncmp, strcmp, strstr, strchr
#include <stdbool.h>
#include <stdlib.h>   // NULL, malloc, free, qsort, memset

#include "semantic.h"
#include "builtins.h"
#include "error.h"
#include "string.h"
#include "symtable.h"

/* Forward declaration*/
int semantic_pass2(semantic *table, ast syntax_tree);

/* =========================================================================
 *                      Type helper predicates
 * ========================================================================= */
/**
 * @brief Returns true given data_type is ST_UNKNOWN.
 */
static bool sem_is_unknown_type(data_type t) {
    return t == ST_UNKNOWN;
}

/**
 * @brief Returns true given data_type is numeric (ST_INT or ST_DOUBLE).
 */
static bool sem_is_numeric_type(data_type t) {
    return t == ST_INT || t == ST_DOUBLE;
}

/**
 * @brief Returns true if given data_type is a string.
 */
static bool sem_is_string_type(data_type t) {
    return t == ST_STRING;
}

/**
 * @brief Returns true if given data_type is a boolean.
 */
static bool sem_is_bool_type(data_type t) {
    return t == ST_BOOL;
}

/**
 * @brief Returns a unified numeric type for two numeric operands.
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
 *          Global "__" registry for code generator
 * ========================================================================= */

/**
 * @brief Registry storing all global names ("__name").
 */
typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} sem_globals;

//Global global registry instance
static sem_globals g_globals = {NULL, 0, 0};

/**
 * @brief Registry entry for learned global types.
 */
typedef struct {
    char *name;
    data_type type;
} sem_global_type;

/* Global learned type registry instance. */
static sem_global_type *g_global_types = NULL;
static size_t g_global_types_count = 0;
static size_t g_global_types_cap = 0;

/**
 * @brief Resets the learned type registry for globals.
 */
static void sem_global_types_reset(void) {
    if (g_global_types) {
        for (size_t i = 0; i < g_global_types_count; ++i) {
            free(g_global_types[i].name);
        }
        free(g_global_types);
    }
    g_global_types = NULL;
    g_global_types_count = 0;
    g_global_types_cap = 0;
}

/**
 * @brief Returns the current learned type of a global variable.
 * @param name  Global name.
 * @return Learned data_type or ST_UNKNOWN.
 */
static data_type sem_global_type_get(const char *name) {
    for (size_t i = 0; i < g_global_types_count; ++i) {
        if (strcmp(g_global_types[i].name, name) == 0) {
            data_type t = g_global_types[i].type;
            if (t == ST_VOID || sem_is_unknown_type(t)) {
                return ST_UNKNOWN;
            }
            return t;
        }
    }
    return ST_UNKNOWN;
}

/**
 * @brief Learns the type of a global based on an assignment.
 * @param name Global name.
 * @param rhs_type Data type of the right expression.
 */
static void sem_global_type_learn(const char *name, data_type rhs_type) {
    if (rhs_type == ST_VOID || sem_is_unknown_type(rhs_type)) {
        return;
    }

    // search for existing entry and update type
    for (size_t i = 0; i < g_global_types_count; ++i) {
        if (strcmp(g_global_types[i].name, name) == 0) {
            data_type old_t = g_global_types[i].type;
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

            g_global_types[i].type = new_t;
            return;
        }
    }

    // ggrow the registry array
    if (g_global_types_count == g_global_types_cap) {
        size_t new_cap = g_global_types_cap ? g_global_types_cap * 2 : 8;
        sem_global_type *new_arr = realloc(g_global_types, new_cap * sizeof *new_arr);
        if (!new_arr) {
            return;
        }
        g_global_types = new_arr;
        g_global_types_cap = new_cap;
    }

    // creates a copy of the global
    size_t len = strlen(name);
    char *copy = malloc(len + 1);
    if (!copy) {
        return;
    }
    memcpy(copy, name, len + 1);

    // stores the new entry with learned type
    g_global_types[g_global_types_count].name = copy;
    g_global_types[g_global_types_count].type = rhs_type;
    g_global_types_count++;
}

/**
 * @brief Resets the global name registry.
 */
static void sem_globals_reset(void) {
    if (g_globals.items) {
        for (size_t i = 0; i < g_globals.count; ++i) {
            free(g_globals.items[i]);
        }
        free(g_globals.items);
    }
    g_globals.items = NULL;
    g_globals.count = 0;
    g_globals.capacity = 0;
}

/**
 * @brief Inserts a global name into the registry if not already present.
 * @param name Global name to insert.
 * @return SUCCESS on success, ERR_INTERNAL on allocation failure.
 */
static int sem_globals_add(const char *name) {
    // ignore identifiers that are not globals
    if (name[0] != '_' || name[1] != '_') {
        return SUCCESS;
    }
    for (size_t i = 0; i < g_globals.count; ++i) {
        if (strcmp(g_globals.items[i], name) == 0) {
            return SUCCESS;
        }
    }

    // grow array if full
    if (g_globals.count == g_globals.capacity) {
        size_t new_cap = (g_globals.capacity == 0) ? 8 : g_globals.capacity * 2;
        char **new_items = realloc(g_globals.items, new_cap * sizeof(char *));
        if (!new_items) {
            return error(ERR_INTERNAL, "semantic: failed to grow  globals array");
        }
        g_globals.items = new_items;
        g_globals.capacity = new_cap;
    }

    // copy name into array
    size_t len = strlen(name);
    char *copy = malloc(len + 1);
    if (!copy) {
        return error(ERR_INTERNAL, "semantic: failed to allocate  global name");
    }
    memcpy(copy, name, len + 1);
    g_globals.items[g_globals.count++] = copy;
    return SUCCESS;
}

/* =========================================================================
 *                  Numeric and string helpers
 * ========================================================================= */

/**
 * @brief Converts an unsigned integer to its decimal representation.
 *
 * @param buffer Target buffer.
 * @param buffer_size Size of the target buffer.
 * @param value Value to convert.
 */
static void sem_uint_to_dec(char *buffer, size_t buffer_size, unsigned int value) {
    char temp[16];
    size_t pos = sizeof(temp) - 1;
    temp[pos] = '\0';
    // fill temp from the end with decimal digits
    do {
        if (pos == 0) {
            break;
        }
        temp[--pos] = (char) ('0' + (value % 10));
        value /= 10;
    } while (value > 0);

    const char *digits = &temp[pos];
    size_t len = strlen(digits);
    if (len >= buffer_size) {
        len = buffer_size - 1;
    }
    // copy digits into buffer and terminate
    memcpy(buffer, digits, len);
    buffer[len] = '\0';
}

/**
 * @brief Builds a flattened scope suffix by stripping dots from scope_str.
 */
static void sem_build_scope_suffix(const char *scope_str, char *buffer, size_t buffer_size) {
    size_t pos = 0;
    if (scope_str) {
        for (size_t i = 0; scope_str[i] != '\0'; ++i) {
            char c = scope_str[i];
            if (c == '.') {
                continue;
            }
            if (pos + 1 >= buffer_size) {
                break;
            }
            buffer[pos++] = c; // copy non-dot character
        }
    }
    buffer[pos] = '\0'; // terminate flattened scope string
}

/**
 * @brief Builds "<name>_<flat_scope>" into buffer (null-terminated).
 */
static void sem_build_cg_name(char *buffer, size_t buffer_size, const char *name, const char *scope_str) {
    buffer[0] = '\0';
    if (!name) {
        return; // leave buffer empty when name is missing
    }

    size_t max_total = buffer_size - 1;
    size_t pos = 0;
    // copy base name
    size_t name_len = strlen(name);
    if (name_len > max_total) {
        name_len = max_total;
    }
    memcpy(buffer + pos, name, name_len);
    pos += name_len;

    if (pos < max_total) {
        buffer[pos++] = '_'; // add separator before flattened scope

        char scope_clean[64];
        sem_build_scope_suffix(scope_str, scope_clean, sizeof scope_clean);

        // append flattened scope, truncated if needed
        size_t scope_len = strlen(scope_clean);
        size_t remaining = max_total - pos;
        if (scope_len > remaining) {
            scope_len = remaining;
        }
        if (scope_len > 0) {
            memcpy(buffer + pos, scope_clean, scope_len);
            pos += scope_len;
        }
    }
    buffer[pos] = '\0'; // terminate final cg name
}

/* =========================================================================
 *                  Scope-ID stack helpers
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
    current_frame->child_count = 0; // reset child count for new frame
    const char *parent_path = parent_frame->path;
    size_t parent_length = strlen(parent_path);
    if (parent_length >= SEM_MAX_SCOPE_PATH) {
        parent_length = SEM_MAX_SCOPE_PATH - 1; // parent length to buffer size
    }

    size_t pos = 0;
    if (parent_length > 0) {
        memcpy(current_frame->path, parent_path, parent_length); // copy parent scope path
        pos = parent_length;
    }

    if (pos + 1 < SEM_MAX_SCOPE_PATH) {
        current_frame->path[pos++] = '.'; // append dot separator
        current_frame->path[pos] = '\0';
        char index_buffer[16];
        sem_uint_to_dec(index_buffer, sizeof index_buffer, (unsigned int) child_index);
        size_t index_length = strlen(index_buffer);
        size_t remaining = SEM_MAX_SCOPE_PATH - 1 - pos;
        if (index_length > remaining) {
            index_length = remaining; // index length to remaining space
        }
        if (index_length > 0) {
            memcpy(current_frame->path + pos, index_buffer, index_length); // append index digits
            pos += index_length;
        }
    }
    current_frame->path[pos] = '\0'; // terminate final scope path string
    scope_id_stack->depth = new_depth; // move depth to new child frame
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
 *                             Key builders, parameter helpers
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
    const char *name_text = function_name ? function_name : "(null)";
    size_t max_total = buffer_size - 1; // keep space for terminator
    size_t pos = 0;
    size_t name_len = strlen(name_text);
    if (name_len > max_total) {
        name_len = max_total;
    }
    memcpy(buffer + pos, name_len ? name_text : "", name_len); // copy function name part
    pos += name_len;

    if (pos < max_total) {
        buffer[pos++] = '#'; // append separator for arity
        char number_buffer[32];
        sem_uint_to_dec(number_buffer, sizeof number_buffer, (unsigned int) arity); // convert arity to text
        size_t num_len = strlen(number_buffer);
        size_t remaining = max_total - pos;
        if (num_len > remaining) {
            num_len = remaining;
        }
        memcpy(buffer + pos, num_len ? number_buffer : "", num_len); // append arity digits
        pos += num_len;
    }
    buffer[pos] = '\0'; // terminate result string
}

/**
 * @brief Composes a key for "any overload" as "@name".
 *
 * @param buffer Output buffer.
 * @param buffer_size Size of output buffer.
 * @param function_name Base function name.
 */
static void make_function_any_key(char *buffer, size_t buffer_size, const char *function_name) {
    const char *name_text = function_name;
    size_t max_total = buffer_size - 1; // keep space for terminator
    size_t pos = 0;

    if (pos < max_total) {
        buffer[pos++] = '@'; // prefix key with '@' to mark any overload
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

    buffer[pos] = '\0'; // terminate result string
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
    const char *prefix = is_setter ? "set" : "get"; // pick prefix based on accessor kind
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
        buffer[pos++] = ':'; // append separator between prefix and base name
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
 * @brief Counts the number of parameters in ast_parameter list.
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
 * @brief Returns true if an identifier name matches the  global pattern "__name".
 *
 * @param identifier_name Identifier name to check.
 * @return True if the name is a  global.
 */
static bool is_global_identifier(const char *identifier_name) {
    return identifier_name && identifier_name[0] == '_' && identifier_name[1] == '_';
}

/**
 * @brief Returns true if an accessor (getter/setter) with given base name exists.
 *
 * @param semantic_table Semantic table.
 * @param base_name Base accessor name.
 * @param is_setter True for setter, false for getter.
 * @return True if such accessor exists in the function table.
 */
static bool sem_has_accessor(semantic *semantic_table, const char *base_name, bool is_setter) {
    if (!semantic_table->funcs) {
        return false;
    }
    char key[256];
    make_accessor_key(key, sizeof key, base_name, is_setter);
    return st_find(semantic_table->funcs, key) != NULL;
}

/**
 * @brief checks the left side of an assignment.
 * @param semantic_table semantic context.
 * @param name identifier on the left-hand side.
 * @return SUCCESS or an error code (ERR_DEF, ERR_INTERNAL).
 */
static int sem_check_assignment_lhs(semantic *semantic_table, const char *name) {
    if (!semantic_table || !name) {
        return SUCCESS;
    }
    if (scopes_lookup(&semantic_table->scopes, name)) {
        return SUCCESS; // local variable or parameter already declared
    }
    if (sem_has_accessor(semantic_table, name, true)) {
        return SUCCESS; // setter allowed as assignment target
    }
    if (is_global_identifier(name)) {
        int rc = sem_globals_add(name); // register global for later codegen
        if (rc != SUCCESS) {
            return rc;
        }
        return SUCCESS;
    }
    return error(ERR_DEF, "assignment to undefined local variable '%s'", name);
}

/* =========================================================================
 *                        Function-table operations
 * ========================================================================= */
static int collect_headers_from_block(semantic *semantic_table, ast_block block_node, const char *class_scope_name);

/**
 * @brief inserts a user function signature (name, arity) into the function table.
 * duplicate handling inside one class:
 *  - same (name, arity) and same class_scope_name -> ERR_REDEF (4),
 *  - same (name, arity) in different classes -> allowed.
 * @param semantic_table semantic context.
 * @param function_name function name.
 * @param arity number of parameters.
 * @param class_scope_name name of the class scope.
 * @return SUCCESS or an error code (ERR_REDEF, ERR_INTERNAL).
 */
static int function_table_insert_signature(semantic *semantic_table, const char *function_name, int arity, const char *class_scope_name) {
    char function_key[256];
    make_function_key(function_key, sizeof function_key, function_name, arity); // build key "name#arity"

    // search existing signature
    st_data *existing = st_get(semantic_table->funcs, function_key);
    if (existing) {
        const char *existing_scope = NULL;
        if (existing->scope_name && existing->scope_name->data && existing->scope_name->length > 0) {
            existing_scope = existing->scope_name->data;
        }

        // reject duplicate in same class
        if (existing_scope && class_scope_name && strcmp(existing_scope, class_scope_name) == 0) {
            return error(ERR_REDEF, "duplicate function signature %s in class '%s'", function_key, existing_scope);
        }
        // allow same (name,arity) in other classes
        return SUCCESS;
    }

    // insert new function entry
    st_insert(semantic_table->funcs, function_key, ST_FUN, true);
    st_data *function_data = st_get(semantic_table->funcs, function_key);
    if (!function_data) {
        return error(ERR_INTERNAL, "failed to store function signature: %s", function_key);
    }

    function_data->symbol_type = ST_FUN;
    function_data->param_count = arity;
    function_data->defined = false;
    function_data->global = true;

    // store class scope name
    if (class_scope_name) {
        function_data->scope_name = string_create(0);
        if (!function_data->scope_name ||
            !string_append_literal(function_data->scope_name, (char *) class_scope_name)) {
            return error(ERR_INTERNAL, "failed to store function scope_name for '%s'", function_name ? function_name : "(null)");
        }
    } else {
        function_data->scope_name = NULL;
    }

    // store function base name as ID
    if (function_name) {
        function_data->ID = string_create(0);
        if (!function_data->ID ||
            !string_append_literal(function_data->ID, (char *) function_name)) {
            return error(ERR_INTERNAL, "failed to store function name (ID) for '%s'", function_name);
        }
    } else {
        function_data->ID = NULL;
    }

    if (function_name && !builtins_is_builtin_qname(function_name)) {
        // build @name key
        char any_key[256];
        make_function_any_key(any_key, sizeof any_key, function_name);

        if (!st_find(semantic_table->funcs, any_key)) {
            st_insert(semantic_table->funcs, any_key, ST_FUN, true);
            st_data *any_data = st_get(semantic_table->funcs, any_key);
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
 * @brief inserts a getter/setter signature into the function table.
 * Rules:
 *  - most one getter and one setter per base name in a given class,
 *  - second getter/setter with the same base in the same class -> ERR_REDEF (4),
 *  - getter/setter with the same base in another class -> allowed.
 *
 * @param semantic_table semantic context.
 * @param base_name base name (property name).
 * @param is_setter true for setter, false for getter.
 * @param class_scope_name name of the class scope.
 * @return SUCCESS or an error code (ERR_REDEF, ERR_INTERNAL).
 */
static int function_table_insert_accessor(semantic *semantic_table, const char *base_name, bool is_setter, const char *class_scope_name) {
    // build "get:base" or "set:base" key
    char accessor_key[256];
    make_accessor_key(accessor_key, sizeof accessor_key, base_name, is_setter);

    // search existing accessor record
    st_data *existing = st_get(semantic_table->funcs, accessor_key);
    if (existing) {
        const char *existing_scope = NULL;
        if (existing->scope_name && existing->scope_name->data && existing->scope_name->length > 0) {
            existing_scope = existing->scope_name->data;
        }
        if (existing_scope && class_scope_name &&
            strcmp(existing_scope, class_scope_name) == 0) {
            return error(ERR_REDEF, is_setter ? "duplicate setter for '%s' in class '%s'" : "duplicate getter for '%s' in class '%s'", base_name ? base_name : "(null)", existing_scope); // reject second accessor in same class
        }
        // allow same base in a different class
        return SUCCESS;
    }

    // insert new accessor entry
    st_insert(semantic_table->funcs, accessor_key, ST_FUN, true);
    st_data *accessor_data = st_get(semantic_table->funcs, accessor_key);
    if (!accessor_data) {
        return error(ERR_INTERNAL, "failed to store accessor signature: %s", accessor_key);
    }

    accessor_data->symbol_type = ST_FUN;
    accessor_data->param_count = is_setter ? 1 : 0;
    accessor_data->defined = false;
    accessor_data->global = true;

    // store class scope name
    if (class_scope_name) {
        accessor_data->scope_name = string_create(0);
        if (!accessor_data->scope_name ||
            !string_append_literal(accessor_data->scope_name, (char *) class_scope_name)) {
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
            !string_append_literal(accessor_data->ID, (char *) base_name)) {
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
 * @param semantic_table Semantic context.
 * @param function_name Function name.
 * @param arity Arity of the function.
 * @return SUCCESS or ERR_DEF if main() has wrong arity.
 */
static int check_and_mark_main_function(semantic *semantic_table, const char *function_name, int arity) {
    if (!function_name || strcmp(function_name, "main") != 0) {
        return SUCCESS;
    }
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
 * @brief Checks if exact function signature (name, arity) exists in the function table.
 * @param semantic_table Semantic context.
 * @param function_name Function name.
 * @param arity Arity to check.
 * @return True if signature exists.
 */
static bool function_table_has_signature(semantic *semantic_table, const char *function_name, int arity) {
    if (!semantic_table || !semantic_table->funcs) {
        return false;
    }
    char function_key[256];
    make_function_key(function_key, sizeof function_key, function_name, arity);
    return st_find(semantic_table->funcs, function_key) != NULL;
}

/**
 * @brief Checks if at overload for a function name exists.
 * @param semantic_table Semantic context.
 * @param function_name Function name.
 * @return True if any overload exists.
 */
static bool function_table_has_any_overload(semantic *semantic_table, const char *function_name) {
    if (!semantic_table || !semantic_table->funcs) {
        return false;
    }
    char any_key[256];
    make_function_any_key(any_key, sizeof any_key, function_name);
    return st_find(semantic_table->funcs, any_key) != NULL;
}

/**
 * @brief Performs arity checks for user function calls.
 * Rules:
 *  - exact header (name, arity) exists -> call is valid
 *  - another overload for the same name exists but not with correct arity -> ERR_ARGNUM.
 * @param semantic_table Semantic context.
 * @param function_name Function name.
 * @param arity Argument count.
 * @return SUCCESS or ERR_ARGNUM.
 */
static int check_function_call_arity(semantic *semantic_table, const char *function_name, int arity) {
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

/**
 * @brief Literal kind classification for expressions.
 */
typedef enum {
    LITERAL_UNKNOWN = 0,
    LITERAL_NUMERIC,
    LITERAL_STRING
} literal_kind;

/**
 * @brief Returns true if the expression is integer.
 *
 * @param expression_node Expression node.
 * @return True if the expression is integer.
 */
static bool expression_is_integer_literal(ast_expression expression_node) {
    return expression_node && expression_node->type == AST_VALUE && expression_node->operands.identity.value_type == AST_VALUE_INT;
}

/**
 * @brief determines literal-kind of a value expression (number, string, or unknown).
 * @param expression_node expression node.
 * @return literal kind classification.
 */
static literal_kind get_literal_kind_of_value_expression(ast_expression expression_node) {
    if (expression_node->type != AST_VALUE) {
        // non-value node is treated as non-literal
        return LITERAL_UNKNOWN;
    }
    // map concrete ast value types to literal_kind
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
 * @brief recursively computes literal-kind for an expression subtree.
 *  - LITERAL_NUMERIC: numeric-only literals/operators.
 *  - LITERAL_STRING : string-safe literals/operators (string+string, string*int literal, concat).
 *  - LITERAL_UNKNOWN: involves identifiers, calls, or mixed/unsupported combinations.
 * @param expression_node expression node.
 * @return literal kind classification.
 */
static literal_kind get_expression_literal_kind(ast_expression expression_node) {
    if (!expression_node) {
        return LITERAL_UNKNOWN;
    }

    switch (expression_node->type) {
        case AST_VALUE:
            return get_literal_kind_of_value_expression(expression_node);
        // binary operators
        case AST_ADD: {
            literal_kind left_kind = get_expression_literal_kind(expression_node->operands.binary_op.left);
            literal_kind right_kind = get_expression_literal_kind(expression_node->operands.binary_op.right);

            // mixed  to unknown
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
        // subtraction, division: num-num
        case AST_SUB:
        case AST_DIV: {
            literal_kind left_kind = get_expression_literal_kind(expression_node->operands.binary_op.left);
            literal_kind right_kind = get_expression_literal_kind(expression_node->operands.binary_op.right);

            if (left_kind == LITERAL_NUMERIC && right_kind == LITERAL_NUMERIC) {
                return LITERAL_NUMERIC;
            }
            return LITERAL_UNKNOWN;
        }
        // multiplication: num*num or str*int-literal
        case AST_MUL: {
            literal_kind left_kind = get_expression_literal_kind(expression_node->operands.binary_op.left);
            literal_kind right_kind = get_expression_literal_kind(expression_node->operands.binary_op.right);

            if (left_kind == LITERAL_NUMERIC && right_kind == LITERAL_NUMERIC) {
                return LITERAL_NUMERIC;
            }

            if (left_kind == LITERAL_STRING &&
                expression_is_integer_literal(expression_node->operands.binary_op.right)) {
                return LITERAL_STRING;
            }
            return LITERAL_UNKNOWN;
        }
        // relational operators require numeric literals
        case AST_CONCAT: {
            literal_kind left_kind = get_expression_literal_kind(expression_node->operands.binary_op.left);
            literal_kind right_kind = get_expression_literal_kind(expression_node->operands.binary_op.right);

            if (left_kind == LITERAL_STRING && right_kind == LITERAL_STRING) {
                return LITERAL_STRING;
            }
            return LITERAL_UNKNOWN;
        }

        default:
            // all other treated as nonliteral
            return LITERAL_UNKNOWN;
    }
}

static int visit_expression_node(semantic *semantic_table, ast_expression expression_node);

/**
 * @brief performs literal-only checks for a binary expression.
 *
 * @param op ast operator type.
 * @param left_kind literal kind of left operand.
 * @param right_kind literal kind of right operand.
 * @param right_expression right operand expression.
 * @return SUCCESS or ERR_EXPR on literal-only policy violation.
 */
static int sem_check_literal_binary(int op, literal_kind left_kind, literal_kind right_kind, ast_expression right_expression) {
    // skip policy when at least one side is non-literal
    if (!left_kind || !right_kind) {
        return SUCCESS;
    }

    switch (op) {
        case AST_ADD: {
            // allow num+num or string+string
            bool ok = (left_kind == LITERAL_NUMERIC && right_kind == LITERAL_NUMERIC) ||
                      (left_kind == LITERAL_STRING && right_kind == LITERAL_STRING);
            if (!ok) {
                return error(ERR_EXPR, "invalid literal '+' operands");
            }
            break;
        }

        case AST_SUB:
        case AST_DIV:
            // allow only numeric for '-', '/'
            if (!(left_kind == LITERAL_NUMERIC && right_kind == LITERAL_NUMERIC)) {
                return error(ERR_EXPR, "invalid literal arithmetic operands");
            }
            break;

        case AST_MUL: {
            // allow num*num or string*int
            bool ok = (left_kind == LITERAL_NUMERIC && right_kind == LITERAL_NUMERIC) ||
                      (left_kind == LITERAL_STRING && expression_is_integer_literal(right_expression));
            if (!ok) {
                return error(ERR_EXPR, "invalid literal '*' operands");
            }
            break;
        }
        case AST_LT:
        case AST_LE:
        case AST_GT:
        case AST_GE:
            // allow only numeric literals for relational operators
            if (!(left_kind == LITERAL_NUMERIC && right_kind == LITERAL_NUMERIC)) {
                return error(ERR_EXPR, "relational operators require numeric literals");
            }
            break;
        default:
            break;
    }

    return SUCCESS;
}

/* -------------------------------------------------------------------------
 *                Built-in function calls (Ifj.*) – Pass 1
 * ------------------------------------------------------------------------- */
/**
 * @brief Normalizes a builtin name to fully qualified "Ifj.*" form.
 */
static const char *sem_build_ifj_qname(const char *raw_name, char *buffer, size_t buffer_size) {
    if (!raw_name) {
        return NULL;
    }
    if (strncmp(raw_name, "Ifj.", 4) == 0) {
        return raw_name;
    }
    size_t base_len = strlen(raw_name);
    size_t needed = 4 + base_len + 1;

    // fall back to raw name when buffer is too small
    if (needed > buffer_size) {
        return raw_name;
    }

    memcpy(buffer, "Ifj.", 4);
    // copy name including terminating null
    memcpy(buffer + 4, raw_name, base_len + 1);
    return buffer;
}

/**
 * @brief Parameter literal kind classification.
 */
typedef enum {
    PARAM_KIND_UNKNOWN = 0,
    PARAM_KIND_STRING_LITERAL,
    PARAM_KIND_NUMERIC_LITERAL
} param_kind;

/**
 * @brief Basic literal classification of a built-in argument.
 * Returns:
 *  - PARAM_KIND_STRING_LITERAL for string literals,
 *  - PARAM_KIND_NUMERIC_LITERAL for int/float literals,
 *  - PARAM_KIND_UNKNOWN for identifiers, null, complex expressions, or  globals.
 * @param param Parameter node.
 * @return Parameter literal kind.
 */
static param_kind sem_get_param_literal_kind(ast_parameter param) {
    if (!param) {
        return PARAM_KIND_UNKNOWN;
    }
    // treat "__..." string or identifier as global
    if ((param->value_type == AST_VALUE_STRING || param->value_type == AST_VALUE_IDENTIFIER) &&
        is_global_identifier(param->value.string_value)) {
        return PARAM_KIND_UNKNOWN;
    }
    // map concrete ast value types to param_kind
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
 * @brief checks builtin calls in pass 1 (arity and simple literal types). argument count is checked against the builtin function table.
 *
 * @param semantic_table semantic context.
 * @param raw_name either fully qualified "Ifj.foo" or short name from ast.
 * @param parameters parameter list.
 * @return SUCCESS or ERR_ARGNUM.
 */
static int sem_check_builtin_call(semantic *semantic_table, const char *raw_name, ast_parameter parameters) {
    if (!semantic_table || !raw_name) {
        return SUCCESS;
    }

    char qname_buffer[64];
    // normalize name to "Ifj.*"
    const char *name = sem_build_ifj_qname(raw_name, qname_buffer, sizeof qname_buffer);

    // check builtin arity using function table
    int arg_count = count_parameters(parameters);
    if (!function_table_has_signature(semantic_table, name, arg_count)) {
        return error(ERR_ARGNUM, "wrong number of arguments for builtin %s (arity=%d)", name, arg_count);
    }

    ast_parameter p1 = parameters;
    ast_parameter p2 = p1 ? p1->next : NULL;
    ast_parameter p3 = p2 ? p2->next : NULL;

    // precompute literal kinds for the first three parameters
    param_kind k1 = sem_get_param_literal_kind(p1);
    param_kind k2 = sem_get_param_literal_kind(p2);
    param_kind k3 = sem_get_param_literal_kind(p3);

    // check literal types for specific builtins
    if (strcmp(name, "Ifj.floor") == 0) {
        if (k1 != PARAM_KIND_UNKNOWN && k1 != PARAM_KIND_NUMERIC_LITERAL) {
            return error(ERR_ARGNUM, "wrong literal type for builtin Ifj.floor");
        }
        return SUCCESS;
    }

    if (strcmp(name, "Ifj.length") == 0) {
        if (k1 != PARAM_KIND_UNKNOWN && k1 != PARAM_KIND_STRING_LITERAL) {
            return error(ERR_ARGNUM, "wrong literal type for builtin Ifj.length");
        }
        return SUCCESS;
    }

    if (strcmp(name, "Ifj.substring") == 0) {
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
        if (k1 != PARAM_KIND_UNKNOWN && k1 != PARAM_KIND_STRING_LITERAL) {
            return error(ERR_ARGNUM, "wrong literal type for builtin Ifj.strcmp(arg1)");
        }
        if (k2 != PARAM_KIND_UNKNOWN && k2 != PARAM_KIND_STRING_LITERAL) {
            return error(ERR_ARGNUM, "wrong literal type for builtin Ifj.strcmp(arg2)");
        }
        return SUCCESS;
    }

    if (strcmp(name, "Ifj.ord") == 0) {
        if (k1 != PARAM_KIND_UNKNOWN && k1 != PARAM_KIND_STRING_LITERAL) {
            return error(ERR_ARGNUM, "wrong literal type for builtin Ifj.ord(arg1)");
        }
        if (k2 != PARAM_KIND_UNKNOWN && k2 != PARAM_KIND_NUMERIC_LITERAL) {
            return error(ERR_ARGNUM, "wrong literal type for builtin Ifj.ord(arg2)");
        }
        return SUCCESS;
    }

    if (strcmp(name, "Ifj.chr") == 0) {
        if (k1 != PARAM_KIND_UNKNOWN && k1 != PARAM_KIND_NUMERIC_LITERAL) {
            return error(ERR_ARGNUM, "wrong literal type for builtin Ifj.chr");
        }
        return SUCCESS;
    }
    return SUCCESS;
}

/**
 * @brief handles a function-call expression node (AST_FUNCTION_CALL) in pass 1.
 * @param semantic_table semantic context.
 * @param expression_node expression node representing the call.
 * @return SUCCESS or an error code.
 */
static int sem_visit_call_expr(semantic *semantic_table, ast_expression expression_node) {
    if (!expression_node->operands.function_call) {
        return SUCCESS;
    }
    // read call node and function name from expression
    ast_fun_call call_node = expression_node->operands.function_call;
    const char *called_name = call_node->name;
    // handle builtin calls (arity + literals)
    if (builtins_is_builtin_qname(called_name)) {
        return sem_check_builtin_call(semantic_table, called_name, call_node->parameters);
    }
    // for user functions only arity
    int parameter_count = count_parameters(call_node->parameters);
    return check_function_call_arity(semantic_table, called_name, parameter_count);
}

/**
 * @brief handles a binary-like expression (relational, logical, ternary, 'is') in pass 1.
 * @param semantic_table semantic context.
 * @param expression_node expression node.
 * @return SUCCESS or error code.
 */
static int sem_visit_binary_expr(semantic *semantic_table, ast_expression expression_node) {
    // read operand pointers
    ast_expression left_expression = expression_node->operands.binary_op.left;
    ast_expression right_expression = expression_node->operands.binary_op.right;

    // visit left operand
    int result_code = visit_expression_node(semantic_table, left_expression);
    if (result_code != SUCCESS) {
        return result_code;
    }

    // visit right operand
    result_code = visit_expression_node(semantic_table, right_expression);
    if (result_code != SUCCESS) {
        return result_code;
    }
    // compute literal kinds
    literal_kind left_kind = get_expression_literal_kind(left_expression);
    literal_kind right_kind = get_expression_literal_kind(right_expression);
    // literal checker
    return sem_check_literal_binary(expression_node->type, left_kind, right_kind, right_expression);
}

/**
 * @brief visits an expression node in pass 1 and runs early checks.
 * @param semantic_table semantic context.
 * @param expression_node expression node.
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
            // simple values and unary operators are ignored
            return SUCCESS;
        case AST_IFJ_FUNCTION_EXPR: {
            // handle expression-form builtin call (Ifj.*)
            ast_ifj_function ifj_call = expression_node->operands.ifj_function;
            if (!ifj_call || !ifj_call->name) {
                return SUCCESS;
            }
            return sem_check_builtin_call(semantic_table, ifj_call->name, ifj_call->parameters);
        }
        case AST_FUNCTION_CALL:
            // handle normal function call expression
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
            // visit binary expression and apply literal rules
            return sem_visit_binary_expr(semantic_table, expression_node);
        default:
            return SUCCESS;
    }
}

/* =========================================================================
 *                       Bodies walk (scopes & nodes) – Pass 1
 * ========================================================================= */

static int visit_block_node(semantic *semantic_table, ast_block block_node);

static int visit_statement_node(semantic *semantic_table, ast_node node);

/**
 * @brief enters a block scope in pass 1.
 * @param semantic_table semantic context.
 */
static void sem_scope_enter_block(semantic *semantic_table) {
    // update scope id stack for new block
    if (semantic_table->ids.depth < 0) {
        sem_scope_ids_enter_root(&semantic_table->ids);
    } else {
        sem_scope_ids_enter_child(&semantic_table->ids);
    }
    // push new local scope frame
    scopes_push(&semantic_table->scopes);
}

/**
 * @brief leaves a block scope in pass 1.
 * @param semantic_table semantic context.
 * @param context diagnostic context string (for error messages).
 * @return SUCCESS or ERR_INTERNAL on underflow.
 */
static int sem_scope_leave_block(semantic *semantic_table, const char *context) {
    // pop local scope frame
    bool ok = scopes_pop(&semantic_table->scopes);
    // move up in scope id
    sem_scope_ids_leave(&semantic_table->ids);
    if (!ok) {
        return error(ERR_INTERNAL, "scope stack underflow in %s", context);
    }
    return SUCCESS;
}

/**
 * @brief declares a function parameter list in the current pass 1 scope.
 * @param semantic_table semantic context.
 * @param parameter_list head of the parameter list.
 * @return SUCCESS or an error code.
 */
static int declare_parameter_list_in_current_scope(semantic *semantic_table, ast_parameter parameter_list) {
    for (ast_parameter parameter = parameter_list; parameter; parameter = parameter->next) {
        const char *param_name = sem_get_parameter_name(parameter);
        if (!param_name) {
            return error(ERR_INTERNAL, "parameter without name in current scope");
        }

        // declare parameter in local scope
        if (!scopes_declare_local(&semantic_table->scopes, param_name, true)) {
            return error(ERR_REDEF, "parameter '%s' redeclared in the same scope", param_name);
        }

        // mark symbol as parameter with unknown data type
        st_data *parameter_data = scopes_lookup_in_current(&semantic_table->scopes, param_name);
        if (parameter_data) {
            parameter_data->symbol_type = ST_PAR;
            parameter_data->data_type = ST_UNKNOWN;
        }
    }
    return SUCCESS;
}

/**
 * @brief Visits a block in Pass 1: pushes scope, walks all nodes, then pops scope.
 * @param semantic_table Semantic context.
 * @param block_node Block node.
 * @return SUCCESS or an error code.
 */
static int visit_block_node(semantic *semantic_table, ast_block block_node) {
    if (!block_node) {
        return SUCCESS;
    }
    // enter new block scope
    sem_scope_enter_block(semantic_table);
    // visit each statement in block
    for (ast_node node = block_node->first; node; node = node->next) {
        int result_code = visit_statement_node(semantic_table, node);
        if (result_code != SUCCESS) {
            // leave scope
            sem_scope_leave_block(semantic_table, "visit_block_node (early error)");
            return result_code;
        }
    }
    // leave block scope
    return sem_scope_leave_block(semantic_table, "visit_block_node");
}

/**
 * @brief Handles an AST_CONDITION node (if/else) in Pass 1.
 * @param semantic_table Semantic context.
 * @param node AST node of type AST_CONDITION.
 * @return SUCCESS or an error code.
 */
static int sem_handle_condition_node(semantic *semantic_table, ast_node node) {
    // check condition expression
    int result_code = visit_expression_node(semantic_table, node->data.condition.condition);
    if (result_code != SUCCESS) {
        return result_code;
    }
    // visit if branch block
    result_code = visit_block_node(semantic_table, node->data.condition.if_branch);
    if (result_code != SUCCESS) {
        return result_code;
    }
    // visit else branch block (if present)
    return visit_block_node(semantic_table, node->data.condition.else_branch);
}

/**
 * @brief Handles an AST_WHILE_LOOP node in Pass 1.
 * @param semantic_table Semantic context.
 * @param node AST node of type AST_WHILE_LOOP.
 * @return SUCCESS or an error code.
 */
static int sem_handle_while_node(semantic *semantic_table, ast_node node) {
    // check loop condition expression
    int result_code = visit_expression_node(semantic_table, node->data.while_loop.condition);
    if (result_code != SUCCESS) {
        return result_code;
    }

    // increase loop depth before visiting body
    semantic_table->loop_depth++;
    result_code = visit_block_node(semantic_table, node->data.while_loop.body);
    // restore loop depth after visiting body
    semantic_table->loop_depth--;
    return result_code;
}

/**
 * @brief Handles a function body in Pass 1 (parameters and body share one scope).
 * @param semantic_table Semantic context.
 * @param node AST node of type AST_FUNCTION.
 * @return SUCCESS or an error code.
 */
static int sem_handle_function_node(semantic *semantic_table, ast_node node) {
    ast_function function_node = node->data.function;
    // enter function scope (parameters + body)
    sem_scope_enter_block(semantic_table);
    int result_code = declare_parameter_list_in_current_scope(semantic_table, function_node->parameters);
    if (result_code != SUCCESS) {
        // leave scope
        sem_scope_leave_block(semantic_table, "sem_handle_function_node (params)");
        return result_code;
    }

    if (function_node->code) {
        // visit all statements
        for (ast_node stmt = function_node->code->first; stmt; stmt = stmt->next) {
            result_code = visit_statement_node(semantic_table, stmt);
            if (result_code != SUCCESS) {
                // leave scope
                sem_scope_leave_block(semantic_table, "sem_handle_function_node (body)");
                return result_code;
            }
        }
    }
    // leave function scope
    return sem_scope_leave_block(semantic_table, "sem_handle_function_node");
}

/**
 * @brief Handles a getter body in Pass 1 - all sttatements share one scope.
 * @param semantic_table Semantic context.
 * @param node AST node of type AST_GETTER.
 * @return SUCCESS or an error code.
 */
static int sem_handle_getter_node(semantic *semantic_table, ast_node node) {
    // enter getter scope
    sem_scope_enter_block(semantic_table);
    if (node->data.getter.body) {
        // visit all statements in getter body
        for (ast_node stmt = node->data.getter.body->first; stmt; stmt = stmt->next) {
            int result_code = visit_statement_node(semantic_table, stmt);
            if (result_code != SUCCESS) {
                // leave scope
                sem_scope_leave_block(semantic_table, "sem_handle_getter_node (body)");
                return result_code;
            }
        }
    }

    // leave getter scope
    return sem_scope_leave_block(semantic_table, "sem_handle_getter_node");
}

/**
 * @brief Handles a setter body in Pass 1 - parameter and body share one scope.
 * @param semantic_table Semantic context.
 * @param node AST node of type AST_SETTER.
 * @return SUCCESS or an error code.
 */
static int sem_handle_setter_node(semantic *semantic_table, ast_node node) {
    const char *param_name = node->data.setter.param;
    // enter setter scope
    sem_scope_enter_block(semantic_table);

    // declare implicit setter parameter in local scope
    if (!scopes_declare_local(&semantic_table->scopes, param_name, true)) {
        sem_scope_leave_block(semantic_table, "sem_handle_setter_node (param)");
        return error(ERR_REDEF, "setter parameter redeclared: %s", param_name ? param_name : "(null)");
    }

    st_data *setter_param_data = scopes_lookup_in_current(&semantic_table->scopes, param_name);
    if (setter_param_data) {
        // symbol as parameter
        setter_param_data->symbol_type = ST_PAR;
    }

    if (node->data.setter.body) {
        // visit all statements in setter body
        for (ast_node stmt = node->data.setter.body->first; stmt; stmt = stmt->next) {
            int result_code = visit_statement_node(semantic_table, stmt);
            if (result_code != SUCCESS) {
                sem_scope_leave_block(semantic_table, "sem_handle_setter_node (body)");
                return result_code;
            }
        }
    }
    // leave setter scope
    return sem_scope_leave_block(semantic_table, "sem_handle_setter_node");
}

/**
 * @brief visits a single ast node in statement during pass 1.
 * @param semantic_table semantic context
 * @param node ast node to visit
 * @return SUCCESS or an error code
 */
static int visit_statement_node(semantic *semantic_table, ast_node node) {
    if (!node) {
        return SUCCESS;
    }

    switch (node->type) {
        case AST_BLOCK:
            // visit nested block with its own scope
            return visit_block_node(semantic_table, node->data.block);

        case AST_CONDITION:
            // handle if/else condition node
            return sem_handle_condition_node(semantic_table, node);

        case AST_WHILE_LOOP:
            // handle while loop (condition + body, loop depth tracking)
            return sem_handle_while_node(semantic_table, node);

        case AST_BREAK:
            // check that break is used inside a loop
            if (semantic_table->loop_depth <= 0) {
                return error(ERR_SEM, "break outside of loop");
            }
            return SUCCESS;

        case AST_CONTINUE:
            // check that continue is used inside a loop
            if (semantic_table->loop_depth <= 0) {
                return error(ERR_SEM, "continue outside of loop");
            }
            return SUCCESS;
        case AST_EXPRESSION:
            return visit_expression_node(semantic_table, node->data.expression);

        case AST_VAR_DECLARATION: {
            // declare new local variable in current scope
            const char *variable_name = node->data.declaration.name;

            if (!scopes_declare_local(&semantic_table->scopes, variable_name, true)) {
                return error(ERR_REDEF, "variable '%s' already declared in this scope", variable_name ? variable_name : "(null)");
            }
            // mark symbol as variable
            st_data *variable_data = scopes_lookup_in_current(&semantic_table->scopes, variable_name);
            if (variable_data) {
                variable_data->symbol_type = ST_VAR;
            }
            return SUCCESS;
        }

        case AST_ASSIGNMENT: {
            // check that assignment target exists
            const char *assigned_name = node->data.assignment.name;
            int result_code = sem_check_assignment_lhs(semantic_table, assigned_name);
            if (result_code != SUCCESS) {
                return result_code;
            }
            // visit right expression
            return visit_expression_node(semantic_table, node->data.assignment.value);
        }
        case AST_FUNCTION:
            // handle function body (parameters + body in one scope)
            return sem_handle_function_node(semantic_table, node);
        case AST_IFJ_FUNCTION: {
            // handle builtin call in statement
            ast_ifj_function ifj_call = node->data.ifj_function;
            if (!ifj_call || !ifj_call->name) {
                return SUCCESS;
            }
            return sem_check_builtin_call(semantic_table, ifj_call->name, ifj_call->parameters);
        }
        case AST_CALL_FUNCTION: {
            // handle user or builtin call
            ast_fun_call call_node = node->data.function_call;
            int parameter_count = count_parameters(call_node->parameters);

            if (builtins_is_builtin_qname(call_node->name)) {
                return sem_check_builtin_call(semantic_table, call_node->name, call_node->parameters);
            }

            return check_function_call_arity(semantic_table, call_node->name, parameter_count);
        }
        case AST_RETURN:
            // expression returned from current function
            return visit_expression_node(semantic_table, node->data.return_expr.output);
        case AST_GETTER:
            // handle getter body in its own scope
            return sem_handle_getter_node(semantic_table, node);
        case AST_SETTER:
            // handle setter body in its own scope
            return sem_handle_setter_node(semantic_table, node);
        default:
            return SUCCESS;
    }
}

/* =========================================================================
 *                      Header collection – Pass 1
 * ========================================================================= */

/**
 * @brief collects function/getter/setter headers inside all class blocks.
 * @param semantic_table semantic context
 * @param syntax_tree ast root
 * @return SUCCESS or an error code
 */
static int collect_headers(semantic *semantic_table, ast syntax_tree) {
    for (ast_class class_node = syntax_tree->class_list; class_node; class_node = class_node->next) {
        // get class root block and collect headers
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
 * @brief recursively walks a block to collect function/getter/setter headers
 *  - collects AST_FUNCTION / AST_GETTER / AST_SETTER headers,
 *  - descends into nested AST_BLOCK nodes,
 *  - associates each header with a class_scope_name
 * @param semantic_table semantic context
 * @param block_node root block node to scan
 * @param class_scope_name name of the current class scope
 * @return SUCCESS or an error code
 */
static int collect_headers_from_block(semantic *semantic_table, ast_block block_node, const char *class_scope_name) {
    if (!block_node) {
        return SUCCESS;
    }
    // iterate over all nodes in the block
    for (ast_node node = block_node->first; node; node = node->next) {
        switch (node->type) {
            case AST_FUNCTION: {
                // register function signature and check main
                ast_function fn = node->data.function;
                const char *fn_name = fn->name;
                int arity = count_parameters(fn->parameters);

                int rc = function_table_insert_signature(semantic_table, fn_name, arity, class_scope_name);
                if (rc != SUCCESS) {
                    return rc;
                }
                rc = check_and_mark_main_function(semantic_table, fn_name, arity);
                if (rc != SUCCESS) {
                    return rc;
                }
                break;
            }
            case AST_GETTER: {
                // register getter header
                int rc = function_table_insert_accessor(semantic_table, node->data.getter.name, false, class_scope_name);
                if (rc != SUCCESS) {
                    return rc;
                }
                break;
            }
            case AST_SETTER: {
                // register setter header
                int rc = function_table_insert_accessor(semantic_table, node->data.setter.name, true, class_scope_name);
                if (rc != SUCCESS) {
                    return rc;
                }
                break;
            }
            case AST_BLOCK: {
                // recurse into nested block - collect inner headers
                int rc = collect_headers_from_block(semantic_table, node->data.block, class_scope_name);
                if (rc != SUCCESS) {
                    return rc;
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
 *                              PASS 1
 * ========================================================================= */
/**
 * @brief Runs the first semantic pass over the AST and then Pass 2.
 *  - initializes semantic tables and registries,
 *  - installs IFJ built-ins into the function table,
 *  - collects function/getter/setter headers (Pass 1, step 1),
 *  - checks presence and arity of main(),
 *  - walks bodies and fills scopes (Pass 1, step 2),
 *  - runs semantic_pass2() with the same semantic context,
 *  - frees all internal tables and returns the result of Pass 2.
 *
 * @param tree AST root.
 * @return SUCCESS or the first error encountered.
 */
int semantic_pass1(ast tree) {
    // initialize semantic context
    semantic semantic_table = {0};
    // reset globals
    sem_globals_reset();
    // reset global types
    sem_global_types_reset();

    // initialize global function table
    semantic_table.funcs = st_init();
    if (!semantic_table.funcs) {
        return error(ERR_INTERNAL, "failed to init global function table");
    }

    // initialize scopes and scope ids stack
    scopes_init(&semantic_table.scopes);
    sem_scope_ids_init(&semantic_table.ids);

    // initialize loop depth and main() seen flag
    semantic_table.loop_depth = 0;
    semantic_table.seen_main = false;

    // install built-in functions
    builtins_config builtins_configuration = (builtins_config){.ext_boolthen = false, .ext_statican = false};
    if (!builtins_install(semantic_table.funcs, builtins_configuration)) {
        int rc = error(ERR_INTERNAL, "failed to install built-ins");
        st_free(semantic_table.funcs);
        return rc;
    }

    // collect function/getter/setter headers from all classes
    int result_code = collect_headers(&semantic_table, tree);
    if (result_code != SUCCESS) {
        st_free(semantic_table.funcs);
        return result_code;
    }

    // check that main() with 0 parameters exists
    if (!semantic_table.seen_main) {
        int rc = error(ERR_DEF, "missing main() with 0 parameters");
        st_free(semantic_table.funcs);
        return rc;
    }

    // visit all class root blocks to fill scopes
    for (ast_class class_node = tree->class_list; class_node; class_node = class_node->next) {
        ast_block root_block = get_class_root_block(class_node);
        if (!root_block) {
            continue;
        }

        result_code = visit_block_node(&semantic_table, root_block);
        if (result_code != SUCCESS) {
            st_free(semantic_table.funcs);
            return result_code;
        }
    }

    // run Pass 2 with the same semantic context
    int pass2_result = semantic_pass2(&semantic_table, tree);
    // free function table
    st_free(semantic_table.funcs);
    return pass2_result;
}

/* =========================================================================
 *                              Pass 2
 * ========================================================================= */
static int sem2_visit_block(semantic *table, ast_block blk);

/* -------------------------------------------------------------------------
 *  Identifier resolver + Function call checker (Pass 2)
 * ------------------------------------------------------------------------- */
/**
 * @brief Resolves an identifier in Pass 2, checking locals, accessors, and  globals.
 * Resolution order:
 *  1) Local variable/parameter,
 *  2) Accessor (getter or setter),
 *  3) Global "__name" (implicitly allowed),
 *  4) Otherwise ERR_DEF.
 * @param cxt Semantic context.
 * @param name Identifier name.
 * @return SUCCESS or ERR_DEF/ERR_INTERNAL.
 */
static int sem2_resolve_identifier(semantic *cxt, const char *name) {
    if (!name) {
        return SUCCESS;
    }

    // resolve local variable or parameter
    st_data *local = scopes_lookup(&cxt->scopes, name);
    if (local) {
        return SUCCESS;
    }
    // check getter accessor
    char key_get[256];
    make_accessor_key(key_get, sizeof key_get, name, false);
    if (st_find(cxt->funcs, key_get)) {
        return SUCCESS;
    }

    // check setter accessor
    char key_set[256];
    make_accessor_key(key_set, sizeof key_set, name, true);
    if (st_find(cxt->funcs, key_set)) {
        return error(ERR_DEF, "use of setter-only property '%s' without getter", name);
    }

    // register global and accept
    if (is_global_identifier(name)) {
        int rc = sem_globals_add(name);
        if (rc != SUCCESS) {
            return rc;
        }
        return SUCCESS;
    }
    // unknown identifier semantic error
    return error(ERR_DEF, "use of undefined identifier '%s'", name);
}

/**
 * @brief Checks a function call in Pass 2, including built-ins and user functions.
 * Built-in calls:
 *  - checks exact arity via the function table.
 * User functions:
 *  - if exact signature exists - OK,
 *  - if any overload exists but not with this arity - ERR_ARGNUM,
 *  - otherwise - ERR_DEF (undefined function).
 * @param cxt Semantic context.
 * @param name Function name (possibly fully qualified for built-ins).
 * @param arity Argument count.
 * @return SUCCESS or an error code.
 */
static int sem2_check_function_call(semantic *cxt, const char *name, int arity) {
    if (!name) {
        return SUCCESS;
    }
    // check built-in function call
    if (builtins_is_builtin_qname(name)) {
        char key[256];
        make_function_key(key, sizeof key, name, arity);

        if (!st_find(cxt->funcs, key)) {
            return error(ERR_ARGNUM, "wrong number of arguments for builtin %s(%d)", name, arity);
        }
        return SUCCESS;
    }
    // check user function call
    if (function_table_has_signature(cxt, name, arity)) {
        return SUCCESS;
    }
    // check for any overload
    if (function_table_has_any_overload(cxt, name)) {
        return error(ERR_ARGNUM, "wrong number of arguments for %s (arity=%d)", name, arity);
    }
    return error(ERR_DEF, "call to undefined function '%s'", name);
}

/*-------------------------------------------------------------------------
 *  Helpers (Pass 2)
 * ------------------------------------------------------------------------- */
/**
 * @brief checks if a data type is unknown in Pass 2.
 * @param t data type to check
 * @return true if unknown, false otherwise
 */
static bool sem_is_unknownish_type(data_type t) {
    return t == ST_UNKNOWN || t == ST_VOID || sem_is_unknown_type(t);
}

/**
 * @brief common function call handler for Pass 2.
 * @param cxt Semantic context.
 * @param name_for_check Function name for checking.
 * @param params Parameter list of the function call.
 * @param treat_as_builtin Whether to treat the function as a built-in for return type inference.
 * @param out_type Optional output for inferred return type.
 * @return SUCCESS or an error code.
 */
static int sem2_visit_call_common(semantic *cxt, const char *name_for_check, ast_parameter params, bool treat_as_builtin, data_type *out_type) {
    int ar = count_parameters(params);

    // check function call against headers
    int rc = sem2_check_function_call(cxt, name_for_check, ar);
    if (rc != SUCCESS) {
        return rc;
    }
    // resolve identifier arguments
    for (ast_parameter p = params; p; p = p->next) {
        if (p->value_type == AST_VALUE_IDENTIFIER) {
            rc = sem2_resolve_identifier(cxt, p->value.string_value);
            if (rc != SUCCESS) {
                return rc;
            }
        }
    }
    // return type for builtins only
    if (out_type) {
        data_type ret = ST_UNKNOWN;
        if (treat_as_builtin && name_for_check) {
            char key[256];
            make_function_key(key, sizeof key, name_for_check, ar);
            st_data *fn = st_get(cxt->funcs, key);
            if (fn) {
                ret = fn->data_type;
            }
        }
        *out_type = ret;
    }
    return SUCCESS;
}

/*-------------------------------------------------------------------------
 *  Expression visitor and type checker (Pass 2)
 * ------------------------------------------------------------------------- */
/**
 * @brief Visits an expression in Pass 2 and checks its approximate data type.
 * @param cxt Semantic context.
 * @param e Expression node.
 * @param out_type Optional output for inferred type.
 * @return SUCCESS or an error code.
 */
static int sem2_visit_expr(semantic *cxt, ast_expression e, data_type *out_type) {
    if (!e) {
        return SUCCESS;
    }

    // set default inferred type for this expression
    if (out_type) {
        *out_type = ST_UNKNOWN;
    }

    switch (e->type) {
        case AST_IDENTIFIER: {
            // resolve identifier and check visibility
            const char *name = e->operands.identifier.value;

            int rc = sem2_resolve_identifier(cxt, name);
            if (rc != SUCCESS) {
                return rc;
            }

            // lookup symbol in current scopes
            st_data *sym = scopes_lookup(&cxt->scopes, name);

            // propagate codegen name for local variable declaration
            if (sym && sym->decl_node && sym->decl_node->type == AST_VAR_DECLARATION) {
                e->operands.identifier.cg_name = sym->decl_node->data.declaration.cg_name;
            }

            // infer type from local symbol or learned global type
            if (out_type && name) {
                data_type t = ST_UNKNOWN;

                if (sym) {
                    t = sym->data_type;
                    if (t == ST_VOID || t == ST_NULL || sem_is_unknown_type(t)) {
                        *out_type = ST_UNKNOWN;
                    } else {
                        *out_type = t;
                    }
                } else if (is_global_identifier(name)) {
                    t = sem_global_type_get(name);
                    if (t == ST_NULL || sem_is_unknown_type(t)) {
                        *out_type = ST_UNKNOWN;
                    } else {
                        *out_type = t;
                    }
                }
            }

            return SUCCESS;
        }

        case AST_VALUE: {
            // map literal node kind to data_type
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
                return SUCCESS;
            }

            bool treat_as_builtin = (call->name && builtins_is_builtin_qname(call->name));
            // handle regular function call through shared helper
            return sem2_visit_call_common(cxt, call->name, call->parameters, treat_as_builtin, out_type);
        }

        case AST_IFJ_FUNCTION_EXPR: {
            ast_ifj_function call = e->operands.ifj_function;
            if (!call) {
                return SUCCESS;
            }

            char qname[128];
            const char *name = call->name ? sem_build_ifj_qname(call->name, qname, sizeof qname) : "(null)";
            // handle ifj.* call expression through shared helper (always builtin)
            return sem2_visit_call_common(cxt, name, call->parameters, true, out_type);
        }

        case AST_NOT:
        case AST_NOT_NULL: {
            // visit inner expression for side effects and then treat result as bool
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

        // binary operators
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
            // visit both operands and infer their types
            data_type lt, rt;
            int rc = sem2_visit_expr(cxt, e->operands.binary_op.left, &lt);
            if (rc != SUCCESS) {
                return rc;
            }

            rc = sem2_visit_expr(cxt, e->operands.binary_op.right, &rt);
            if (rc != SUCCESS) {
                return rc;
            }

            // bail out early if any side is unknownish (keep expression flexible)
            if (sem_is_unknownish_type(lt) || sem_is_unknownish_type(rt)) {
                if (out_type) {
                    if (e->type == AST_EQUALS || e->type == AST_NOT_EQUAL ||
                        e->type == AST_LT || e->type == AST_LE ||
                        e->type == AST_GT || e->type == AST_GE ||
                        e->type == AST_AND || e->type == AST_OR) {
                        *out_type = ST_BOOL;
                    } else {
                        *out_type = ST_UNKNOWN;
                    }
                }
                return SUCCESS;
            }

            // apply type rules
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
                // relational operators
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

                // equality operators
                case AST_EQUALS:
                case AST_NOT_EQUAL:
                    if (out_type) {
                        *out_type = ST_BOOL;
                    }
                    return SUCCESS;

                // logical operators
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
            // visit both branches to trigger checks
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
            // visit left-hand side and enforce allowed type name
            data_type lhs_type;
            int rc = sem2_visit_expr(cxt, e->operands.binary_op.left, &lhs_type);
            if (rc != SUCCESS) {
                return rc;
            }

            ast_expression rhs = e->operands.binary_op.right;
            const char *type_name = NULL;

            // extract type name from right-hand side
            if (rhs && rhs->type == AST_IDENTIFIER && rhs->operands.identifier.value) {
                type_name = rhs->operands.identifier.value;
            }
            // check that type name is present
            if (!type_name) {
                return error(ERR_EXPR, "invalid right-hand side of 'is' operator");
            }
            // check allowed type names
            if (strcmp(type_name, "Num") != 0 && strcmp(type_name, "String") != 0 && strcmp(type_name, "Null") != 0) {
                return error(ERR_EXPR, "invalid type '%s' on right-hand side of 'is' (expected Num, String or Null)", type_name);
            }

            if (out_type) {
                *out_type = ST_BOOL;
            }
            return SUCCESS;
        }
        case AST_NONE:
        case AST_NIL:
            // treat as null
            if (out_type) {
                *out_type = ST_NULL;
            }
            return SUCCESS;
        default:
            // unknown node kind
            return SUCCESS;
    }
}

/* -------------------------------------------------------------------------
 *  Statement visitor (Pass 2)
 * ------------------------------------------------------------------------- */
/**
 * @brief Visits a statement node in Pass 2. Dispatches based on node type.
 * @param table Semantic context.
 * @param node AST node.
 * @return SUCCESS or an error code.
 */
static int sem2_visit_statement_node(semantic *table, ast_node node) {
    if (!node) {
        return SUCCESS;
    }

    switch (node->type) {
        case AST_BLOCK:
            // visit nested block in pass 2
            return sem2_visit_block(table, node->data.block);

        case AST_CONDITION: {
            // condition expression for side effects and checks
            if (node->data.condition.condition) {
                int rc = sem2_visit_expr(table, node->data.condition.condition, NULL);
                if (rc != SUCCESS) {
                    return rc;
                }
            }

            // visit both branches
            int rc = sem2_visit_block(table, node->data.condition.if_branch);
            if (rc != SUCCESS) {
                return rc;
            }

            return sem2_visit_block(table, node->data.condition.else_branch);
        }

        case AST_WHILE_LOOP: {
            // visit loop condition for side effects and checks
            if (node->data.while_loop.condition) {
                int rc = sem2_visit_expr(table, node->data.while_loop.condition, NULL);
                if (rc != SUCCESS) {
                    return rc;
                }
            }
            // visit loop body
            return sem2_visit_block(table, node->data.while_loop.body);
        }

        case AST_EXPRESSION: {
            // visit expression statement
            if (!node->data.expression) {
                return SUCCESS;
            }
            return sem2_visit_expr(table, node->data.expression, NULL);
        }

        case AST_VAR_DECLARATION: {
            // get variable name
            const char *name = node->data.declaration.name;
            if (!name) {
                return error(ERR_INTERNAL, "variable declaration without name in Pass 2");
            }
            // declare variable in current scope
            if (!scopes_declare_local(&table->scopes, name, true)) {
                return error(ERR_REDEF, "variable '%s' already declared in this scope", name);
            }
            // verify that declaration symbol exists
            st_data *sym = scopes_lookup(&table->scopes, name);
            if (!sym) {
                return error(ERR_INTERNAL, "scope lookup failed for '%s'", name);
            }
            sym->decl_node = node;

            // build codegen name based on scope idm
            const char *scope_src = sem_scope_ids_current(&table->ids);
            char final[128];
            sem_build_cg_name(final, sizeof final, name, scope_src);

            // assign codegen name to declaration node
            if (node->data.declaration.cg_name) {
                free(node->data.declaration.cg_name);
            }
            node->data.declaration.cg_name = my_strdup(final);
            if (!node->data.declaration.cg_name) {
                return error(ERR_INTERNAL, "memory allocation failed for cg_name");
            }
            return SUCCESS;
        }

        case AST_ASSIGNMENT: {
            // get left side identifier
            const char *lhs = node->data.assignment.name;
            if (!lhs) {
                return error(ERR_INTERNAL, "assignment without LHS name");
            }
            int is_global = is_global_identifier(lhs);

            // resolve local identifier if assignment is not to global
            if (!is_global) {
                int rc = sem2_resolve_identifier(table, lhs);
                if (rc != SUCCESS) {
                    return rc;
                }
            }

            // visit right side and infer its type
            data_type rhs_type = ST_UNKNOWN;
            int rc = sem2_visit_expr(table, node->data.assignment.value, &rhs_type);
            if (rc != SUCCESS) {
                return rc;
            }

            if (is_global) {
                // learn type of global from assignment rhs
                sem_global_type_learn(lhs, rhs_type);
            } else {
                st_data *sym = scopes_lookup(&table->scopes, lhs);

                // propagate cg_name from declaration into assignment node
                if (sym && sym->decl_node && sym->decl_node->type == AST_VAR_DECLARATION) {
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
                // update type for local variable or parameter
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
                    }
                }
            }

            return SUCCESS;
        }

        case AST_FUNCTION: {
            // get function node
            ast_function fn = node->data.function;
            if (!fn) {
                return SUCCESS;
            }

            // enter function scope for parameters and body
            sem_scope_enter_block(table);

            int rc = declare_parameter_list_in_current_scope(table, fn->parameters);
            if (rc != SUCCESS) {
                sem_scope_leave_block(table, "function params");
                return rc;
            }

            // assign cg_name for parameters based on current scope id
            const char *scope_str = sem_scope_ids_current(&table->ids);

            for (ast_parameter p = fn->parameters; p; p = p->next) {
                if (p->value_type != AST_VALUE_IDENTIFIER) {
                    continue;
                }
                const char *pname = p->value.string_value;
                if (!pname) {
                    continue;
                }
                st_data *sym = scopes_lookup(&table->scopes, pname);
                if (!sym || !sym->decl_node) {
                    continue;
                }
                char final[128];
                sem_build_cg_name(final, sizeof final, pname, scope_str);
                p->cg_name = my_strdup(final);
                sym->decl_node->data.declaration.cg_name = p->cg_name;
            }

            // visit function body statements
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
            ast_block body = node->data.getter.body;

            // enter getter scope and visit body
            sem_scope_enter_block(table);

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
            const char *param_name = node->data.setter.param;
            ast_block body = node->data.setter.body;

            // enter setter scope and declare parameter
            sem_scope_enter_block(table);

            if (param_name) {
                if (!scopes_declare_local(&table->scopes, param_name, true)) {
                    sem_scope_leave_block(table, "setter header");
                    return error(ERR_REDEF, "setter parameter redeclared: %s", param_name);
                }

                st_data *param_data = scopes_lookup_in_current(&table->scopes, param_name);
                if (param_data) {
                    param_data->symbol_type = ST_PAR;
                }
            }

            // visit setter body statements
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
            // get function call node
            ast_fun_call call = node->data.function_call;
            if (!call) {
                return SUCCESS;
            }

            // check call header resolve identifier arguments
            int ar = count_parameters(call->parameters);
            int rc = sem2_check_function_call(table, call->name, ar);
            if (rc != SUCCESS) {
                return rc;
            }

            // assign cg_name for identifier arguments based on scope id
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
            // visit return expression for side effects and checks
            if (node->data.return_expr.output) {
                return sem2_visit_expr(table, node->data.return_expr.output, NULL);
            }

            return SUCCESS;
        }

        case AST_BREAK:
        case AST_CONTINUE:
            // loop control statements are validated in pass 1
            return SUCCESS;

        case AST_IFJ_FUNCTION: {
            // get ifj.* function call node
            ast_ifj_function call = node->data.ifj_function;
            if (!call) {
                return SUCCESS;
            }

            // check call header
            int ar = count_parameters(call->parameters);
            char qname[128];
            const char *name = call->name ? sem_build_ifj_qname(call->name, qname, sizeof qname) : "(null)";
            int rc = sem2_check_function_call(table, name, ar);
            if (rc != SUCCESS) {
                return rc;
            }

            // assign cg_name for identifier arguments based on scope id
            const char *scope_str = sem_scope_ids_current(&table->ids);

            for (ast_parameter p = call->parameters; p; p = p->next) {
                if (p->value_type != AST_VALUE_IDENTIFIER) {
                    continue;
                }
                const char *pname = p->value.string_value;
                if (!pname) {
                    continue;
                }
                st_data *sym = scopes_lookup(&table->scopes, pname);
                if (!sym) {
                    continue;
                }
                // build codegen name
                char final[128];
                sem_build_cg_name(final, sizeof final, pname, scope_str);

                p->cg_name = my_strdup(final);
            }

            // resolve identifier arguments
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
    return SUCCESS;
}

/* -------------------------------------------------------------------------
 *  Block visitor (Pass 2)
 * ------------------------------------------------------------------------- */
/**
 * @brief Visits a block in Pass 2, manages scope entry/exit and visits all statements.
 * @param table Semantic context.
 * @param blk Block node.
 * @return SUCCESS or an error code.
 */
static int sem2_visit_block(semantic *table, ast_block blk) {
    if (!blk) {
        return SUCCESS;
    }

    // enter new scope for the block
    sem_scope_enter_block(table);
    // visit all statements in the block
    for (ast_node n = blk->first; n; n = n->next) {
        int rc = sem2_visit_statement_node(table, n);
        if (rc != SUCCESS) {
            sem_scope_leave_block(table, "sem2_visit_block");
            return rc;
        }
    }
    // leave block scope
    sem_scope_leave_block(table, "sem2_visit_block");
    return SUCCESS;
}

/* -------------------------------------------------------------------------
    *  Pass 2 entry point
 * ------------------------------------------------------------------------- */
/**
 * @brief Entry point for Pass 2 semantic analysis.
 * Reinitializes scopes and scope IDs, then traverses all class
 * root blocks, performing identifier resolution, call checking and type learning.
 * @param table Semantic context initialized by Pass 1.
 * @param syntax_tree AST root.
 * @return SUCCESS or the first error encountered.
 */
int semantic_pass2(semantic *table, ast syntax_tree) {
    // reinitialize scopes and scope IDs
    scopes_init(&table->scopes);
    sem_scope_ids_init(&table->ids);
    table->loop_depth = 0;

    // visit all class root blocks
    for (ast_class c = syntax_tree->class_list; c; c = c->next) {
        ast_block root = get_class_root_block(c);

        int rc = sem2_visit_block(table, root);
        if (rc != SUCCESS) {
            return rc;
        }
    }
    return SUCCESS;
}

/* =========================================================================
 *                    Global names retrieval
 * ========================================================================= */
/**
 * @brief Returns a copy of all global names for the code generator.
 * @param out_globals Output pointer to the allocated array of strings.
 * @param out_count Output pointer to the number of strings.
 * @return SUCCESS or ERR_INTERNAL on allocation failure.
 */
int semantic_get_globals(char ***out_globals, size_t *out_count) {
    // validate output pointers
    if (!out_globals || !out_count) {
        return error(ERR_INTERNAL, "semantic_get_globals: NULL output pointer");
    }
    // handle empty globals
    if (g_globals.count == 0) {
        *out_globals = NULL;
        *out_count = 0;
        return SUCCESS;
    }

    // allocate array of string pointers
    char **copy = malloc(g_globals.count * sizeof(char *));
    if (!copy) {
        return error(ERR_INTERNAL, "semantic_get_globals: allocation failed");
    }

    // copy each string
    for (size_t i = 0; i < g_globals.count; ++i) {
        size_t len = strlen(g_globals.items[i]);
        copy[i] = malloc(len + 1);
        if (!copy[i]) {
            for (size_t j = 0; j < i; ++j) {
                free(copy[j]);
            }
            free(copy);
            return error(ERR_INTERNAL, "semantic_get_globals: allocation failed (string)");
        }
        memcpy(copy[i], g_globals.items[i], len + 1);
    }

    // set output values
    *out_globals = copy;
    *out_count = g_globals.count;
    return SUCCESS;
}
