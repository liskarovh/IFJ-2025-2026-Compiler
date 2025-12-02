/**
 * @authors Šimon Dufek (xdufeks00)

 * @file string.h
 * 
 * @brief Custom dynamic strings for easy string manipulation
 * BUT FIT
 */

#ifndef IFJ_STRING
#define IFJ_STRING

#include <stddef.h>
#include <stdbool.h>

#define DEFAULT_SIZE 16

/// @brief Structure for the dynamic string
typedef struct string {
    size_t length;
    size_t capacity;
    char *data;
} *string;

/// @brief creates a new string
/// @param init_capacity capacity of chars of the string 0 = DEFAULT_SIZE
/// @return pointer to the new string
string string_create(size_t init_capacity);

/// @brief appends a char to the string
/// @param str string to append to
/// @param c char to append
/// @return true if success, false if memory allocation error
bool string_append_char(string str, char c);

/// @brief appends a char to the string
/// @param str string to append to
/// @param literal pointer ro array of chars
/// @return true if success, false if memory allocation error
bool string_append_literal(string str, char *literal);


/// @brief concatenates two strings to the first one
/// @param str1 
/// @param str2 
/// @return true if success, false if memory allocation error
bool string_concat(string str1, string str2);

/// @brief clears the string (str = "")
/// @param str  string to clear
void string_clear(string str);

/// @brief frees the memory allocated for the string
/// @param str string to be freed
void string_destroy(string str);

/// @brief outputs string to a file
/// @param str a string you want to output
void string_to_file(string str);

#endif
