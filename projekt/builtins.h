/**
 * @file builtins.h
 * @brief Registration to the symtable for IFJ25 built-in functions.
 *
 * @authors Hana Liškařová (xliskah00)
 * @note  Project: IFJ / BUT FIT
 */

#ifndef BUILTINS_H
#define BUILTINS_H

#include <stdbool.h>
#include "symtable.h"

/**
 * @brief Kinds of built-in function parameters.
 *
 * - BUILTIN_PARAM_ANY:    any type is accepted,
 * - BUILTIN_PARAM_STRING: only string type is accepted,
 * - BUILTIN_PARAM_NUMBER: only number type is accepted.
 */
typedef enum {
    BUILTIN_PARAM_ANY = 0,
    BUILTIN_PARAM_STRING,
    BUILTIN_PARAM_NUMBER
} builtin_param_kind;

/**
 * @brief Feature switch for optional extensions.
 *
 * - ext_boolthen: enables Ifj.read_bool
 * - ext_statican: enables Ifj.is_int
 */
typedef struct {
    bool ext_boolthen;
    bool ext_statican;
} builtins_config;

/**
 * @brief Install all enabled built-ins into global function table.
 *
 * @param gtab Global symbol table used for function signatures.
 * @param cfg  Extension flags controlling which built-ins are registered.
 * @return true on success, false on allocation/insert failure.
 */
bool builtins_install(symtable *gtab, builtins_config cfg);

/**
 * @brief Retrieve parameter kind specification for a built-in function.
 *
 * @param qname      Fully-qualified name of the built-in function.
 * @param out_kinds  Output array to receive parameter kinds.
 * @param max_kinds  Maximum number of kinds to write to @p out_kinds.
 * @return Number of parameter kinds written to @p out_kinds.
 */
unsigned builtins_get_param_spec(const char *qname, builtin_param_kind *out_kinds, unsigned max_kinds);

/**
 * @brief Check if a given name is a built-in function.
 *
 * @param name Fully-qualified name to check.
 * @return true if @p name is a built-in function, false otherwise.
 */
bool builtins_is_builtin_qname(const char *name);

#endif /* BUILTINS_H */
