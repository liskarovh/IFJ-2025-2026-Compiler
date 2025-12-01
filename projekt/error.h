/**
 * @authors Šimon Dufek (xdufeks00)
 *
 * @file error.h;
 * 
 * @brief Error handeling and output to stderr
 * BUT FIT
 */

#ifndef IFJ_ERROR
#define IFJ_ERROR

#include <errno.h>

//codes

#define SUCCESS             0   //Successful compilation
#define ERR_LEX             1   //Lexical anal. error
#define ERR_SYN             2   //Syntactic anal. error
#define ERR_DEF             3   //Function or variable not defined
#define ERR_REDEF           4   //Redefinition function or variable
#define ERR_ARGNUM          5   //Wrong number of arguments in function call
#define ERR_EXPR            6   //Wrong type in expression
#define ERR_SEM             10  //All other semnatic errors
#define ERR_RUN_PRMT        25  //Runtime error - wrong parameter type
#define ERR_RUN_EXPR        26  //Runtime error - wrong type in expression
#define ERR_INTERNAL        99  //Internal compiler error

/// @brief Prints an error message to stderr and returns the exit code
/// @param exitcode 
/// @param fmt 
/// @param  ...
/// @return exitcode
int error(int exitcode, const char *fmt, ...);

#endif
