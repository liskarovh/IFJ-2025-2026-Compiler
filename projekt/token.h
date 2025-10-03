/**
 * @authors Martin Bíško (xbiskom00)

 * @file token.h
 * 
 * Implementation of the token structure and list of tokens
 * BUT FIT
 */

#ifndef TOKEN_H
#define TOKEN_H

#include "string.h"

#define TOKEN_INIT 1

/// @brief all token types that can be found in the code
typedef enum {
    T_NONE,

    T_IDENTIFIER,
    T_GLOB_IDENTIFIER,
    T_EOF,
    T_EOL,

    // keywords
    T_CLASS,
    T_IF,
    T_ELSE,
    T_IS,
    T_NULL,
    T_RETURN,
    T_VAR,
    T_WHILE,
    T_STATIC,
    T_IMPORT,
    T_FOR,
    T_NUM,
    T_STRING,
    T_NULLTYPE,
    T_IFJ,
    T_IN,
    T_BREAK,
    T_CONTINUE,

    // literars
    T_INT,
    T_HEXINT,
    T_FLOAT,
    T_STRING,
    T_TRUE,
    T_FALSE,

    // operators
    T_PLUS,     // +
    T_MINUS,    // -
    T_MUL,      // *
    T_DIV,      // /
    T_LT,       // <
    T_LE,       // <=
    T_GT,       // >
    T_GE,       // >=
    T_EQ,       // ==
    T_NEQ,      // !=
    T_AND,      // &&
    T_OR,       // ||
    T_NOT,      // !
    T_ASSIGN,   // =
    T_LPAREN,   // (
    T_RPAREN,   // )
    T_LBRACE,   // {
    T_RBRACE,   // }
    T_COMMA,    // ,
    T_DOT,      // .
    T_COLON,    // :
    T_QUESTION  // ?
    } token_type;


/// @brief structure for the token
typedef struct token {
    token_type type;
    string value;
    float value_float;
    int value_int;
    int depth;
} *tokenPtr;

/// @brief allocates memory for the token
/// @param - nothing
/// @return pointer to new token 
tokenPtr token_create(void);

/// @brief prints the token
/// @param t token to print
void token_format_string(tokenPtr t);

/// @brief clears the token (set all values to default)
/// @param t token to be cleared
void token_clear(tokenPtr t);

/// @brief frees the memory allocated for the token
/// @param t token to be freed
void token_destroy(tokenPtr);

/// @brief Structure for the element of the list of tokens
typedef struct DLLTokenElement
{
    tokenPtr token;
    struct DLLTokenElement *next;
    struct DLLTokenElement *prev;
} *DLLTokenElementPtr;

/// @brief Structure for the list of tokens
typedef struct
{
    // Pointer to the first element
    DLLTokenElementPtr first;
    // Pointer to the last element
    DLLTokenElementPtr active;
    // Pointer to the active element
    DLLTokenElementPtr last;
    // Length of the list
    int length;
} DLListTokens;

/// @brief sets the list of tokens to default values
/// @param list pointer to the list
void DLLTokens_Init(DLListTokens *list);

/// @brief Disposes of the list of tokens
/// @param  list list to dispose
void DLLTokens_Dispose(DLListTokens *list);

/// @brief inserts token at the begining of the list
/// @param list list to insert into
/// @param t token to be inserted
void DLLTokens_InsertFirst(DLListTokens *list, tokenPtr t);

/// @brief inserts token at the end of the list
/// @param list list to insert into
/// @param t token to be inserted
void DLLTokens_InsertLast(DLListTokens *list, tokenPtr t);

/// @brief sets active token to the first
/// @param list pointer to the list
void DLLTokens_First(DLListTokens *list);

/// @brief sets active token to the next token
/// @param list pointer to the list
void DLLTokens_Next(DLListTokens *list);

/// @brief prints the list of tokens
/// @param list list to print
void DLLTokens_Print(DLListTokens *list);

#endif // TOKEN_H