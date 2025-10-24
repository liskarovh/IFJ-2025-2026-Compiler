/**
* @authors Matej Kurta (xkurtam00)
 *
 * @file stack.c
 *
 * Stack structure and its functions
 * BUT FIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "stack.h"


///     @brief Initialize a new stack structure
///     @param capacity initial number of elements the stack holds
///     @return returns the initialized stack or NULL
Stack *create_stack(int capacity){
    Stack *stack = (Stack*)malloc(sizeof(Stack));
    stack->data = (ast_node**)malloc(sizeof(ast_node*) * capacity);
    stack->capacity = capacity;
    stack->top = -1;
    return stack;
}

///     @brief    Push a new node pointer to the top of the stack,
///     @param stack the struct pointer we are pushing into
///     @param node the node being pushed
///     @return void
void push(Stack *stack, ast_node *node){
    if (stack->top == stack->capacity - 1){
        stack->capacity *= 2;
        stack->data = (ast_node **)realloc(stack->data,sizeof(ast_node*)*stack->capacity); 
    }
    stack->data[++stack->top] = node;
}

///     @brief Take out the element from the top
///     @param stack the struct from which we take out
///     @return the node that we take out
ast_node pop(Stack *stack){
    if(stack->top == -1){
        return NULL;
    }
    return stack->data[--stack->top];
}

///     @brief Return the element from the top (without change in the stack)
///     @param stack the struct from where we take    
///     @return the node at the top of the stack
ast_node *top(Stack *stack){
    return stack->data[stack->top];
}

///     @brief Check if stack is empty
///     @param stack the struct we check
///     @return True or False
bool stack_is_empty(Stack *stack){
    return stack->top == -1;
}

///     @brief Free all allocated memory
///     @param stack the struct where we free memory from
///     @return void
void free_stack(Stack *stack){
    free(stack->data);
    free(stack);
}