/**
* @authors
 *   Hana Liškařová (xliskah00)
 *
 * @file tests/test_stack.c
 * @brief Unit tests for generic LIFO stack (push, push_value, top, pop, is_empty).
 *
 * The test verifies:
 *  - LIFO order for pointers and by-value copies,
 *  - `stack_top` reflects the last pushed element,
 *  - popping from empty returns NULL,
 *  - `stack_free` releases all nodes (caller owns data).
 *
 * Build:
 *   gcc -std=c17 -Wall -Wextra -O2 stack.c tests/test_stack.c -o test_stack
 * Run:
 *   ./test_stack
 *
 * BUT FIT
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../projekt/stack.h"

/** @brief Basic test: push copies by value, then pop and validate order. */
static void test_push_value(void){
    stack s; stack_init(&s);
    assert(stack_is_empty(&s));

    int a=10, b=20, c=30;
    stack_push_value(&s, &a, sizeof a);
    stack_push_value(&s, &b, sizeof b);
    stack_push_value(&s, &c, sizeof c);

    int *t = (int*)stack_top(&s);             assert(t && *t==30);
    int *pc = (int*)stack_pop(&s);            assert(pc && *pc==30); free(pc);
    int *pb = (int*)stack_pop(&s);            assert(pb && *pb==20); free(pb);
    int *pa = (int*)stack_pop(&s);            assert(pa && *pa==10); free(pa);
    assert(stack_pop(&s)==NULL);

    stack_free(&s);
}

/** @brief Basic test: push raw pointers, then pop and validate order. */
static void test_push_pointers(void){
    stack s; stack_init(&s);

    int *x = malloc(sizeof *x), *y = malloc(sizeof *y);
    *x=42; *y=99;
    stack_push(&s, x);
    stack_push(&s, y);

    int *t = (int*)stack_top(&s);             assert(t==y && *t==99);
    int *py = (int*)stack_pop(&s);            assert(py==y && *py==99);
    int *px = (int*)stack_pop(&s);            assert(px==x && *px==42);
    assert(stack_is_empty(&s));

    free(y); free(x);
    stack_free(&s);
}

int main(void){
    test_push_value();
    test_push_pointers();
    puts("OK: test_stack passed.");
    return 0;
}
