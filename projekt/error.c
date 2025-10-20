/**
 * @authors Šimon Dufek (xdufeks00)

 * @file error.c
 * 
 * Error handeling and output to stderr
 * BUT FIT
 */

#include <stdarg.h>
#include <stdio.h>

#include "error.h"

/// @brief Prints an error message to stderr and returns the exit code
/// @param exitcode 
/// @param fmt 
/// @param  ...
/// @return exitcode
int error(int exit_code, const char *fmt, ...){
    va_list args;
    va_start(args, fmt);

    #ifndef NERROR
    fprintf(stderr, "Error:");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    #endif

    va_end(args);
    return exit_code;
}