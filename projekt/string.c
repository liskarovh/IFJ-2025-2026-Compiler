/**
 * @authors Šimon Dufek (xdufeks00)

 * @file string.c
 * 
 * Custom dynamic strings for easy string manipulation
 * BUT FIT
 */

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "string.h"
#include "error.h"

/// @brief creates a new string
/// @param init_capacity capacity of chars of the string 0 = DEFAULT_SIZE
/// @return pointer to the new string
string string_create(size_t init_capacity)
{
    if (init_capacity == 0)
        init_capacity = DEFAULT_SIZE;
    string str = malloc(sizeof(struct string));
    if (str == NULL)
    {
        return NULL;
    }
    str->data = malloc(init_capacity + 1);
    if (str->data == NULL)
    {
        free(str);
        return NULL;
    }
    str->capacity = init_capacity;
    string_clear(str);
    return str;
}

/// @brief internal function to double the capacity of the string
/// @param str string to double
/// @return true if success, false if memory allocation error
bool __double_string(string str)
{
    char *new_string = realloc(str->data, str->capacity * 2 + 1);
    if (new_string == NULL)
        return false;
    str->data = new_string;
    str->capacity *= 2;
    return true;
}

/// @brief clears the string (str = "")
/// @param str  string to clear
void string_clear(string str)
{
    str->data[0] = '\0';
    str->length = 0;
}

/// @brief frees the memory allocated for the string
/// @param str string to be freed
void string_destroy(string str)
{
    if (!str) return;
    if (str->data) {
        free(str->data);
        str->data = NULL;
    }
    free(str);
}

/// @brief appends a char to the string
/// @param str string to append to
/// @param c char to append
/// @return true if success, false if memory allocation error
bool string_append_char(string str, char c)
{
    if (str->capacity < str->length + 1)
    {
        if (!__double_string(str))
        {
            error(ERR_INTERNAL, "Memory alocation error");
            return false;
        }
    }
    // if(c == ' ')
    //     c = '_';

    str->data[str->length++] = c;
    str->data[str->length] = '\0';
    return true;
}

/// @brief appends a char to the string
/// @param str string to append to
/// @param literal pointer to string
/// @return true if success, false if memory allocation error
bool string_append_literal(string str, char * literal) {
    if(literal == NULL)
        return true;
    while (str->capacity < str->length + strlen(literal)) {
        if (!__double_string(str)) {
            error(ERR_INTERNAL, "Memory alocation error");
            return false;
        }
    }

    strcpy(str->data + str->length, literal);
    str->length = strlen(str->data);
    return true;
}

/// @brief concatenates two strings to the first one
/// @param str1 
/// @param str2 
/// @return 
bool string_concat(string str1, string str2){
    // loop over the second string and append it to the first one
    for (size_t i = 0; i < str2->length; i++)
    {
        if(str2->data[i] == '\0') break;
        if (!string_append_char(str1, str2->data[i]))
        {
            return false;
        }
    }
    return true;
}

/// @brief outputs string to a file
/// @param str a string you want to output
void string_to_file(string str) {
    FILE *file = fopen("ifjcode24", "w");
    if (file == NULL) {
        fprintf(stderr, "Error: Could not open file for writing.\n");
        return;
    }

    if (fwrite(str->data, sizeof(char), str->length, file) != str->length) {
        fprintf(stderr, "Error: Could not write entire string to file.\n");
    }

    fclose(file);
}