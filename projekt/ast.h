/**
 * @authors Šimon Dufek (xdufeks00)

 * @file ast.h
 * 
 * Implementation of the ast nodes and ast logic
 * BUT FIT
 */

#ifndef AST_H
#define AST_H

#include "string.h"

enum ast_node_type {
    AST_BLOCK,
    AST_CONDITION,
    AST_WHILE_LOOP,
    AST_BREAK,
    AST_CONTINUE,
    AST_EXPRESSION,
    AST_VAR_DECLARATION,
    AST_ASSIGNMENT,
    AST_FUNCTION,
    AST_CALL_FUNCTION,
    AST_RETURN,
    AST_GETTER,
    AST_SETTER,
    AST_IFJ_FUNCTION
};

typedef enum {
    AST_ID,
    AST_NONE,
    AST_NIL,
    AST_VALUE,
    AST_IDENTIFIER,
    AST_IFJ_FUNCTION_EXPR,
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
    AST_IS,
    AST_CONCAT
} ast_expression_type;

typedef enum {
    AST_VALUE_INT,
    AST_VALUE_FLOAT,
    AST_VALUE_STRING,
    AST_VALUE_NULL
} ast_value_type;

typedef struct ast_parameter {
    char *name;
    struct ast_parameter *next;
} *ast_parameter;

typedef struct ast_expression {
    ast_expression_type type;
    union {
        struct ast_binary_operation {
            struct ast_expression *left;
            struct ast_expression *right;
        } binary_op;
        struct ast_unary_operation {
            struct ast_expression *expression;
        } unary_op;
        struct ast_value {
            ast_value_type value_type;
            union {
                int int_value;
                double double_value;
                char *string_value;
            } value;
        } identity;
        struct ast_identifier {
            char *value;
        } identifier;
        struct ast_fun_call *function_call;
        struct ast_ifj_function *ifj_function;
    } operands;
} *ast_expression;

typedef struct ast_node {
    enum ast_node_type type;
    struct ast_node *next;

    union {
        struct ast_block *block;

        struct ast_condition {
            struct ast_expression *condition;
            struct ast_block *if_branch;
            struct ast_block *else_branch;
        } condition;

        struct ast_while {
            struct ast_expression *condition;
            struct ast_block *body;
        } while_loop;

        struct ast_expression *expression;

        struct ast_function *function;

        struct ast_fun_call *function_call;

        struct ast_declaration {
            char *name;
        } declaration;

        struct ast_assignment {
            char *name;
            struct ast_expression *value;
        } assignment;

        struct ast_return {
            ast_expression output;
        } return_expr;

        struct ast_getter {
            char *name;
            struct ast_block *body;
        } getter;

        struct ast_setter {
            char *name;
            char *param;
            struct ast_block *body;
        } setter;

        struct ast_ifj_function *ifj_function;
    } data;
} *ast_node;

typedef struct ast_function {
    char *name;
    struct ast_parameter *parameters;
    struct ast_block *code;
} *ast_function;

typedef struct ast_ifj_function {
    char *name;
    struct ast_parameter *parameters;
} *ast_ifj_function;


typedef struct ast_fun_call {
    char *name;
    struct ast_parameter *parameters;
} *ast_fun_call;

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
    char *name;
    struct ast_block *current;
    struct ast_class *next;
} *ast_class;

/// @brief Block of code
typedef struct ast_block {
    struct ast_node *first;
    struct ast_node *current;
    struct ast_block *parent;
    struct ast_node *next;
} *ast_block;

/// @brief Initialize the AST
/// @param ast pointer to the AST
void ast_init(ast *tree);

/// @brief Initializes an import node
/// @return initialized import node
ast_import ast_import_init();

/// @brief Initializes a class node - creates root if root_class_node is NULL or appends to the end of the list and set current to the new node
/// @param root_class_node pointer to the root class node
ast_class ast_class_init(ast_class *root_class_node);

/// @brief Initializes a block node - block of the class
/// @param class_node pointer to the  class node
void ast_block_init(ast_class *class_node);

/// @brief Sets the parent block as the current block
/// @param class_node pointer to the class node
void ast_block_parent(ast_class *class_node);

/// @brief Adds a new node at the end of the block
/// @param class_node pointer to the class node
/// @param type type of the AST node to be created
void ast_add_new_node(ast_class *class_node, enum ast_node_type type);

/// @brief Disposes of the AST
/// @param tree pointer to the AST
void ast_dispose(ast tree);

/// @brief Disposes of a class node
/// @param class_node pointer to the class node
void ast_class_dispose(ast_class class_node);

/// @brief Disposes of a block node
/// @param block_node pointer to the block node
void ast_block_dispose(ast_block block_node);

/// @brief Disposes of a node
/// @param node pointer to the AST node
void ast_node_dispose(ast_node node);

/// @brief Disposes of an expression node
/// @param expr pointer to the expression node
void ast_expression_dispose(ast_expression expr);

/// @brief Prints the AST
/// @param tree pointer to the AST
void ast_print(ast tree);

/// @brief Prints the class list
/// @param class_node pointer to the class node
/// @param offset offset of class node in the print
void ast_print_class(ast_class class_node, char *offset);

/// @brief Prints a block node
/// @param block_node pointer to the block node
/// @param offset offset for printing
void ast_print_block(ast_block block_node, char *offset);

/// @brief Prints a single AST node
/// @param node pointer to the AST node
/// @param offset offset for printing
void ast_print_node(ast_node node, char *offset);

/// @brief Prints an AST expression
/// @param expr pointer to the AST expression
/// @param offset offset for printing
void ast_print_expression(ast_expression expr, char *offset);

#endif // AST_H