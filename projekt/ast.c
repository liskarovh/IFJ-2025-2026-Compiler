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
        new_node->data.condition.if_branch = malloc(sizeof(struct ast_block));
        new_node->data.condition.if_branch->first = NULL;
        new_node->data.condition.if_branch->current = NULL;
        new_node->data.condition.if_branch->next = NULL;
        new_node->data.condition.if_branch->parent = (*class_node)->current;
        new_node->data.condition.else_branch = malloc(sizeof(struct ast_block));
        new_node->data.condition.else_branch->first = NULL;
        new_node->data.condition.else_branch->current = NULL;
        new_node->data.condition.else_branch->next = NULL;
        new_node->data.condition.else_branch->parent = (*class_node)->current;
        break;
    case AST_WHILE_LOOP: 
        new_node->data.while_loop.condition = NULL;
        new_node->data.while_loop.body = malloc(sizeof(struct ast_block));
        new_node->data.while_loop.body->first = NULL;
        new_node->data.while_loop.body->current = NULL;
        new_node->data.while_loop.body->next = NULL;
        new_node->data.while_loop.body->parent = (*class_node)->current;
        break;
    case AST_BREAK:
        // No additional data needed for BREAK
        break;
    case AST_CONTINUE:
        // No additional data needed for CONTINUE
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
    case AST_GETTER:
        new_node->data.getter.name = NULL;
        new_node->data.getter.body = malloc(sizeof(struct ast_block));
        new_node->data.getter.body->first = NULL;
        new_node->data.getter.body->current = NULL;
        new_node->data.getter.body->next = NULL;
        new_node->data.getter.body->parent = (*class_node)->current;
        break;
    case AST_SETTER:
        new_node->data.setter.name = NULL;
        new_node->data.setter.param = NULL;
        new_node->data.setter.body = malloc(sizeof(struct ast_block));
        new_node->data.setter.body->first = NULL;
        new_node->data.setter.body->current = NULL;
        new_node->data.setter.body->next = NULL;
        new_node->data.setter.body->parent = (*class_node)->current;
        break;
    case AST_IFJ_FUNCTION:
        new_node->data.ifj_function = malloc(sizeof(struct ast_ifj_function));
        new_node->data.ifj_function->name = NULL;
        new_node->data.ifj_function->parameters = NULL;
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
        ast_block_dispose(node->data.condition.if_branch);
        ast_block_dispose(node->data.condition.else_branch);
        break;
    case AST_WHILE_LOOP:
        ast_block_dispose(node->data.while_loop.body);
        break;
    case AST_BREAK:
        break;
    case AST_CONTINUE:
        break;
    case AST_EXPRESSION:
        ast_expression_dispose(node->data.expression);
        break;
    case AST_VAR_DECLARATION:
        break;
    case AST_ASSIGNMENT:
        ast_expression_dispose(node->data.assignment.value);
        break;
    case AST_FUNCTION: {
        ast_parameter param = node->data.function->parameters;
        ast_parameter temp;
        while(param != NULL) {
            temp = param->next;
            free(param);
            param = temp;
        }
        ast_block_dispose(node->data.function->code);
        free(node->data.function);
        break;
    }
    case AST_CALL_FUNCTION: {
        ast_parameter param = node->data.function_call->parameters;
        ast_parameter temp;
        while(param != NULL) {
            temp = param->next;
            free(param);
            param = temp;
        }
        free(node->data.function_call);
        break;
    }
    case AST_RETURN:
        break;
    case AST_GETTER:
        ast_block_dispose(node->data.getter.body);
        break;
    case AST_SETTER:
        ast_block_dispose(node->data.setter.body);
        break;
    case AST_IFJ_FUNCTION: {
        ast_parameter param = node->data.ifj_function->parameters;
        while(param != NULL) {
            ast_parameter to_free = param;
            param = param->next;
            free(to_free);
        }
        free(node->data.ifj_function);
        break;
    }
    }
}

