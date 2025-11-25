#ifndef SYMTABLE_H
#define SYMTABLE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "string.h"



#define SYMTABLE_SIZE 16381

typedef enum {
    ST_NULL,
    ST_INT,
    ST_DOUBLE,
    ST_STRING,
    ST_BOOL,
    ST_VOID,
    ST_U8
}data_type;

typedef enum {
    ST_VAR,
    ST_CONST,
    ST_FUN,
    ST_PAR
}symbol_type;

typedef struct st_data{
    data_type data_type;
    symbol_type symbol_type;
    bool defined;
    bool global;
    string ID;

    //functions
    int param_count;
    string *params;
    
    // main, block etc.
    string scope_name;

}st_data;

typedef struct st_symbol{
    char *key;
     st_data *data;
    bool occupied;
    bool deleted;
}st_symbol;

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


#endif // SYMTABLE_H