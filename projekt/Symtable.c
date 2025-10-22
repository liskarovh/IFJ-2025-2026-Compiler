#include "Symtable.h"


int Max(int a, int b)
{
    return (a > b)? a : b;
}

int Height(Bst_node* Node){
    if (Node == NULL) return 0;
    return Max(Height(Node->left),Height(Node->right))+1; 
}



Bst_data* create_data(Data_type type, bool global, bool defined) {
    Bst_data* data = malloc(sizeof(Bst_data));
    if (!data) {
        fprintf(stderr, "Memory allocation failed for Bst_data\n");
        exit(EXIT_FAILURE);
    }
    data->type = type;
    data->global = global;
    data->defined = defined;
    return data;
}

Bst_node* bst_create_node(const char* key, Bst_data* value) {
    Bst_node* node = malloc(sizeof(Bst_node));
    if (!node) {
        fprintf(stderr, "Memory allocation failed for Bst_node\n");
        exit(EXIT_FAILURE);
    }

    node->key = strdup(key);  
    node->value = value;      
    node->left = NULL;
    node->right = NULL;
    node->height = 1;         

    return node;
}

Bst_node* Rotate_right(Bst_node* y)
{
    Bst_node* x = y->left;
    Bst_node* T2 = x->right;

    x->right = y;
    y->left = T2;

    y->height = Max(Height(y->left),
                    Height(y->right)) + 1;
    x->height = Max(Height(x->left),
                    Height(x->right)) + 1;

    return x;
}


Bst_node* Rotate_left(Bst_node* x)
{
    Bst_node* y = x->right;
    Bst_node* T2 = y->left;

    
    y->left = x;
    x->right = T2;

    
    x->height = Max(Height(x->left),   
                    Height(x->right)) + 1;
    y->height = Max(Height(y->left),
                    Height(y->right)) + 1;

    
    return y;
}

int Balance(Bst_node* Node){
    if (Node == NULL)return 0;
    return Height(Node->right) - Height(Node->left);
}

Bst_node* Bst_insert_node(Bst_node* Node, const char* key, Bst_data* value) {
    
    if (Node == NULL)
        return bst_create_node(key, value);

    
    int compare = strcmp(key, Node->key);

    if (compare < 0)
        Node->left = Bst_insert_node(Node->left, key, value);
    else if (compare > 0)
        Node->right = Bst_insert_node(Node->right, key, value);
    else {
       
        free(Node->value);
        Node->value = value;
        return Node;
    }

    
    Node->height = 1 + Max(Height(Node->left), Height(Node->right));

    
    int bal = Balance(Node);

    

    // RR
    if (bal > 1 && strcmp(key, Node->right->key) > 0)
        return Rotate_left(Node);

    // RL
    if (bal > 1 && strcmp(key, Node->right->key) < 0) {
        Node->right = Rotate_right(Node->right);
        return Rotate_left(Node);
    }

    // LL
    if (bal < -1 && strcmp(key, Node->left->key) < 0)
        return Rotate_right(Node);

    // LR
    if (bal < -1 && strcmp(key, Node->left->key) > 0) {
        Node->left = Rotate_left(Node->left);
        return Rotate_right(Node);
    }

    return Node;
}
