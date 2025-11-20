/**
 * @authors Šimon Dufek (xdufeks00)

 * @file codegen.c
 * 
 * Code generator implementation using Syntactic tree
 * BUT FIT
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "codegen.h"
#include "error.h"
#include "string.h"
#include "stack.h"


void generate_unary(generator gen, char * result, ast_expression node);
void generate_binary(generator gen, char * result, ast_expression node);
void generate_expression(generator gen, char * result, ast_expression node);
void generate_function_call(generator gen, ast_node function);
void generate_assignment(generator gen, ast_node node);
void generate_declaration(generator gen, ast_node node);
void generate_if_statement(generator gen, ast_node node);
void generate_node(ast_node node, generator gen);
void init_code(generator gen, ast syntree);
void generate_code(generator gen, ast syntree);
void generate_function(generator gen, ast_node node);
void generate_block(generator gen, ast_block block);


const char *PREFIXES[] = {
    "int@", 
    "float@", 
    "string@",
    "GF@",
    "LF@",
    NULL // Ukončovací prvek pole
};

/**
 * @brief Kontroluje, zda řetězec 'str' začíná některým z prefixů v poli 'prefixes'.
 *
 * @param str Řetězec ke kontrole.
 * @param prefixes Pole zakázaných prefixů (ukončené NULL).
 * @return 1, pokud řetězec začíná zakázaným prefixem, jinak 0.
 */
