#ifndef SYMTABLE_H
#define SYMTABLE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// ---------------------------
// Data structures
// ---------------------------

// Enum for possible data types in the symbol table
typedef enum {
    BST_NULL,
    BST_INT,
    BST_DOUBLE,
    BST_STRING,
    BST_BOOL
} Data_type;

// Struct representing the stored data
typedef struct {
    Data_type type;
    bool global;
    bool defined;
} Bst_data;

// Struct representing a node in the AVL tree
typedef struct Bst_node {
    char* key;
    Bst_data* value;
    struct Bst_node* left;
    struct Bst_node* right;
    int height;
} Bst_node;


int Max(int a, int b);
int Height(Bst_node* Node);
int Balance(Bst_node* Node);

Bst_data* create_data(Data_type type, bool global, bool defined);
Bst_node* bst_create_node(const char* key, Bst_data* value);

Bst_node* Rotate_left(Bst_node* x);
Bst_node* Rotate_right(Bst_node* y);

Bst_node* Bst_insert_node(Bst_node* Node, const char* key, Bst_data* value);

#endif // SYMTABLE_H
