/**
 * @file symtable.h
 * @brief Symbol table definition.
 *
 * This header file defines the structures and functions for managing a symbol table,
 * which is used to store information about identifiers.
 *
 * @authors Matej Kurta (xkurtam00)
 * @note Project: IFJ / BUT FIT
 */

#ifndef SYMTABLE_H
#define SYMTABLE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "string.h"
#include "ast.h"

#define SYMTABLE_SIZE 16381

/**
 * @brief Data type enumeration.
 */
typedef enum {
    ST_UNKNOWN = -1,
    ST_NULL,
    ST_INT,
    ST_DOUBLE,
    ST_STRING,
    ST_BOOL,
    ST_VOID,
    ST_U8
} data_type;

/**
 * @brief Symbol type enumeration.
 */
typedef enum {
    ST_VAR,
    ST_CONST,
    ST_FUN,
    ST_PAR,
    ST_GLOB,
    ST_GETTER,
    ST_SETTER
} symbol_type;

/**
 * @brief Symbol data structure.
 */
typedef struct st_data {
    data_type data_type;
    symbol_type symbol_type;
    bool defined;
    bool global;
    string ID;

    //functions
    int param_count;
    string *params;
    ast_node decl_node;

    // main, block etc.
    string scope_name;
} st_data;

/**
 * @brief Symbol table entry structure - symbol.
 */
typedef struct st_symbol {
    char *key;
    st_data *data;
    bool occupied;
    bool deleted;
} st_symbol;

/**
 * @brief Symbol table structure.
 */
typedef struct {
    unsigned size;
    st_symbol *table;
} symtable;


/**
 * @brief Computes a hash value for a given key string.
 * @param key Input string to hash.
 * @return Unsigned integer hash value.
 */
unsigned st_hash(char *key);

/**
 * @brief Initializes a new symbol table.
 * @return Pointer to an allocated and initialized symtable structure, or NULL on failure.
 */
symtable *st_init(void);

/**
 * @brief Finds a symbol in the table by its key.
 * @param table Pointer to the symbol table.
 * @param key The identifier to look up.
 * @return Pointer to the found symbol, or NULL if not found.
 */
st_symbol *st_find(symtable *table, char *key);

/**
 * @brief Inserts a new symbol into the table if it does not already exist.
 * @param table Pointer to the symbol table.
 * @param key Identifier name (string).
 * @param type Symbol type (variable, constant, function, etc.).
 * @param defined True if the symbol is defined, false otherwise.
 */
void st_insert(symtable *table, char *key, symbol_type type, bool defined);

/**
 * @brief Retrieves symbol data by key.
 * @param table Pointer to the symbol table.
 * @param key Identifier to look up.
 * @return Pointer to symbol data, or NULL if not found.
 */
st_data *st_get(symtable *table, char *key);

/**
 * @brief Frees all memory allocated for the symbol table.
 * @param table Pointer to the symbol table to free.
 */
void st_free(symtable *table);

/**
 * @brief Print a human-readable dump of the symbol table.
 *        Each occupied entry prints: index, key, kind, param_count (if any).
 * @param table Symbol table to dump.
 * @param out   Output stream (stdout/stderr/file).
 */
void st_dump(symtable *table, FILE *out);

/**
 * @brief Callback function type for iterating over symbol table entries.
 * @param key The key of the current symbol.
 * @param data Pointer to the symbol's data.
 * @param user_data User-defined data passed to the callback.
 */
typedef void (*st_iter_cb)(const char *key, st_data *data, void *user_data);

/**
 * @brief Iterate over all entries in the symbol table.
 *        For each (key, data) calls cb(key, data, user_data).
 *        @param   t The symbol table to iterate over.
 *        @param   cb The callback function to call for each occupied symbol.
 *        @param   user_data User-defined data to pass to the callback function.
 */
void st_foreach(symtable *t, st_iter_cb cb, void *user_data);

/**
 * @brief Duplicates a string by allocating new memory.
 * @param s Input string to duplicate.
 * @return Pointer to the newly allocated duplicate string, or NULL on failure.
 */
char *my_strdup(const char *s);

#endif // SYMTABLE_H