int starts_with_prefix(const char *str, const char **prefixes) {
    if (str == NULL) {
        return 0;
    }
    for (int i = 0; prefixes[i] != NULL; i++) {
        size_t len = strlen(prefixes[i]);
        if (strncmp(str, prefixes[i], len) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Parsuje název proměnné, přidává prefix rámce a ověřuje zakázané prefixy.
 *
 * @param var Původní název proměnné.
 * @return Nově alokovaný řetězec ve formátu FRAME@var, nebo NULL při chybě/neplatném vstupu.
 */
char* var_frame_parse(char *var) {
    if (var == NULL) {
        fprintf(stderr, "Chyba: Vstupní proměnná je NULL.\n");
        return NULL;
    }

    const char *prefix;

    if (starts_with_prefix(var, PREFIXES)) { // kontrola, zda nen9 prefix
        prefix = "";
    }
    else if (var[0] == '_' && var[1] == '_') {
        prefix = "GF@";
    } else {
        prefix = "LF@";
    }

    size_t prefix_len = strlen(prefix);
    size_t var_len = strlen(var);
    
    char *varout = (char*)malloc(prefix_len + var_len + 1);
    
    if (varout == NULL) {
        fprintf(stderr, "Chyba alokace paměti.\n");
        return NULL;
    }

    strcpy(varout, prefix);
    strcat(varout, var);
    
    return varout;
}

char* escape_string_literal(const char* original_str) {
    if (original_str == NULL) { // prázdný řetězec
        char* prefix_only = malloc(7 * sizeof(char));
        if (prefix_only) prefix_only = "string@";
        return prefix_only;
    }

    size_t len = strlen(original_str);
    size_t max_size = 7 + len * 4 + 1;

    char* result = (char*)malloc(max_size);
    if (result == NULL) {
        return NULL;
    }

    strcpy(result, "string@");
    char* current = result + 7; // zapisování za prefixem

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)original_str[i];

        // escapování \xyz pro 0-32, 35 (#) a 92 (\)
        if (ch <= 32 || ch == 35 || ch == 92) {
            current += sprintf(current, "\\%03d", ch); 
        } 
        else {
            *current++ = ch;
        }
    }
    
    *current = '\0';

    return result;
}

char *ast_value_to_string(ast_expression node) {
    char *result = NULL;
    
    switch (node->operands.identity.value_type) {
        case AST_VALUE_INT: {
            result = malloc((12 + 4) * sizeof(char)); //int@num
            if (result) {
                sprintf(result, "int@%d", node->operands.identity.value.int_value);
            }
            break;
        }
        
        case AST_VALUE_FLOAT: {
            result = malloc((64 + 6) * sizeof(char));
            if (result) {
                sprintf(result, "float@%a", node->operands.identity.value.double_value);
            }
            break;
        }
        
        case AST_VALUE_STRING: {
            result = escape_string_literal(node->operands.identity.value.string_value);
            break;
        }
        
        default:
            return NULL;
    }
    
    return result;
}

enum arity get_op_arity(ast_expression_type type){
    switch(type){ //get number of variables needed
        case AST_ADD:
        case AST_SUB:
        case AST_MUL:
        case AST_DIV:
        case AST_EQUALS:
        case AST_NOT_EQUAL:
        case AST_TERNARY:
        case AST_LT:
        case AST_LE:
        case AST_GT:
        case AST_GE:
        case AST_AND:
        case AST_OR:
        case AST_CONCAT: //concat
            return ARITY_BINARY;
        case AST_NOT:
            return ARITY_UNARY;
        default:
            return ARITY_UNDEFINED;
    }
}

void create_gen(generator gen){
    gen->output = string_create(2048);
    gen->counter = 0;
}

void createframe(generator gen){
    string_append_literal(gen->output, "CREATEFRAME\n");
}
void pushframe(generator gen){
    string_append_literal(gen->output, "PUSHFRAME\n");
}
void popframe(generator gen){
    string_append_literal(gen->output, "POPFRAME\n");
}
void return_code(generator gen){
    string_append_literal(gen->output, "RETURN\n");
}
void fn_call(generator gen, char * fn_name){
    string_append_literal(gen->output, "CALL ");
    string_append_literal(gen->output, fn_name);
    string_append_literal(gen->output, "\n");
}
void label(generator gen, char * label){
    string_append_literal(gen->output, "LABEL ");
    string_append_literal(gen->output, label);
    string_append_literal(gen->output, "\n");
}
void jump(generator gen, char * label){
    string_append_literal(gen->output, "JUMP ");
    string_append_literal(gen->output, label);
    string_append_literal(gen->output, "\n");
}
void add_jumpifeq(generator gen, char * label, char * symb1, char * symb2){
    char *nlabel = var_frame_parse(label);
    char *nsymb1 = var_frame_parse(symb1);
    char *nsymb2 = var_frame_parse(symb2);
    string_append_literal(gen->output, "JUMPIFEQ ");
    string_append_literal(gen->output, nlabel);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, nsymb1);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, nsymb2);
    string_append_literal(gen->output, "\n");
    free(nlabel);
    free(nsymb1);
    free(nsymb2);
}
void add_jumpifneq(generator gen, char * label, char * symb1, char * symb2){
    char *nlabel = var_frame_parse(label);
    char *nsymb1 = var_frame_parse(symb1);
    char *nsymb2 = var_frame_parse(symb2);
    string_append_literal(gen->output, "JUMPIFNEQ ");
    string_append_literal(gen->output, nlabel);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, nsymb1);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, nsymb2);
    string_append_literal(gen->output, "\n");
    free(nlabel);
    free(nsymb1);
    free(nsymb2);
}
void push(generator gen, char * name){
    char *nname = var_frame_parse(name);
    string_append_literal(gen->output, "PUSHS ");
    string_append_literal(gen->output, nname);
    string_append_literal(gen->output, "\n");
    free(nname);
}
void pop(generator gen, char * name){
    char *nname = var_frame_parse(name);
    string_append_literal(gen->output, "POPS ");
    string_append_literal(gen->output, nname);
    string_append_literal(gen->output, "\n");
    free(nname);
}
void define_variable(generator gen, char * name){
    char *nname = var_frame_parse(name);
    string_append_literal(gen->output, "DEFVAR ");
    string_append_literal(gen->output, nname);
    string_append_literal(gen->output, "\n");
    free(nname);
}
void move_var(generator gen, char * var1, char * var2){
    char *nvar1 = var_frame_parse(var1);
    char *nvar2 = var_frame_parse(var2);
    string_append_literal(gen->output, "MOVE ");
    string_append_literal(gen->output, nvar1);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, nvar2);
    string_append_literal(gen->output, "\n");
    free(nvar1);
    free(nvar2);
}
void binary_operation(generator gen, char * op, char * result, char * left, char * right){
    char *nresult = var_frame_parse(result);
    char *nleft = var_frame_parse(left);
    char *nright = var_frame_parse(right);
    string_append_literal(gen->output, op);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, nresult);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, nleft);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, nright);
    string_append_literal(gen->output, "\n");
    free(nresult);
    free(nleft);
    free(nright);
}
void op_add(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "ADD", result, left, right);
}
void op_sub(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "SUB", result, left, right);
}
void op_mul(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "MUL", result, left, right);
}
void op_div(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "DIV", result, left, right);
}
void op_idiv(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "IDIV", result, left, right);
}
void op_lt(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "LT", result, left, right);
}
void op_gt(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "GT", result, left, right);
}
void op_eq(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "EQ", result, left, right);
}
void op_and(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "AND", result, left, right);
}
void op_or(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "OR", result, left, right);
}
void op_concat(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "CONCAT", result, left, right);
}
void op_not(generator gen, char * result, char * op){
    char *nresult = var_frame_parse(result);
    string_append_literal(gen->output, "NOT ");
    string_append_literal(gen->output, nresult);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, op);
    string_append_literal(gen->output, "\n");
    free(nresult);
}
void ifj_read(generator gen, char * name, char * type){
    char *nname = var_frame_parse(name);
    string_append_literal(gen->output, "READ ");
    string_append_literal(gen->output, nname);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, type);
    string_append_literal(gen->output, "\n");
    free(nname);
}
void ifj_write(generator gen, char * name){
    char *nname = var_frame_parse(name);
    string_append_literal(gen->output, "WRITE ");
    string_append_literal(gen->output, nname);
    string_append_literal(gen->output, "\n");
    free(nname);
}
void ifj_strlen(generator gen, char * output, char * input){
    char *noutput = var_frame_parse(output);
    char *ninput = var_frame_parse(input);
    string_append_literal(gen->output, "STRLEN ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, "\n");
    free(ninput);
    free(noutput);
}
void ifj_getchar(generator gen, char * output, char * input, char * position){
    char *ninput = var_frame_parse(input);
    char *noutput = var_frame_parse(output);
    string_append_literal(gen->output, "GATCHAR ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, position);
    string_append_literal(gen->output, "\n");
    free(ninput);
    free(noutput);
}
void ifj_setchar(generator gen, char * output, char * position, char * input){
    char *noutput = var_frame_parse(output);
    char *ninput = var_frame_parse(input);
    string_append_literal(gen->output, "SETCHAR ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, position);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, "\n");
    free(ninput);
    free(noutput);
}
void ifj_type(generator gen, char * output, char * input){
    char *ninput = var_frame_parse(input);
    char *noutput = var_frame_parse(output);
    string_append_literal(gen->output, "TYPE ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, "\n");
    free(ninput);
    free(noutput);
}
void ifj_float2int(generator gen, char * output, char * input){
    char *ninput = var_frame_parse(input);
    char *noutput = var_frame_parse(output);
    string_append_literal(gen->output, "FLOAT2INT ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, "\n");
    free(ninput);
    free(noutput);
}
void ifj_int2char(generator gen, char * output, char * input){
    char *ninput = var_frame_parse(input);
    char *noutput = var_frame_parse(output);
    string_append_literal(gen->output, "INT2CHAR ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, "\n");
    free(ninput);
    free(noutput);
}
void ifj_float2char(generator gen, char * output, char * input){
    char *ninput = var_frame_parse(input);
    char *noutput = var_frame_parse(output);
    string_append_literal(gen->output, "FLOAT2CHAR ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, "\n");
    free(ninput);
    free(noutput);
}
void ifj_stri2int(generator gen, char * output, char * input){
    char *ninput = var_frame_parse(input);
    char *noutput = var_frame_parse(output);
    string_append_literal(gen->output, "STRING2INT ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, "\n");
    free(ninput);
    free(noutput);
}
void ifj_int2float(generator gen, char * output, char * input){
    char *ninput = var_frame_parse(input);
    char *noutput = var_frame_parse(output);
    string_append_literal(gen->output, "INT2FLOAT ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, "\n");
    free(ninput);
    free(noutput);
}
void exit_code(generator gen, char * code){
    string_append_literal(gen->output, "EXIT ");
    string_append_literal(gen->output, code);
    string_append_literal(gen->output, "\n");
}



void generate_unary(generator gen, char * result, ast_expression node){
    ast_expression expr;
    ast_expression_type operation;

    expr = node->operands.unary_op.expression;
    operation = node->type;

    generate_expression(gen, "GF@tmp_op", expr);

    switch (operation) { //switch of all operators
        case AST_NOT:
            op_not(gen, result, "GF@tmp_op");
            break;
        default:
            break;
    }
}

void generate_binary(generator gen, char * result, ast_expression node){
    ast_expression left;
    ast_expression right;
    ast_expression_type operation;

    left = node->operands.binary_op.left;
    right = node->operands.binary_op.right;
    operation = node->type;
    

    generate_expression(gen, "GF@tmp_l", left); //left expression
    generate_expression(gen, "GF@tmp_r", right); //right expression

    switch (operation) { //switch of all operators
        case AST_ADD:
            op_add(gen, result, "GF@tmp_l", "GF@tmp_r");
            break;
        case AST_SUB:
            op_sub(gen, result, "GF@tmp_l", "GF@tmp_r");
            break;
        case AST_MUL:
            op_mul(gen, result, "GF@tmp_l", "GF@tmp_r");
            break;
        case AST_DIV:
            op_div(gen, result, "GF@tmp_l", "GF@tmp_r");
            break;
        case AST_LT:
            op_lt(gen, result, "GF@tmp_l", "GF@tmp_r");
            break;
        case AST_GT:
            op_gt(gen, result, "GF@tmp_l", "GF@tmp_r");
            break;
        case AST_EQUALS:
            op_eq(gen, result, "GF@tmp_l", "GF@tmp_r");
            break;
        case AST_AND:
            op_and(gen, result, "GF@tmp_l", "GF@tmp_r");
            break;
        case AST_OR:
            op_or(gen, result, "GF@tmp_l", "GF@tmp_r");
            break;
        case AST_LE:
            op_lt(gen, "GF@tmp1", "GF@tmp_l", "GF@tmp_r");
            op_eq(gen, "GF@tmp2", "GF@tmp_l", "GF@tmp_r");
            op_or(gen, result, "GF@tmp1", "GF@tmp2");
            break;
        case AST_GE:
            op_gt(gen, "GF@tmp1", "GF@tmp_l", "GF@tmp_r");
            op_eq(gen, "GF@tmp2", "GF@tmp_l", "GF@tmp_r");
            op_or(gen, result, "GF@tmp1", "GF@tmp2");
            break;
        case AST_CONCAT:
            op_concat(gen, result, "GF@tmp1", "GF@tmp2");
            break;
        default:
            break;
    }
}

void generate_expression(generator gen, char * result, ast_expression node){
    
    if (node->type == AST_VALUE)
        move_var(gen, ast_value_to_string(node), result);
    else if(node->type == AST_ID){ //not expression
        move_var(gen, result, node->operands.identifier.value); //move LF@var value
    }
    //else if(node->type == AST_IFJ_FUNCTION_EXPR)
    //    generate_ifjfunction(gen, result, node->operands.ast_ifj_function->parameters, node->operands.ast_ifj_function->name);
    else {
        switch(get_op_arity(node->type)){
                case ARITY_UNARY:
                    generate_unary(gen, result, node); //only one variable
                    break;
                case ARITY_BINARY:
                    generate_binary(gen, result, node); //two variables
                    break;
                default:
                    break;
        }
    }
}

void generate_ifjfunction(generator gen, char* name, ast_parameter params, char* output){ 
    if(strcmp(name, "str") == 0)
        ifj_float2char(gen, output, params->name);
    //else if(strcmp(name, "chr"))
    else if(strcmp(name, "floor") == 0)
        ifj_float2int(gen, output, params->name);
    else if(strcmp(name, "length") == 0)
        ifj_strlen(gen, output, params->name);
    else if(strcmp(name, "ord") == 0){
        ifj_getchar(gen, "GF@tmp_ifj", params->name, params->next->name);
        ifj_stri2int(gen, output, "GF@tmp_ifj");
    }
    else if(strcmp(name, "read_num") == 0)
        ifj_read(gen, output, "float");
    else if(strcmp(name, "read_str") == 0)
        ifj_read(gen, output, "string");
    else if(strcmp(name, "strcmp") == 0)
        op_eq(gen, output, params->name, params->next->name);
    //else if(strcmp(name, "substring"))
    else if(strcmp(name, "write") == 0)
        ifj_write(gen, params->name);
}

void generate_function_call(generator gen, ast_node node){
    stack stack;
    stack_init(&stack);

    ast_parameter param = node->data.function_call->parameters;
    while(param != NULL){
        stack_push(&stack, param->name);
        param = param->next;
    }
    char *param_name;
    while(!stack_is_empty(&stack)){ //reverse push
        param_name = stack_pop(&stack);
        pop(gen, param_name);
    }
    fn_call(gen, node->data.function_call->name); //call function*/

    stack_free(&stack);
}

void generate_function_return(generator gen, ast_node node){
    generate_expression(gen, "GF@fn_ret", node->data.return_expr.output);
    popframe(gen);
    return_code(gen);
}

void generate_assignment(generator gen, ast_node node){
    if(node->data.assignment.value != NULL){
        ast_expression expr = node->data.assignment.value;
        generate_expression(gen, node->data.assignment.name, expr); //expression
    }
}

void generate_declaration(generator gen, ast_node node){
    define_variable(gen, node->data.declaration.name);
    //generate_assignment(gen, node->data.declaration.assignment); //when implemented
}

void generate_if_statement(generator gen, ast_node node){
    char tmp[20];
    string end_label = string_create(20);
    string else_lable = string_create(20);
    ast_block body;

    string_append_literal(end_label, "conditionEnd");
    snprintf(tmp, 20, "%u", gen->counter++);
    string_append_literal(end_label, tmp); //create one of label for end of if

    if(node->data.condition.else_branch == NULL) //no else
        string_append_literal(else_lable, end_label->data);
    else{
        string_clear(else_lable);
        string_append_literal(else_lable, "ifEnd");
        snprintf(tmp, 20, "%u", gen->counter);
        string_append_literal(else_lable, tmp); //create one of label for else
    }
    
    //create condition
    string_append_literal(gen->output, "\n# IF CONDITION\n");
    generate_expression(gen, "GF@tmp_if", node->data.condition.condition); //creates the condition
    add_jumpifeq(gen, else_lable->data, "GF@tmp_if", "bool@false");
    string_append_literal(gen->output, "# IF CONDITION END\n\n");

    
    if(node->data.condition.if_branch != NULL){
        string_append_literal(gen->output, "# IF BRANCH\n");
        body = node->data.condition.if_branch;
        generate_block(gen, body);
        jump(gen, end_label->data);
    }
    if(node->data.condition.else_branch != NULL){ //else exists
        label(gen, else_lable->data);
        string_append_literal(gen->output, "\n# ELSE BRANCH\n");
        body = node->data.condition.else_branch;
        generate_block(gen, body);
    }

    label(gen, end_label->data);
    string_append_literal(gen->output, "\n");
    string_destroy(end_label);
    string_destroy(else_lable);
}

void generate_while(generator gen, ast_node node){
    char tmp[20];
    string while_start = string_create(20);
    string while_end = string_create(20);
    ast_block body;

    string_append_literal(while_start, "whileStart");
    string_append_literal(while_end, "whileEnd");
    snprintf(tmp, 20, "%u", gen->counter++);
    string_append_literal(while_start, tmp); //create one of label for loop start
    string_append_literal(while_end, tmp); //create one of label for loop end

    
    string_append_literal(gen->output, "\n# WHILE LOOP START\n");
    
    generate_expression(gen, "GF@tmp_while", node->data.while_loop.condition); //creates the condition
    add_jumpifeq(gen, while_end->data, "GF@tmp_while", "bool@false");

    string_append_literal(gen->output, "\n");
    label(gen, while_start->data); //start lable


    body = node->data.while_loop.body;
    generate_block(gen, body);


    string_append_literal(gen->output, "\n");
    generate_expression(gen, "GF@tmp_while", node->data.while_loop.condition); //creates the condition
    add_jumpifneq(gen, while_start->data, "GF@tmp_while", "bool@false");

    label(gen, while_end->data); //end lable
    string_append_literal(gen->output, "# WHILE LOOP END\n\n");

    string_destroy(while_start);
    string_destroy(while_end);
}

void generate_node(ast_node node, generator gen){
    switch(node->type){
        case AST_CONDITION: // if statement
            generate_if_statement(gen, node);
            break;

        case AST_VAR_DECLARATION: //var declaration
            generate_declaration(gen, node);
            break;

        case AST_ASSIGNMENT: //var assignment
            generate_assignment(gen, node);
            break;

        case AST_IFJ_FUNCTION: //read / write
            generate_ifjfunction(gen, node->data.ifj_function->name, node->data.ifj_function->parameters, NULL);
            break;

        case AST_WHILE_LOOP:
            generate_while(gen, node);
            break;

        case AST_CALL_FUNCTION:
            generate_function_call(gen, node);
            break;

        case AST_RETURN:
            generate_function_return(gen, node);
            break;
        case AST_BLOCK:
            generate_block(gen, node->data.block);
            break;
        case AST_FUNCTION:
            generate_function(gen, node);
            break;
        case AST_GETTER:
            generate_function(gen, node);
            break;
        case AST_SETTER:
            generate_function(gen, node);
            break;
        default:
            break;
    }
}

void generate_block(generator gen, ast_block block){
    //pushframe(gen);
    ast_node node = block->first;
    while (node) {
        generate_node(node, gen);
        node = node->next;
    }
    //popframe(gen);
}

void init_code(generator gen, ast ast){ //.IFJcode25 on the first line and initiale all temp variables
    if(ast != NULL && ast->class_list != NULL){
        create_gen(gen);
        string_append_literal(gen->output, ".IFJcode25 \n\n");
        
        define_variable(gen, "GF@tmp_if");
        define_variable(gen, "GF@tmp_while");
        define_variable(gen, "GF@tmp_l");
        define_variable(gen, "GF@tmp_r");
        define_variable(gen, "GF@tmp_op");
        define_variable(gen, "GF@tmp_ifj");
        define_variable(gen, "GF@tmp1");
        define_variable(gen, "GF@tmp2");
        define_variable(gen, "GF@fn_ret");
    }
}

void generate_code(generator gen, ast ast){ //go threw all nodes in AST
    if(ast != NULL && ast->class_list != NULL){
        ast_class program = ast->class_list; //opens 'program' class   
        ast_block program_body = program->current; // class block

        generate_block(gen, program_body);

        string_append_literal(gen->output, "\n#END OF FILE\n");
    }
}

void generate_function(generator gen, ast_node node){
    char *name;
    ast_block fun_body;
    ast_parameter param = NULL;
    if(node->type == AST_FUNCTION) {
        name = node->data.function->name;
        param = node->data.function->parameters;
        fun_body = node->data.function->code;
    }
    else if(node->type == AST_GETTER) {
        name = node->data.getter.name;
        fun_body = node->data.getter.body;
    }
    else if(node->type == AST_SETTER) {
        name = node->data.setter.name;
        fun_body = node->data.setter.body;
    }
    else
        return;
    string_append_literal(gen->output, "\n# START OF FUNCTION ---");
    string_append_literal(gen->output, name);
    string_append_literal(gen->output, "---\n");
    label(gen, name);
    createframe(gen); //create function frame
    pushframe(gen); //use new frame
    while(param != NULL){
        define_variable(gen, param->name);
        pop(gen, param->name);
        param = param->next;
    }
    if(node->type == AST_SETTER) {
        define_variable(gen, node->data.setter.param);
        pop(gen, node->data.setter.param);
    }

    generate_block(gen, fun_body); //generate body

    popframe(gen); //pop frame from stack and use previous
    string_append_literal(gen->output, "# END OF FUNCTION ---");
    string_append_literal(gen->output, name);
    string_append_literal(gen->output, "---\n");
    if(strcmp(name, "main") == 0)
        exit_code(gen, "int@0");
}