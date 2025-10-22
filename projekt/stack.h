/**
* @authors Matej Kurta (xkurtam00)
 *
 * @file stack.h
 *
 * Stack structure and its functions
 * BUT FIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "ast.h"

typedef struct Stack {
    ast_node *data;
    int top;
    int capacity;
} Stack;

Stack *create_stack (int capacity);
void push(Stack *stack, ast_node *node);
ast_node pop(Stack *stack);
ast_node *top(Stack *stack);
bool stack_is_empty(Stack *stack);
void free_stack(Stack *stack);