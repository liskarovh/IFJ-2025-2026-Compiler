/**
 * @authors Šimon Dufek (xdufeks00)

 * @file ast.c
 * 
 * Implementation of the ast nodes and ast logic
 * BUT FIT
 */

#include "ast.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/// @brief Initialize the AST
/// @param ast pointer to the AST
void ast_init(ast *tree) {
    *tree = malloc(sizeof(struct ast));
    (*tree)->import = NULL;
    (*tree)->class_list = NULL;
}

/// @brief Initializes an import node
/// @return initialized import node
ast_import ast_import_init() {
    ast_import import_node = malloc(sizeof(struct ast_import));
    import_node->path = NULL;
    import_node->alias = NULL;
    return import_node;
}

/// @brief Initializes a class node - creates root if root_class_node is NULL or appends to the end of the list and set current to the new node
/// @param root_class_node pointer to the root class node
/// @return initialized class node
ast_class ast_class_init(ast_class *root_class_node) {
    ast_class class_node = malloc(sizeof(struct ast_class));
    class_node->name = NULL;
    class_node->current = NULL;
    class_node->next = NULL;

    if (*root_class_node == NULL) {
        *root_class_node = class_node;
    } else {
        (*root_class_node)->next = class_node;
    }

    return class_node;
}

/// @brief Initializes a block node - block of the class
/// @param class_node pointer to the  class node
void ast_block_init(ast_class *class_node) {
    ast_block block_node = malloc(sizeof(struct ast_block));
    block_node->first = NULL;
    block_node->current = NULL;
    block_node->parent = NULL;
    block_node->next = NULL;

    (*class_node)->current = block_node;
}

/// @brief Sets the parent block as the current block
/// @param class_node pointer to the class node
void ast_block_parent(ast_class *class_node) {
    if((*class_node)->current->parent != NULL) {
        (*class_node)->current = (*class_node)->current->parent;
    }
}

/// @brief Adds a new node at the end of the block
/// @param class_node pointer to the class node
/// @param type type of the AST node to be created
void ast_add_new_node(ast_class *class_node, enum ast_node_type type) {
    ast_node new_node = malloc(sizeof(struct ast_node));
    new_node->type = type;

    if((*class_node)->current->current == NULL) {
        (*class_node)->current->first = new_node;
        new_node->next = NULL;
    } else {
        (*class_node)->current->current->next = new_node;
    }
    (*class_node)->current->current = new_node;

    switch (type)
    {
    case AST_BLOCK:
        new_node->data.block = malloc(sizeof(struct ast_block));
        new_node->data.block->first = NULL;
        new_node->data.block->current = NULL;
        new_node->data.block->next = NULL;
        new_node->data.block->parent = (*class_node)->current;
        (*class_node)->current = new_node->data.block;
        break;
    case AST_CONDITION: 
        new_node->data.condition.condition = NULL;
        new_node->data.condition.if_branch = NULL;
        new_node->data.condition.else_branch = NULL;
        break;
    case AST_WHILE_LOOP: 
        new_node->data.while_loop.condition = NULL;
        new_node->data.while_loop.proceed = NULL;
        break;
    case AST_EXPRESSION: 
        new_node->data.expression = NULL;
        break;
    case AST_VAR_DECLARATION: 
        new_node->data.declaration.name = NULL;
        break;
    case AST_ASSIGNMENT: 
        new_node->data.assignment.name = NULL;
        new_node->data.assignment.value = NULL;
        break;
    case AST_FUNCTION: 
    new_node->data.function = malloc(sizeof(struct ast_function));
        new_node->data.function->name = NULL;
        new_node->data.function->parameters = NULL;
        new_node->data.function->code = malloc(sizeof(struct ast_block));
        new_node->data.function->code->first = NULL;
        new_node->data.function->code->current = NULL;
        new_node->data.function->code->parent = (*class_node)->current;
        new_node->data.function->code->next = NULL;
        break;
    case AST_CALL_FUNCTION:
        new_node->data.function_call = malloc(sizeof(struct ast_fun_call));
        new_node->data.function_call->name = NULL;
        new_node->data.function_call->parameters = NULL;
        break;
    case AST_RETURN: 
        new_node->data.return_expr.output = NULL;
        break;
    }
}

/// @brief Disposes of the AST
/// @param tree pointer to the AST
void ast_dispose(ast tree) {
    free(tree->import);
    ast_class_dispose(tree->class_list);
    free(tree);
}

/// @brief Disposes of a class node
/// @param class_node pointer to the class node
void ast_class_dispose(ast_class class_node) {
    if(class_node == NULL) {
        return;
    }

    ast_block_dispose(class_node->current);

    ast_class_dispose(class_node->next);
    free(class_node);
}

