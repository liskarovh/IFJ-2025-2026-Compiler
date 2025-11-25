/**
* @authors Martin Bíško (xbiskom00)
 *
 * @file parser.h
 *
 * Syntax analyzer implementation
 * BUT FIT
 */

#ifndef IFJ_PARSER_H
#define IFJ_PARSER_H

#include "token.h"
#include "ast.h"

enum grammar_rule {
    GRAMMAR_ID,
    GRAMMAR_PROGRAM,
    GRAMMAR_IMPORT,
    GRAMMAR_IMPORT_IFJ25,
    GRAMMAR_IMPORT_FOR,
    GRAMMAR_IMPORT_IFJ,
    GRAMMAR_CLASS_LIST,
    GRAMMAR_CLASS_DEF,
    GRAMMAR_BODY,
    GRAMMAR_COMMAND_LIST,
    GRAMMAR_COMMAND,
    GRAMMAR_FUN_DEF,
    GRAMMAR_PARAMS,
    GRAMMAR_PARAM_LIST,
    GRAMMAR_DECLARATION,
    GRAMMAR_ASSIGNMENT,
    GRAMMAR_EXPRESSION,
    GRAMMAR_EXP_OPERATOR,
    GRAMMAR_CONDITION,
    GRAMMAR_COND_EXPRESSION,
    GRAMMAR_COND_OPERATOR,
    GRAMMAR_FOR,
    GRAMMAR_WHILE,
    GRAMMAR_FUN_CALL,
    GRAMMAR_RETURN,
    GRAMMAR_GETTER,
    GRAMMAR_SETTER,
    GRAMMAR_IFJ_CALL
};

/// @brief Parse the token list and generate the AST
/// @param tokenList The list of tokens to parse
/// @param out_ast The output AST
/// @param expected_rule The grammar rule to apply
/// @return SUCCESS on success, or an error code on failure
int parser(DLListTokens *tokenList, ast out_ast, enum grammar_rule expected_rule);

#endif // IFJ_PARSER_H