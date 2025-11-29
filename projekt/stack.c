/**
 * @file stack.c
 * @authors Matej Kurta (xkurtam00)
 *
 * @brief Implementation of a stack structure and its operations.
 *
 * @note Project: IFJ / BUT FIT
 */

#include "stack.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

/**
 * @brief Initializes a stack structure.
 *
 * Sets the top pointer to NULL, preparing the stack for use.
 *
 * @param stack Pointer to the stack structure to initialize.
 */
void stack_init(stack *stack) {
    stack->top = NULL;
}

/**
 * @brief Pushes a pointer to data onto the stack.
 *
 *
 * @param stack Pointer to the stack structure.
 * @param data Pointer to the data to push onto the stack.
 */
void stack_push(stack *stack, void *data) {
    stack_item *new = malloc(sizeof(stack_item));
    if (!new) return;
    new->data = data;
    new->next = stack->top;
    stack->top = new;
}

/**
 * @brief Pushes a copy of a value onto the stack.
 *
 * Useful for pushing enums, integers, or structs by value.
 *
 * @param stack Pointer to the stack structure.
 * @param data Pointer to the value to copy and push.
 * @param size Size of the value to copy, in bytes.
 */
void stack_push_value(stack *stack, void *data, size_t size) {
    void *copy = malloc(size);
    if (!copy) return;
    memcpy(copy, data, size);
    stack_push(stack, copy);
}

/**
 * @brief Pops the top element from the stack.
 *
 *
 * @param stack Pointer to the stack structure.
 * @return Pointer to the popped data, or NULL if the stack is empty.
 */
void *stack_pop(stack *stack) {
    if (!stack->top) return NULL;

    stack_item *temp = stack->top;
    void *data = temp->data;
    stack->top = temp->next;
    free(temp);
    return data;
}

/**
 * @brief Returns the data at the top of the stack without removing it.
 *
 * @param stack Pointer to the stack structure.
 * @return Pointer to the top element's data, or NULL if the stack is empty.
 */
void *stack_top(stack *stack) {
    if (!stack->top) return NULL;
    return stack->top->data;
}

/**
 * @brief Checks whether the stack is empty.
 *
 * @param stack Pointer to the stack structure.
 * @return `true` if the stack is empty, otherwise `false`.
 */
bool stack_is_empty(stack *stack) {
    return stack->top == NULL;
}

/**
 * @brief Frees all nodes in the stack.
 *
 *
 * @param stack Pointer to the stack structure.
 */
void stack_free(stack *stack) {
    while (stack->top) {
        stack_item *temp = stack->top;
        stack->top = temp->next;
        free(temp);
    }
}