/// @brief Disposes of a block node
/// @param block_node pointer to the block node
void ast_block_dispose(ast_block block_node) {
    if(block_node == NULL) {
        return;
    }

    ast_node current_node = block_node->first;
    while(current_node != NULL) {
        ast_node_dispose(current_node);
        current_node = current_node->next;
    }

    free(block_node);
}

/// @brief Disposes of a node
/// @param node pointer to the AST node
void ast_node_dispose(ast_node node) {
    if(node == NULL) {
        return;
    }

    switch (node->type)
    {
    case AST_BLOCK:
        break;
    case AST_CONDITION:
        break;
    case AST_WHILE_LOOP:
        break;
    case AST_EXPRESSION:
        break;
    case AST_VAR_DECLARATION:
        break;
    case AST_ASSIGNMENT:
        break;
    case AST_FUNCTION: {
        ast_parameter param = node->data.function->parameters;
        while(param != NULL) {
            free(param);
            param = param->next;
        }
        ast_block_dispose(node->data.function->code);
        free(node->data.function);
        break;
    }
    case AST_CALL_FUNCTION: {
        ast_parameter param = node->data.function_call->parameters;
        while(param != NULL) {
            free(param);
            param = param->next;
        }
        free(node->data.function_call);
        break;
    }
    case AST_RETURN:
        break;
    }
}

/// @brief Prints the AST
/// @param tree pointer to the AST
void ast_print(ast tree) {
    if (tree == NULL) {
        printf("Empty tree\n");
        return;
    }

    printf("Program\n");
    if(tree->import != NULL) {
        printf("|\n");
        printf("+-- IMPORT (path: %s, alias: %s)\n", tree->import->path, tree->import->alias);
    }
    if(tree->class_list != NULL) {
        printf("|\n");
        printf("+-- CLASS LIST\n");
        ast_class class = tree->class_list;
        do {
            ast_print_class(class, "        ");
            class = class->next;
        } while(class != NULL);
        
    }
}

/// @brief Prints the class list
/// @param class_node pointer to the class node
/// @param offset offset of class node in the print
void ast_print_class(ast_class class_node, char *offset) {
    printf("%s|\n", offset);
    printf("%s+-- CLASS (name: %s)\n", offset, class_node->name);
    char newOffset[100]; 
    strcpy(newOffset, offset);
    strcat(newOffset, class_node->next == NULL ? "    " : "|   ");
    ast_print_block(class_node->current, newOffset);
}

/// @brief Prints a block node
/// @param block_node pointer to the block node
/// @param offset offset for printing
void ast_print_block(ast_block block_node, char *offset) {
    printf("%s|\n", offset);
    printf("%s+-- BLOCK\n", offset);
    ast_node current_node = block_node->first;
    while(current_node != NULL) {
        ast_print_node(current_node, offset);
        current_node = current_node->next;
    }
}

/// @brief Prints a single AST node
/// @param node pointer to the AST node
/// @param offset offset for printing
void ast_print_node(ast_node node, char *offset) {
    switch (node->type)
    {
    case AST_BLOCK: {
        char newOffset[100]; 
        strcpy(newOffset, offset);
        strcat(newOffset, node->data.block->parent == NULL ? "    " : "|   ");
        ast_print_block(node->data.block, newOffset);
        break;
    }
    case AST_CONDITION:
        break;
    case AST_WHILE_LOOP:
        break;
    case AST_EXPRESSION:
        break;
    case AST_VAR_DECLARATION:
        printf("%s    |\n", offset);
        printf("%s    +-- VAR DECLARATION (name: %s)\n", offset, node->data.declaration.name);
        break;
    case AST_ASSIGNMENT:
        break;
    case AST_FUNCTION:
        printf("%s    |\n", offset);
        printf("%s    +-- FUNCTION (name: %s", offset, node->data.function->name);
        if(node->data.function->parameters != NULL) {
            printf(", parameters: ");
            ast_parameter parameter = node->data.function->parameters;
            while(parameter != NULL) {
                printf("%s", parameter->name);
                parameter = parameter->next;
                if(parameter != NULL) {
                    printf(", ");
                }
            }
        }
        printf(")\n");
        char newOffset[100]; 
        strcpy(newOffset, offset);
        strcat(newOffset, "        ");
        ast_print_block(node->data.function->code, newOffset);
        break;
    case AST_CALL_FUNCTION:
        printf("%s    |\n", offset);
        printf("%s    +-- FUNCTION CALL (name: %s", offset, node->data.function_call->name);
        if(node->data.function_call->parameters != NULL) {
            printf(", parameters: ");
            ast_parameter parameter = node->data.function_call->parameters;
            while(parameter != NULL) {
                printf("%s", parameter->name);
                parameter = parameter->next;
                if(parameter != NULL) {
                    printf(", ");
                }
            }
        }
        printf(")\n");
        break;
    case AST_RETURN:
        printf("%s    |\n", offset);
        printf("%s    +-- RETURN\n", offset);
        break;
    }
}