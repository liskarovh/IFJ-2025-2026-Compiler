/**
* @file builtins.h
 * @brief Registration of IFJ25 built-in functions into the global symbol table.
 *
 * All names are stored fully-qualified as "Ifj.read_str", "Ifj.write", etc.
 * Only arity is persisted into the symtable for Pass 1; richer typing can be
 * optionally added in later passes.
 *
 * @authors
 *   Hana Liškařová (xliskah00)
 */

#ifndef BUILTINS_H
#define BUILTINS_H

#include <stdbool.h>
#include "symtable.h"

/**
 * @brief Feature switches for optional extensions.
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
 * @brief Quick check whether a function name is an IFJ built-in.
 * @param name Fully-qualified candidate name.
 * @return true if it starts with "Ifj.", otherwise false.
 */
static inline bool builtins_is_builtin_qname(const char *name) {
    return name && name[0]=='I' && name[1]=='f' && name[2]=='j' && name[3]=='.';
}

#endif /* BUILTINS_H */
