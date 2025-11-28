/**
 * @file builtins.h
 * @brief Registration and metadata for IFJ25 built-in functions.
 *
 * All built-in functions are stored fully-qualified as "Ifj.read_str",
 * "Ifj.write", etc. During Pass 1 their signatures are inserted into the
 * global function table with keys of the form "<qname>#<arity>"
 * (for example "Ifj.write#1").
 *
 * Only the arity is persisted in the symtable entry itself; lightweight
 * parameter-kind information is available through builtins_get_param_spec()
 * for semantic checks.
 *
 * @authors
 *   Hana Liškařová (xliskah00)
 */

#ifndef BUILTINS_H
#define BUILTINS_H

#include <stdbool.h>
#include "symtable.h"

/**
 * @brief Compile-time classification of a single built-in parameter.
 *
 * BUILTIN_PARAM_ANY    – the compiler does not restrict the type
 *                        (e.g. Ifj.write, Ifj.str, Ifj.is_int),
 * BUILTIN_PARAM_STRING – the parameter must be a string (e.g. Ifj.length),
 * BUILTIN_PARAM_NUMBER – the parameter must be a number (e.g. Ifj.floor).
 */
typedef enum {
    BUILTIN_PARAM_ANY = 0,
    BUILTIN_PARAM_STRING,
    BUILTIN_PARAM_NUMBER
} builtin_param_kind;

/**
 * @brief Feature switches for optional extensions.
 *
 * - ext_boolthen: enables Ifj.read_bool
 * - ext_statican: enables Ifj.is_int
 */
typedef struct {
    bool ext_boolthen;
    bool ext_statican;
} builtins_config;

/**
 * @brief Install all enabled built-ins into the given global function table.
 *
 * For each built-in, a symbol is inserted under key "<qname>#<arity>".
 * Example: "Ifj.write#1".
 *
 * On success, the created entry has:
 *   - st_data.symbol_type = ST_FUN
 *   - st_data.param_count = arity
 *
 * @param gtab Global symbol table used for function signatures.
 * @param cfg  Extension flags controlling which built-ins are registered.
 * @return true on success, false on allocation/insert failure.
 */
bool builtins_install(symtable *gtab, builtins_config cfg);

/**
 * @brief Return the declared parameter kinds for a built-in function.
 *
 * This is a read-only view into the canonical built-in table.
 *
 * @param qname      Fully-qualified name (for example "Ifj.substring").
 * @param out_kinds  Optional output buffer; if non-NULL, the first
 *                   min(arity, max_kinds) parameter kinds are written here.
 * @param max_kinds  Size of @p out_kinds buffer.
 * @return           Arity of the built-in (number of parameters), or 0 if
 *                   @p qname is not a known built-in.
 */
unsigned builtins_get_param_spec(const char *qname, builtin_param_kind *out_kinds, unsigned max_kinds);

/**
 * @brief Quick check whether a function name is an IFJ built-in.
 *
 * A function is considered built-in if its fully-qualified name starts
 * with the "Ifj." prefix.
 *
 * @param name Fully-qualified candidate name.
 * @return true if the name starts with "Ifj.", otherwise false.
 */
bool builtins_is_builtin_qname(const char *name);

#endif /* BUILTINS_H */