/**
 * @authors Šimon Dufek (xdufeks00)

 * @file ast.h
 * 
 * Implementation of the ast nodes and ast logic
 * BUT FIT
 */

#ifndef AST_H
#define AST_H

enum ast_node_type {
    AST_CONDITION,
    AST_WHILE_LOOP,
    AST_EXPRESSION,
    AST_VAR_DECLARATION,
    AST_ASSIGNMENT,
    AST_FUNCTION,
    AST_RETURN
};

enum ast_expression_type {
    AST_NONE,
    AST_NIL,
    AST_IDENTIFIER,
    AST_STRING,
    AST_BOOL,
    AST_INTEGER,
    AST_DOUBLE,
    AST_FUNCTION_CALL,
    AST_NOT_NULL,
    AST_NOT,
    AST_ADD,
    AST_SUB,
    AST_MUL,
    AST_DIV,
    AST_EQUALS,
    AST_NOT_EQUAL,
    AST_LT,
    AST_LE,
    AST_GT,
    AST_GE,
    AST_TERNARY,
    AST_AND,
    AST_OR,
    AST_CONCAT
};

typedef struct ast_node {
    enum ast_node_type type;

    union {
        struct ast_condition {
            struct ast_expression *condition;
            struct ast_block *if_branch;
            struct ast_block *else_branch;
        } condition;

        struct ast_while {
            struct ast_expression *condition;
            struct ast_sequence *proceed;
        } while_loop;

        struct ast_expression *expression;

        struct ast_function *function;

        struct ast_decaration {
            char *name;
        } declaration;

        struct ast_assignment {
            char *name;
            struct ast_expression *value;
        } assignment;

        struct ast_return {
            ast_expression output;
        } return_expr;
    } data;
} *ast_node;

typedef struct ast_expression {
    enum ast_expression_type type;
    union {
        struct ast_binary_operation {
            struct ast_expression *left;
            struct ast_expression *right;
        } binary_op;
        struct ast_unary_operation {
            struct ast_expression *expression;
        } unary_op;
        struct ast_value {
            union {
                double number_val;
                char *string_value;
            } value;
        } identity;
        struct ast_identifier {
            char *value;
        } identifier;
        struct ast_function_call {
            char *name;
            unsigned parameter_count;
        } function_call;
    } operands;
} *ast_expression;

typedef struct ast_function {
    char *name;
    struct ast_block *code;
} *ast_function;

typedef struct ast_import {
    char *path;
    char *alias;
} *ast_import;

/// @brief AST 
typedef struct ast {
    struct ast_import *import;
    struct ast_class *class_list;
} *ast;

/// @brief List of classes
typedef struct ast_class {
    struct ast_block *current;
    struct class_list *next;
} *ast_class;

/// @brief Block of code
typedef struct ast_block {
    struct ast_node *current;
    struct ast_block *next;
} *ast_block;

#endif // AST_H