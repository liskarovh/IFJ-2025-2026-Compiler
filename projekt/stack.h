/**
* @authors Matej Kurta (xkurtam00)
 *
 * @file stack.h
 *
 * @brief Stack structure and its functions definition.
 * BUT FIT
 */

#ifndef STACK_H
#define STACK_H

#include <stdlib.h>
#include <stdbool.h>

/**
 * stack item
 */
typedef struct stack_item {
    void *data;
    struct stack_item *next;
} stack_item;

/**
 * stack structure
 */
typedef struct {
    stack_item *top;
} stack;

/**
 * @brief Initializes a stack.
 */
void stack_init(stack *stack);

/**
 * @brief Pushes a value onto the stack.
 * @param data Pointer to the data to store (can be any type).
 */
void stack_push(stack *stack, void *data);


void stack_push_value(stack *stack, void *data, size_t size);

/**
 * @brief Pops the top element and returns its pointer.
 * @return Pointer to popped data, or NULL if stack empty.
 */
void *stack_pop(stack *stack);

/**
 * @brief Returns the top element without popping it.
 * @return Pointer to top element or NULL if stack empty.
 */
void *stack_top(stack *stack);

/**
 * @brief Checks if the stack is empty.
 */
bool stack_is_empty(stack *stack);

/**
 * @brief Frees all nodes in the stack (does not free the data inside).
 */
void stack_free(stack *stack);

#endif // STACK_H
