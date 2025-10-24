/**
 * @authors Martin Bíško (xbiskom00)

 * @file token.c
 * 
 * Implementation of the token structure and list of tokens
 * BUT FIT
 */

#include <stdio.h>
#include <stdlib.h>

#include "token.h"
#include "error.h"

/// @brief allocates memory for the token
/// @param - nothing
/// @return pointer to new token 
tokenPtr token_create(void) {
    tokenPtr token = malloc(sizeof(struct token));
    if (token == NULL)
        return NULL;

    token->value = string_create(TOKEN_INIT);
    token->value_float = 0;
    token->value_int = 0;
    token->type = T_NONE;
    return token;
}

/// @brief prints the token
/// @param t token to print
void token_format_string(tokenPtr t) {
    printf("Token type: %d\n", t->type);
    if(t->value) {
        printf("Token value: %s\n", t->value->data);
    } else {
        printf("Token value: (null)\n");
    }
    printf("Token value_float: %f\n", t->value_float);
    printf("Token value_int: %lld\n", t->value_int);
    printf("\n");
}

/// @brief clears the token (set all values to default)
/// @param t token to be cleared
void token_clear(tokenPtr t) {
    t->type = T_NONE;
    t->value_float = 0;
    t->value_int = 0;
    if (t->value) {
        string_destroy(t->value);
        t->value = NULL;
    }
}

/// @brief frees the memory allocated for the token
/// @param t token to be freed
void token_destroy(tokenPtr t) {
    if (t == NULL) return;
    string_destroy(t->value);
    free(t);
}

/// @brief sets the list of tokens to default values
/// @param list pointer to the list
void DLLTokens_Init(DLListTokens *list) {
    list->first = NULL;
    list->last = NULL;
    list->active = NULL;
    list->length = 0;
}

/// @brief Disposes of the list of tokens
/// @param  list list to dispose
void DLLTokens_Dispose(DLListTokens *list) {
    while (list->first != NULL)
    {
        list->active = list->first;
        list->first = list->first->next;
        token_destroy(list->active->token);
        free(list->active);
    }
    list->active = NULL;
    list->last = NULL;
    list->length = 0;
}

/// @brief inserts token at the begining of the list
/// @param list list to insert into
/// @param t token to be inserted
void DLLTokens_InsertFirst(DLListTokens *list, tokenPtr t) {
    // allocate memory for the new element
    DLLTokenElementPtr newElement = (DLLTokenElementPtr)malloc(sizeof(struct DLLTokenElement));
    if (newElement == NULL)
    {
        error(ERR_INTERNAL, "Failed to allocate memory");
        return;
    }

    // set the data for the new element
    newElement->token = t;
    newElement->prev = NULL;
    newElement->next = list->first;

    // if the list is not empty, update the current last element's next pointer
    if (list->first != NULL)
    {
        list->first->prev = newElement;
    }
    else
    {
        list->last = newElement;
    }

    list->first = newElement;

    list->length++;
}

/// @brief inserts token at the end of the list
/// @param list list to insert into
/// @param t token to be inserted
void DLLTokens_InsertLast(DLListTokens *list, tokenPtr t) {
     // allocate memory for the new element
    DLLTokenElementPtr newElement = (DLLTokenElementPtr)malloc(sizeof(struct DLLTokenElement));
    if (newElement == NULL)
    {
        error(ERR_INTERNAL, "Failed to allocate memory");
        return;
    }

    // set the data for the new element
    newElement->token = t;
    newElement->next = NULL;       // this will be the last element, so no next element
    newElement->prev = list->last; // its previous element is the current last element

    // if the list is not empty, update the current last element's next pointer
    if (list->last != NULL)
    {
        list->last->next = newElement;
    }
    else
    {
        // If the list was empty, the new element is also the first element
        list->first = newElement;
    }

    // Set the new element as the last element
    list->last = newElement;

    // Increase the list's length
    list->length++;
}

/// @brief sets active token to the first
/// @param list pointer to the list
void DLLTokens_First(DLListTokens *list) {
    list->active = list->first;
}

/// @brief sets active token to the next token
/// @param list pointer to the list
void DLLTokens_Next(DLListTokens *list) {
    if(list->active != NULL) {
        list->active = list->active->next;
    }
}

/// @brief prints the list of tokens
/// @param list list to print
void DLLTokens_Print(DLListTokens *list) {
    DLLTokenElementPtr tmp = list->first;

    while (tmp != NULL)
    {
        token_format_string(tmp->token);
        tmp = tmp->next;
    }
}

/// @brief Gets the token type of the active token, ignoring EOL tokens
/// @param tokenList The list of tokens
/// @return The token type of the active token, or T_NONE if there are no valid tokens
token_type get_token_type_ignore_eol(DLListTokens *tokenList) {
    DLLTokenElementPtr current = tokenList->active;

    while (current != NULL) {
        if (current->token->type != T_EOL) {
            return current->token->type;
        }
        current = current->next;
    }

    return T_NONE;
}