/// @brief Disposes of an expression node
/// @param expr pointer to the expression node
void ast_expression_dispose(ast_expression expr) {
    if(expr == NULL) {
        return;
    }
    
    if(expr->type != AST_VALUE) {
        ast_expression_dispose(expr->operands.binary_op.left);
        ast_expression_dispose(expr->operands.binary_op.right);
        return;
    } 

    free(expr);
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
    case AST_CONDITION: {
        printf("%s    |\n", offset);
        printf("%s    +-- CONDITION\n", offset);
        printf("%s    |   |\n", offset);
        printf("%s    |   +-- COND\n", offset);

        char newOffset[100]; 
        strcpy(newOffset, offset);
        strcat(newOffset, "    |   |");
        ast_print_expression(node->data.condition.condition, newOffset);

        strcat(newOffset, "    ");
        printf("%s    |   |\n", offset);
        printf("%s    |   +-- BODY\n", offset);
        ast_print_block(node->data.condition.if_branch, newOffset);

        printf("%s    |   |\n", offset);
        printf("%s    |   +-- ELSE\n", offset);
        ast_print_block(node->data.condition.else_branch, newOffset);
        
        break;
    }
    case AST_WHILE_LOOP: {
        printf("%s    |\n", offset);
        printf("%s    +-- WHILE LOOP\n", offset);
        printf("%s    |   |\n", offset);
        printf("%s    |   +-- COND\n", offset);
        char newOffset[100]; 
        strcpy(newOffset, offset);
        strcat(newOffset, "    |   |");
        ast_print_expression(node->data.while_loop.condition, newOffset);

        strcat(newOffset, "    ");
        printf("%s    |   |\n", offset);
        printf("%s    |   +-- BODY\n", offset);
        ast_print_block(node->data.while_loop.body, newOffset);
        break;
    }
    case AST_BREAK:
        printf("%s    |\n", offset);
        printf("%s    +-- BREAK\n", offset);
        break;
    case AST_CONTINUE:
        printf("%s    |\n", offset);
        printf("%s    +-- CONTINUE\n", offset);
        break;
    case AST_EXPRESSION:
        ast_print_expression(node->data.expression, offset);
        break;
    case AST_VAR_DECLARATION:
        printf("%s    |\n", offset);
        printf("%s    +-- VAR DECLARATION (name: %s)\n", offset, node->data.declaration.name);
        break;
    case AST_ASSIGNMENT: {
        printf("%s    |\n", offset);
        printf("%s    +-- ASSIGNMENT (name: %s)\n", offset, node->data.assignment.name);
        char newOffset[100]; 
        strcpy(newOffset, offset);
        strcat(newOffset, "    ");
        ast_print_expression(node->data.assignment.value, newOffset);
        break;
    }
    case AST_FUNCTION:
        printf("%s    |\n", offset);
        printf("%s    +-- FUNCTION (name: %s", offset, node->data.function->name);
        if(node->data.function->parameters != NULL) {
            printf(", parameters: ");
            ast_parameter parameter = node->data.function->parameters;
            while(parameter != NULL) {
                if (parameter->value_type == AST_VALUE_INT)
                    printf("%d", parameter->value.int_value);
                else if (parameter->value_type == AST_VALUE_FLOAT)
                    printf("%f", parameter->value.double_value);
                else
                    printf("%s", parameter->value.string_value);
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
                if (parameter->value_type == AST_VALUE_INT)
                    printf("%d", parameter->value.int_value);
                else if (parameter->value_type == AST_VALUE_FLOAT)
                    printf("%f", parameter->value.double_value);
                else
                    printf("%s", parameter->value.string_value);
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
        printf("%s    +-- RETURN", offset);
        if(node->data.return_expr.output == NULL) {
            printf(" (no output)\n");
        } else {
            printf("\n");
            char newOffset[100]; 
            strcpy(newOffset, offset);
            strcat(newOffset, "    ");
            ast_print_expression(node->data.return_expr.output, newOffset);
        }
        break;
    case AST_GETTER: {
        printf("%s    |\n", offset);
        printf("%s    +-- GETTER (name: %s)\n", offset, node->data.getter.name);
        
        char newOffset[100]; 
        strcpy(newOffset, offset);
        strcat(newOffset, "        ");
        ast_print_block(node->data.getter.body, newOffset);

        break;
    }
    case AST_SETTER: {
        printf("%s    |\n", offset);
        printf("%s    +-- SETTER (name: %s)\n", offset, node->data.setter.name);

        printf("%s        |    \n", offset);
        printf("%s        +--- PARAM: %s\n", offset, node->data.setter.param);

        char newOffset[100]; 
        strcpy(newOffset, offset);
        strcat(newOffset, "        ");
        ast_print_block(node->data.setter.body, newOffset);

        break;
    }
    case AST_IFJ_FUNCTION: {
        printf("%s    |\n", offset);
        printf("%s    +-- IFJ FUNCTION (name: %s", offset, node->data.ifj_function->name);
        if(node->data.ifj_function->parameters != NULL) {
            printf(", parameters: ");
            ast_parameter parameter = node->data.ifj_function->parameters;
            while(parameter != NULL) {
                if (parameter->value_type == AST_VALUE_INT)
                    printf("%d", parameter->value.int_value);
                else if (parameter->value_type == AST_VALUE_FLOAT)
                    printf("%f", parameter->value.double_value);
                else
                    printf("%s", parameter->value.string_value);
                parameter = parameter->next;
                if(parameter != NULL) {
                    printf(", ");
                }
            }
        }
        printf(")\n");
        break;
    }
    }
}

char *get_operator_symbol(ast_expression_type type) {
    switch (type)
    {
    case AST_ADD:
        return "+";
    case AST_SUB:
        return "-";
    case AST_MUL:
        return "*";
    case AST_DIV:
        return "/";
    case AST_EQUALS:
        return "==";
    case AST_NOT_EQUAL:
        return "!=";
    case AST_LT:
        return "<";
    case AST_LE:
        return "<=";
    case AST_GT:
        return ">";
    case AST_GE:
        return ">=";
    case AST_IS:
        return "is";
    case AST_VALUE:
        return "VALUE";
    case AST_IDENTIFIER:
        return "ID";
    case AST_IFJ_FUNCTION_EXPR:
        return "IFJ Function";
    case AST_FUNCTION_CALL:
        return "FUNCTION CALL";
    default:
        return "UNKNOWN";
    }
}

/// @brief Prints an AST expression
/// @param expr pointer to the AST expression
/// @param offset offset for printing
void ast_print_expression(ast_expression expr, char *offset) {
    printf("%s    |\n", offset);
    printf("%s    +-- EXPRESSION", offset);
    
    if(expr == NULL) {
        printf(" (NULL)\n");
    } else {
        printf(" (type: %s)\n", get_operator_symbol(expr->type));
        char newOffset[100]; 
        strcpy(newOffset, offset);
        strcat(newOffset, "    |");

        if(expr->type == AST_VALUE) {
            printf("%s    |\n", newOffset);
            printf("%s    +-- VALUE: ", newOffset);
            if(expr->operands.identity.value_type == AST_VALUE_INT) {
                printf("%d\n", expr->operands.identity.value.int_value);
            } else if(expr->operands.identity.value_type == AST_VALUE_FLOAT) {
                printf("%f\n", expr->operands.identity.value.double_value);
            } else if(expr->operands.identity.value_type == AST_VALUE_STRING) {
                printf("%s\n", expr->operands.identity.value.string_value);
            } else if (expr->operands.identity.value_type == AST_VALUE_NULL) {
                printf("%s\n", "NULL");
            } else {
                printf("UNKNOWN TYPE\n");
            }

        } else if(expr->type == AST_ID) {
            printf("%s    |\n", newOffset);
            printf("%s    +-- VALUE: ", newOffset);
            printf("%s\n", expr->operands.identifier.value);
            
        } else if(expr->type == AST_IDENTIFIER) {
            printf("%s    |\n", newOffset);
            printf("%s    +-- IDENTIFIER: %s\n", newOffset, expr->operands.identifier.value);
        } else if(expr->type == AST_IFJ_FUNCTION_EXPR) {
            printf("%s    |\n", newOffset);
            printf("%s    +-- IFJ FUNCTION (name: %s", newOffset, expr->operands.ifj_function->name);
            if(expr->operands.ifj_function->parameters != NULL) {
                printf(", parameters: ");
                ast_parameter parameter = expr->operands.ifj_function->parameters;
                while(parameter != NULL) {
                    if (parameter->value_type == AST_VALUE_INT)
                        printf("%d", parameter->value.int_value);
                    else if (parameter->value_type == AST_VALUE_FLOAT)
                        printf("%f", parameter->value.double_value);
                    else
                        printf("%s", parameter->value.string_value);
                    parameter = parameter->next;
                    if(parameter != NULL) {
                        printf(", ");
                    }
                }
            }
        printf(")\n");
        } else if(expr->type == AST_FUNCTION_CALL) {
            printf("%s    |\n", newOffset);
            printf("%s    +-- FUNCTION CALL (name: %s", newOffset, expr->operands.function_call->name);
            if(expr->operands.function_call->parameters != NULL) {
                printf(", parameters: ");
                ast_parameter parameter = expr->operands.function_call->parameters;
                while(parameter) {
                    if (parameter->value_type == AST_VALUE_INT)
                        printf("%d", parameter->value.int_value);
                    else if (parameter->value_type == AST_VALUE_FLOAT)
                        printf("%f", parameter->value.double_value);
                    else
                        printf("%s", parameter->value.string_value);
                    parameter = parameter->next;
                    if(parameter != NULL) {
                        printf(", ");
                    }
                }
            }
        printf(")\n");
        } 
        else {
                ast_print_expression(expr->operands.binary_op.left, newOffset);
                ast_print_expression(expr->operands.binary_op.right, newOffset);
        }
    }
}