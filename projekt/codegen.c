/**
 * @authors Šimon Dufek (xdufeks00)
 * @file codegen.c
 * * Code generator implementation using Stack-based evaluation
 * BUT FIT
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "codegen.h"
#include "error.h"
#include "string.h"
#include "semantic.h"

// Forward declarations
void generate_expression_stack(generator gen, ast_expression node); // Hlavní funkce pro stack
void generate_expression(generator gen, char * result, ast_expression node); // Wrapper
void generate_function_call(generator gen, ast_node node, ast_expression expr_node);
void generate_assignment(generator gen, ast_node node);
void generate_declaration(generator gen, ast_node node);
void generate_if_statement(generator gen, ast_node node);
void generate_node(ast_node node, generator gen, bool declare);
void init_code(generator gen, ast syntree);
void generate_code(generator gen, ast syntree);
void generate_function(generator gen, ast_node node);
void generate_main(generator gen, ast_node node);
void generate_block(generator gen, ast_block block, bool declare);
void generate_ifjfunction(generator gen, char* name, ast_parameter params, char* output);

void generate_add_conversion(generator gen, char *result, char *left, char *right);
void generate_mul_conversion(generator gen, char *result, char *left, char *right);
void generate_div_conversion(generator gen, char *left, char *right);
void generate_repetition(generator gen, char *result, char *left, char *right);
void generate_float_conversion(generator gen, char *var_name, char *type_name);
void generate_type_check(generator gen, char *symb1, char *symb2, char *error_label);

const char *PREFIXES[] = {
    "int@", 
    "float@", 
    "string@",
    "GF@",
    "LF@",
    "nil@",
    "bool@",
    NULL 
};

int starts_with_prefix(const char *str, const char **prefixes) {
    if (str == NULL) return 0;
    for (int i = 0; prefixes[i] != NULL; i++) {
        size_t len = strlen(prefixes[i]);
        if (strncmp(str, prefixes[i], len) == 0) return 1;
    }
    return 0;
}

// Returns GF if var starts with __, else returns LF
char* var_frame_parse(char *var) {
    if (var == NULL) {
        fprintf(stderr, "Chyba: Vstupní proměnná je NULL.\n");
        return NULL;
    }
    const char *prefix;
    if (starts_with_prefix(var, PREFIXES)) {
        prefix = "";
    } else if (var[0] == '_' && var[1] == '_') {
        prefix = "GF@";
    } else {
        prefix = "LF@";
    }
    size_t prefix_len = strlen(prefix);
    size_t var_len = strlen(var);
    char *varout = (char*)malloc(prefix_len + var_len + 1);
    if (varout == NULL) return NULL;
    strcpy(varout, prefix);
    strcat(varout, var);
    return varout;
}


// Generating globals at the start of file
static void sem_def_globals(generator gen) {
    char **globals = NULL;
    size_t count   = 0;
    int rc = semantic_get_globals(&globals, &count);
    if (rc != SUCCESS) return;
    string_append_literal(gen->output, "\n# GLOABLS DECLARATION\n");
    for (size_t i = 0; i < count; ++i) {
        string_append_literal(gen->output, "DEFVAR GF@");
        string_append_literal(gen->output, globals[i]);
        string_append_literal(gen->output, "\nMOVE GF@");
        string_append_literal(gen->output, globals[i]);
        string_append_literal(gen->output, " nil@nil\n");
        free(globals[i]);
    }
    string_append_literal(gen->output, "\n");
    free(globals);
}

// Converting string for correct output
char* escape_string_literal(const char* original_str) {
    if (original_str == NULL) {
        char* prefix_only = malloc(7 * sizeof(char));
        if (prefix_only) prefix_only = "string@";
        return prefix_only;
    }
    size_t len = strlen(original_str);
    size_t max_size = 7 + len * 4 + 1;
    char* result = (char*)malloc(max_size);
    if (result == NULL) return NULL;
    strcpy(result, "string@");
    char* current = result + 7;
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)original_str[i];
        if (ch <= 32 || ch == 35 || ch == 92) {
            current += sprintf(current, "\\%03d", ch); 
        } else {
            *current++ = ch;
        }
    }
    *current = '\0';
    return result;
}

// Value to string conversion from paramenters and expressions
char *ast_value_to_string(ast_expression expr_node, ast_parameter param_node) {
    char *result = NULL;
    ast_value_type type;
    int int_val;
    float float_val;
    char *char_val;
    if(expr_node) { // Expressions
        if(expr_node->type == AST_IDENTIFIER) {
            char_val = expr_node->operands.identifier.cg_name;
            type = AST_VALUE_IDENTIFIER;
        } else {
            type = expr_node->operands.identity.value_type;
            switch (type) {
            case AST_VALUE_INT: int_val = expr_node->operands.identity.value.int_value; break;
            case AST_VALUE_FLOAT: float_val = expr_node->operands.identity.value.double_value; break;
            case AST_VALUE_NULL: break;
            case AST_VALUE_IDENTIFIER: char_val = expr_node->operands.identity.value.string_value; break;
            default:
                char_val = expr_node->operands.identity.value.string_value;
                if (strcmp(char_val, "Num") == 0) char_val = "string@int";
                else if (strcmp(char_val, "String") == 0) char_val = "string@string";
                else if (strcmp(char_val, "Null") == 0) char_val = "string@nil";
                break;
            }
        }
    } else { // Parameters
        type = param_node->value_type;
        switch (type) {
        case AST_VALUE_INT: int_val = param_node->value.int_value; break;
        case AST_VALUE_FLOAT: float_val = param_node->value.double_value; break;
        case AST_VALUE_NULL: break;
        case AST_VALUE_IDENTIFIER: char_val = param_node->cg_name; break;
        default: char_val = param_node->value.string_value; break;
        }
    }

    switch (type) { // Creating string based on type
        case AST_VALUE_INT:
            result = malloc((16) * sizeof(char));
            if (result) sprintf(result, "int@%d", int_val);
            break;
        case AST_VALUE_FLOAT:
            result = malloc((70) * sizeof(char));
            if (result) sprintf(result, "float@%a", float_val);
            break;
        case AST_VALUE_IDENTIFIER:
            result = var_frame_parse(char_val);
            break;
        case AST_VALUE_STRING:
            result = escape_string_literal(char_val);
            break;
        case AST_VALUE_NULL:
            result = malloc((8) * sizeof(char));
            result = "nil@nil";
            break;
        default: return NULL;
    }
    return result;
}

enum arity get_op_arity(ast_expression_type type){
    switch(type){
        case AST_ADD: case AST_SUB: case AST_MUL: case AST_DIV:
        case AST_EQUALS: case AST_NOT_EQUAL: case AST_TERNARY:
        case AST_LT: case AST_LE: case AST_GT: case AST_GE:
        case AST_AND: case AST_OR: case AST_IS: case AST_CONCAT:
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

// --- Instructions ---
void createframe(generator gen){ string_append_literal(gen->output, "CREATEFRAME\n"); }
void pushframe(generator gen){ string_append_literal(gen->output, "PUSHFRAME\n"); }
void popframe(generator gen){ string_append_literal(gen->output, "POPFRAME\n"); }
void return_code(generator gen){ string_append_literal(gen->output, "RETURN\n"); }
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
    char *nsymb1 = var_frame_parse(symb1);
    char *nsymb2 = var_frame_parse(symb2);
    string_append_literal(gen->output, "JUMPIFEQ ");
    string_append_literal(gen->output, label);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, nsymb1);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, nsymb2);
    string_append_literal(gen->output, "\n");
    free(nsymb1); free(nsymb2);
}
void add_jumpifneq(generator gen, char * label, char * symb1, char * symb2){
    char *nsymb1 = var_frame_parse(symb1);
    char *nsymb2 = var_frame_parse(symb2);
    string_append_literal(gen->output, "JUMPIFNEQ ");
    string_append_literal(gen->output, label);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, nsymb1);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, nsymb2);
    string_append_literal(gen->output, "\n");
    free(nsymb1); free(nsymb2);
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
    free(nvar1); free(nvar2);
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
    free(nresult); free(nleft); free(nright);
}
void op_add(generator gen, char * result, char * left, char * right){ binary_operation(gen, "ADD", result, left, right); }
void op_sub(generator gen, char * result, char * left, char * right){ binary_operation(gen, "SUB", result, left, right); }
void op_mul(generator gen, char * result, char * left, char * right){ binary_operation(gen, "MUL", result, left, right); }
void op_div(generator gen, char * result, char * left, char * right){ binary_operation(gen, "DIV", result, left, right); }
void op_idiv(generator gen, char * result, char * left, char * right){ binary_operation(gen, "IDIV", result, left, right); }
void op_lt(generator gen, char * result, char * left, char * right){ binary_operation(gen, "LT", result, left, right); }
void op_gt(generator gen, char * result, char * left, char * right){ binary_operation(gen, "GT", result, left, right); }
void op_eq(generator gen, char * result, char * left, char * right){ binary_operation(gen, "EQ", result, left, right); }
void op_and(generator gen, char * result, char * left, char * right){ binary_operation(gen, "AND", result, left, right); }
void op_or(generator gen, char * result, char * left, char * right){ binary_operation(gen, "OR", result, left, right); }
void op_concat(generator gen, char * result, char * left, char * right){ binary_operation(gen, "CONCAT", result, left, right); }
void op_not(generator gen, char * result, char * op){
    char *nresult = var_frame_parse(result);
    char *nop = var_frame_parse(op);
    string_append_literal(gen->output, "NOT ");
    string_append_literal(gen->output, nresult);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, nop);
    string_append_literal(gen->output, "\n");
    free(nresult); free(nop);
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
    move_var(gen, "GF@tmp1", "nil@nil");
}
void ifj_strlen(generator gen, char * output, char * input){
    char *noutput = var_frame_parse(output);
    char *ninput = var_frame_parse(input);
    string_append_literal(gen->output, "STRLEN ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, "\n");
    free(ninput); free(noutput);
}
void ifj_getchar(generator gen, char * output, char * input, char * position){
    char *ninput = var_frame_parse(input);
    char *noutput = var_frame_parse(output);
    string_append_literal(gen->output, "GETCHAR ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, position);
    string_append_literal(gen->output, "\n");
    free(ninput); free(noutput);
}
void ifj_type(generator gen, char * output, char * input){
    char *ninput = var_frame_parse(input);
    char *noutput = var_frame_parse(output);
    string_append_literal(gen->output, "TYPE ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, "\n");
    free(ninput); free(noutput);
}
void ifj_float2int(generator gen, char * output, char * input){
    char *ninput = var_frame_parse(input);
    char *noutput = var_frame_parse(output);
    string_append_literal(gen->output, "FLOAT2INT ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, "\n");
    free(ninput); free(noutput);
}
void ifj_int2char(generator gen, char * output, char * input){
    char *ninput = var_frame_parse(input);
    char *noutput = var_frame_parse(output);
    string_append_literal(gen->output, "INT2CHAR ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, "\n");
    free(ninput); free(noutput);
}
void ifj_int2str(generator gen, char * output, char * input){
    char *ninput = var_frame_parse(input);
    char *noutput = var_frame_parse(output);
    string_append_literal(gen->output, "INT2STR ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, "\n");
    free(ninput); free(noutput);
}
void ifj_float2str(generator gen, char * output, char * input){
    char *ninput = var_frame_parse(input);
    char *noutput = var_frame_parse(output);
    string_append_literal(gen->output, "FLOAT2STR ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, "\n");
    free(ninput); free(noutput);
}
void ifj_stri2int(generator gen, char * output, char * var1, char *var2){
    char *nvar1 = var_frame_parse(var1);
    char *nvar2 = var_frame_parse(var2);
    char *noutput = var_frame_parse(output);
    string_append_literal(gen->output, "STRI2INT ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, nvar1);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, nvar2);
    string_append_literal(gen->output, "\n");
    free(nvar1); free(nvar2); free(noutput);
}
void ifj_int2float(generator gen, char * output, char * input){
    char *ninput = var_frame_parse(input);
    char *noutput = var_frame_parse(output);
    string_append_literal(gen->output, "INT2FLOAT ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, "\n");
    free(ninput); free(noutput);
}
void exit_code(generator gen, char * code){
    string_append_literal(gen->output, "EXIT ");
    string_append_literal(gen->output, code);
    string_append_literal(gen->output, "\n");
}

// --- Helper Logic for Conversions ---

// Float to int if variable is float
void float_int_conversion(generator gen, char *var) {
    char tmp[20];
    snprintf(tmp, 20, "%u", gen->counter++);
    string is_float_label = string_create(20);
    string_append_literal(is_float_label, "IS_FLOAT_");
    string_append_literal(is_float_label, tmp);

    ifj_type(gen, "GF@tmp_ifj", var);
    op_eq(gen, "GF@tmp_ifj", "GF@tmp_ifj", "string@float");
    add_jumpifeq(gen, is_float_label->data, "GF@tmp_ifj", "bool@false");
    ifj_float2int(gen, var, var);

    label(gen, is_float_label->data);
    string_destroy(is_float_label);
}

// Generate string repetition, when one side of expression is string
void generate_repetition(generator gen, char *result, char *left, char *right) {
    char tmp[20];
    string start_label_str = string_create(20);
    string end_label_str = string_create(20);
    snprintf(tmp, 20, "%u", gen->counter++);
    string_append_literal(start_label_str, "REPETITION_START_");
    string_append_literal(start_label_str, tmp); 
    string_append_literal(end_label_str, "REPETITION_END_");
    string_append_literal(end_label_str, tmp);
    
    char *label_start = start_label_str->data;
    char *label_end = end_label_str->data;

    move_var(gen, "GF@tmp2", "string@");
    move_var(gen, "GF@tmp1", right);

    string_append_literal(gen->output, "\n# REPETITION LOOP START\n");
    op_eq(gen, "GF@tmp_if", "GF@tmp1", "int@0");
    add_jumpifeq(gen, label_end, "GF@tmp_if", "bool@true"); 
    op_lt(gen, "GF@tmp_if", "GF@tmp1", "int@0");
    add_jumpifeq(gen, label_end, "GF@tmp_if", "bool@true"); 

    label(gen, label_start);
    op_concat(gen, "GF@tmp2", "GF@tmp2", left);
    move_var(gen, "GF@tmp3", "int@1");
    op_sub(gen, "GF@tmp1", "GF@tmp1", "GF@tmp3");
    op_gt(gen, "GF@tmp_op", "GF@tmp1", "int@0");
    add_jumpifeq(gen, label_start, "GF@tmp_op", "bool@true");
    label(gen, label_end);
    string_append_literal(gen->output, "# REPETITION LOOP END\n");
    move_var(gen, result, "GF@tmp2");

    string_destroy(start_label_str);
    string_destroy(end_label_str);
}

// Check if both sides of expression are same type
void generate_type_check(generator gen, char *symb1, char *symb2, char *error_label) {
    char tmp[20];
    snprintf(tmp, 20, "%u", gen->counter++);
    string label_end = string_create(20);
    string_append_literal(label_end, "SKIP_CHECK_");
    string_append_literal(label_end, tmp);
    
    ifj_type(gen, "GF@tmp_type_l", symb1);
    ifj_type(gen, "GF@tmp_type_r", symb2);
    string_append_literal(gen->output, "\n# TYPE CHECK\n");
    add_jumpifeq(gen, label_end->data, "GF@tmp_type_r", "string@nil");
    add_jumpifneq(gen, error_label, "GF@tmp_type_l", "GF@tmp_type_r");
    label(gen, label_end->data);
    string_append_literal(gen->output, "# TYPE CHECK: OK\n");
}

// float to int conversion if is float
void generate_float_conversion(generator gen, char *var_name, char *type_name) {
    char tmp[20];
    snprintf(tmp, 20, "%u", gen->counter++);
    string label_end = string_create(20);
    string_append_literal(label_end, "SKIP_INT2FLOAT_");
    string_append_literal(label_end, tmp);
    
    add_jumpifeq(gen, label_end->data, type_name, "string@float"); 
    add_jumpifneq(gen, label_end->data, type_name, "string@int"); 
    
    string_append_literal(gen->output, "# INT TO FLOAT CONVERSION\n");
    ifj_int2float(gen, "GF@tmp1", var_name); 
    move_var(gen, var_name, "GF@tmp1");
    string_append_literal(gen->output, "# CONVERSION END\n");
    label(gen, label_end->data);
    string_destroy(label_end);
}

// Create add, or concatenation, based on types
void generate_add_conversion(generator gen, char *result, char *left, char *right) {
    char tmp[20];
    snprintf(tmp, 20, "%u", gen->counter++);
    string skip_val1_label = string_create(20);
    string_append_literal(skip_val1_label, "SKIP_VAL1_C_");
    string_append_literal(skip_val1_label, tmp);
    string skip_val2_label = string_create(20);
    string_append_literal(skip_val2_label, "SKIP_VAL2_C_");
    string_append_literal(skip_val2_label, tmp);
    string skip_concat = string_create(20);
    string_append_literal(skip_concat, "SKIP_CONCAT_");
    string_append_literal(skip_concat, tmp);
    string skip_end = string_create(20);
    string_append_literal(skip_end, "SKIP_END_");
    string_append_literal(skip_end, tmp);

    string_append_literal(gen->output, "\n# START ADDITION/CONCAT CHECK\n");
    ifj_type(gen, "GF@tmp_type_l", left);
    ifj_type(gen, "GF@tmp_type_r", right);

    op_eq(gen, "GF@tmp_ifj", "GF@tmp_type_l", "string@string"); 
    add_jumpifneq(gen, skip_concat->data, "GF@tmp_ifj", "bool@true"); 
    op_eq(gen, "GF@tmp_ifj", "GF@tmp_type_r", "string@string"); 
    add_jumpifneq(gen, skip_concat->data, "GF@tmp_ifj", "bool@true"); 

    op_concat(gen, result, left, right); // Concatenation
    jump(gen, skip_end->data); // Skip rest
    
    label(gen, skip_concat->data);
    op_eq(gen, "GF@tmp_ifj", "GF@tmp_type_l", "string@float"); // float conversion
    add_jumpifeq(gen, skip_val1_label->data, "GF@tmp_ifj", "bool@false"); 
    generate_float_conversion(gen, right, "GF@tmp_type_r");
    label(gen, skip_val1_label->data);
    
    op_eq(gen, "GF@tmp_ifj", "GF@tmp_type_r", "string@float"); //float conversion
    add_jumpifeq(gen, skip_val2_label->data, "GF@tmp_ifj", "bool@false");
    generate_float_conversion(gen, left, "GF@tmp_type_l");
    label(gen, skip_val2_label->data);
    
    string_append_literal(gen->output, "# END ADDITION/CONCAT CHECK\n");
    generate_type_check(gen, left, right, "ERR26");
    op_add(gen, result, left, right);
    label(gen, skip_end->data);

    string_destroy(skip_val1_label); string_destroy(skip_val2_label);
    string_destroy(skip_concat); string_destroy(skip_end);
}

// Generate multiplication if both are int/float, if one is string, generates repetition
void generate_mul_conversion(generator gen, char *result, char *left, char *right) {
    char tmp[20];
    snprintf(tmp, 20, "%u", gen->counter++);
    string skip_rep = string_create(20);
    string_append_literal(skip_rep, "SKIP_REP_");
    string_append_literal(skip_rep, tmp);
    string skip_end = string_create(20);
    string_append_literal(skip_end, "MUL_END_");
    string_append_literal(skip_end, tmp);

    string_append_literal(gen->output, "\n# MUL CHECK\n");
    ifj_type(gen, "GF@tmp_type_l", left);
    ifj_type(gen, "GF@tmp_type_r", right);

    op_eq(gen, "GF@tmp_ifj", "GF@tmp_type_l", "string@string");  // Repetition
    add_jumpifneq(gen, skip_rep->data, "GF@tmp_ifj", "bool@true"); 
    generate_repetition(gen, result, left, right); 
    jump(gen, skip_end->data); 
    
    label(gen, skip_rep->data);
    string_destroy(skip_rep);

    string skip_v1 = string_create(20);
    string_append_literal(skip_v1, "SKIP_V1_MUL_");
    string_append_literal(skip_v1, tmp);
    string skip_v2 = string_create(20);
    string_append_literal(skip_v2, "SKIP_V2_MUL_");
    string_append_literal(skip_v2, tmp);
    
    op_eq(gen, "GF@tmp_ifj", "GF@tmp_type_l", "string@float"); // Float conversion for MUL
    add_jumpifeq(gen, skip_v1->data, "GF@tmp_ifj", "bool@false"); 
    generate_float_conversion(gen, right, "GF@tmp_type_r");
    label(gen, skip_v1->data);
    
    op_eq(gen, "GF@tmp_ifj", "GF@tmp_type_r", "string@float"); // Float conversion for MUL
    add_jumpifneq(gen, skip_v2->data, "GF@tmp_ifj", "bool@true");
    generate_float_conversion(gen, left, "GF@tmp_type_l");
    label(gen, skip_v2->data);
    
    generate_type_check(gen, left, right, "ERR26");
    op_mul(gen, result, left, right); // Multiplication
    label(gen, skip_end->data);
    string_destroy(skip_v1); string_destroy(skip_v2); string_destroy(skip_end);
}

// convert both sides to float for DIV
void generate_div_conversion(generator gen, char *left, char *right) {    
    string_append_literal(gen->output, "\n# DIV CHECK\n");
    ifj_type(gen, "GF@tmp_type_l", left);
    ifj_type(gen, "GF@tmp_type_r", right);
    generate_float_conversion(gen, right, "GF@tmp_type_r");
    generate_float_conversion(gen, left, "GF@tmp_type_l");
    generate_type_check(gen, left, right, "ERR26");
}

// Correct types
void process_auto_corecion(generator gen, char *left, char *right) {
    char tmp[20];
    snprintf(tmp, 20, "%u", gen->counter++);
    string skip_v1 = string_create(20);
    string_append_literal(skip_v1, "AC_V1_");
    string_append_literal(skip_v1, tmp);
    string skip_v2 = string_create(20);
    string_append_literal(skip_v2, "AC_V2_");
    string_append_literal(skip_v2, tmp);

    string_append_literal(gen->output, "\n# BINARY AUTO COERCION\n");
    ifj_type(gen, "GF@tmp_type_l", left);
    ifj_type(gen, "GF@tmp_type_r", right);

    op_eq(gen, "GF@tmp_ifj", "GF@tmp_type_l", "string@float");
    add_jumpifeq(gen, skip_v1->data, "GF@tmp_ifj", "bool@false"); 
    generate_float_conversion(gen, right, "GF@tmp_type_r");
    label(gen, skip_v1->data);
    
    op_eq(gen, "GF@tmp_ifj", "GF@tmp_type_r", "string@float");
    add_jumpifneq(gen, skip_v2->data, "GF@tmp_ifj", "bool@true");
    generate_float_conversion(gen, left, "GF@tmp_type_l");
    label(gen, skip_v2->data);
    
    string_destroy(skip_v1); string_destroy(skip_v2);
    generate_type_check(gen, left, right, "ERR26");
}

// Recursive expression generation with the use of stack
void generate_expression_stack(generator gen, ast_expression node) {
    if (!node) return;

    if (node->type == AST_VALUE || node->type == AST_IDENTIFIER) { // Value/ID
        char *val = ast_value_to_string(node, NULL);
        push(gen, val);
        if (node->type == AST_VALUE && node->operands.identity.value_type != AST_VALUE_IDENTIFIER && node->operands.identity.value_type != AST_VALUE_NULL) free(val);
        return;
    }

    if (node->type == AST_FUNCTION_CALL) { // Function call
        generate_function_call(gen, NULL, node);
        push(gen, "GF@fn_ret");
        return;
    }
    
    if (node->type == AST_IFJ_FUNCTION_EXPR) { // IFJ function call
        generate_ifjfunction(gen, node->operands.ifj_function->name, node->operands.ifj_function->parameters, "GF@tmp1");
        push(gen, "GF@tmp1");
        return;
    }

    if (get_op_arity(node->type) == ARITY_UNARY) { // Unary expression
        generate_expression_stack(gen, node->operands.unary_op.expression);
        pop(gen, "GF@tmp1");
        
        if (node->type == AST_NOT) {
            op_not(gen, "GF@tmp1", "GF@tmp1");
        }
        
        push(gen, "GF@tmp1");
        return;
    }

    if (get_op_arity(node->type) == ARITY_BINARY) { // Binary expression
        generate_expression_stack(gen, node->operands.binary_op.left); // Recursive left side
        
        if (node->type == AST_IS) {
            pop(gen, "GF@tmp_l");
            char *val_type = "string@nil";
            char *right_raw = node->operands.binary_op.right->operands.identifier.value;
            
            if (strcmp(right_raw, "Num") == 0) val_type = "string@int";
            else if (strcmp(right_raw, "String") == 0) val_type = "string@string";
            else if (strcmp(right_raw, "Null") == 0) val_type = "string@nil";
            
            ifj_type(gen, "GF@tmp_type_r", "GF@tmp_l");
            op_eq(gen, "GF@tmp1", "GF@tmp_type_r", val_type);
            push(gen, "GF@tmp1");
            return;
        }

        generate_expression_stack(gen, node->operands.binary_op.right); // Recursive right side
        
        pop(gen, "GF@tmp_r"); // Get result of nested expression
        pop(gen, "GF@tmp_l"); // Get result of nested expression
        
        char *res = "GF@tmp1";

        switch (node->type) { // Generate all types of operations
            case AST_ADD:
                generate_add_conversion(gen, res, "GF@tmp_l", "GF@tmp_r");
                break;
            case AST_SUB:
                process_auto_corecion(gen, "GF@tmp_l", "GF@tmp_r");
                op_sub(gen, res, "GF@tmp_l", "GF@tmp_r");
                break;
            case AST_MUL:
                generate_mul_conversion(gen, res, "GF@tmp_l", "GF@tmp_r");
                break;
            case AST_DIV:
                generate_div_conversion(gen, "GF@tmp_l", "GF@tmp_r");
                op_div(gen, res, "GF@tmp_l", "GF@tmp_r");
                break;
            case AST_LT:
                process_auto_corecion(gen, "GF@tmp_l", "GF@tmp_r");
                op_lt(gen, res, "GF@tmp_l", "GF@tmp_r");
                break;
            case AST_GT:
                process_auto_corecion(gen, "GF@tmp_l", "GF@tmp_r");
                op_gt(gen, res, "GF@tmp_l", "GF@tmp_r");
                break;
            case AST_LE:
                 process_auto_corecion(gen, "GF@tmp_l", "GF@tmp_r");
                 op_gt(gen, "GF@tmp2", "GF@tmp_l", "GF@tmp_r");
                 op_not(gen, res, "GF@tmp2");
                 break;
            case AST_GE:
                 process_auto_corecion(gen, "GF@tmp_l", "GF@tmp_r");
                 op_lt(gen, "GF@tmp2", "GF@tmp_l", "GF@tmp_r");
                 op_not(gen, res, "GF@tmp2");
                 break;
            case AST_EQUALS:
                process_auto_corecion(gen, "GF@tmp_l", "GF@tmp_r");
                op_eq(gen, res, "GF@tmp_l", "GF@tmp_r");
                break;
            case AST_NOT_EQUAL:
                process_auto_corecion(gen, "GF@tmp_l", "GF@tmp_r");
                op_eq(gen, res, "GF@tmp_l", "GF@tmp_r");
                op_not(gen, res, res);
                break;
            case AST_AND:
                op_and(gen, res, "GF@tmp_l", "GF@tmp_r");
                break;
            case AST_OR:
                op_or(gen, res, "GF@tmp_l", "GF@tmp_r");
                break;
            case AST_CONCAT:
                op_concat(gen, res, "GF@tmp_l", "GF@tmp_r");
                break;
            default:
                break;
        }
        push(gen, res); // Push for recursive expressions
    }
}

// Start of recursive expressions
void generate_expression(generator gen, char * result, ast_expression node){
    generate_expression_stack(gen, node);
    pop(gen, result);
}

// ifj.str handling
void generate_ifj_str(generator gen, char *result, ast_parameter param) {
    char tmp[20];
    snprintf(tmp, 20, "%u", gen->counter++);
    string label_int = string_create(20);
    string label_string = string_create(20);
    string label_end = string_create(20);
    
    string_append_literal(label_int, "STR_INT_");
    string_append_literal(label_int, tmp);
    string_append_literal(label_string, "STR_FLOAT_");
    string_append_literal(label_string, tmp);
    string_append_literal(label_end, "STR_END_");
    string_append_literal(label_end, tmp);
    
    move_var(gen, "GF@tmp1", ast_value_to_string(NULL, param));
    ifj_type(gen, "GF@tmp_ifj", "GF@tmp1");
    add_jumpifeq(gen, label_int->data, "GF@tmp_ifj", "string@int");
    add_jumpifeq(gen, label_string->data, "GF@tmp_ifj", "string@float");
    move_var(gen, result, "nil@nil");
    jump(gen, label_end->data);
    label(gen, label_int->data);
    ifj_int2str(gen, result, "GF@tmp1"); 
    jump(gen, label_end->data);
    label(gen, label_string->data);
    ifj_float2str(gen, result, "GF@tmp1");
    label(gen, label_end->data);
    string_destroy(label_int); string_destroy(label_string); string_destroy(label_end);
}

// ifj.substring handling
void generate_substring(generator gen, char *result, char *var1, char *var2, char *var3) {
    char tmp[20];
    snprintf(tmp, 20, "%u", gen->counter++);
    string skip_label = string_create(20);
    string_append_literal(skip_label, "SKIP_SUB_");
    string_append_literal(skip_label, tmp);
    string loop_label = string_create(20);
    string_append_literal(loop_label, "LOOP_SUB_");
    string_append_literal(loop_label, tmp);

    move_var(gen, "GF@tmp2", var2);
    move_var(gen, "GF@tmp3", var3);

    float_int_conversion(gen, "GF@tmp2");
    float_int_conversion(gen, "GF@tmp3");

    ifj_type(gen, "GF@tmp_type_l", "GF@tmp2");
    ifj_type(gen, "GF@tmp_type_r", "GF@tmp3");
    op_eq(gen, "GF@tmp_ifj", "GF@tmp_type_l", "string@int");
    add_jumpifeq(gen, "ERR26", "GF@tmp_ifj", "bool@false");
    op_eq(gen, "GF@tmp_ifj", "GF@tmp_type_r", "string@int");
    add_jumpifeq(gen, "ERR26", "GF@tmp_ifj", "bool@false");

    move_var(gen, result, "nil@nil");
    ifj_strlen(gen, "GF@tmp1", var1);
    op_lt(gen, "GF@tmp_ifj", "GF@tmp2", "int@0");
    add_jumpifeq(gen, skip_label->data, "GF@tmp_ifj", "bool@true");
    op_lt(gen, "GF@tmp_ifj", "GF@tmp2", "GF@tmp1");
    add_jumpifeq(gen, skip_label->data, "GF@tmp_ifj", "bool@false");
    op_lt(gen, "GF@tmp_ifj", "GF@tmp3", "int@0");
    add_jumpifeq(gen, skip_label->data, "GF@tmp_ifj", "bool@true");
    op_lt(gen, "GF@tmp_ifj", "GF@tmp3", "GF@tmp1");
    add_jumpifeq(gen, skip_label->data, "GF@tmp_ifj", "bool@false");

    move_var(gen, result, "string@");
    move_var(gen, "GF@tmp_l", "GF@tmp2");
    label(gen, loop_label->data);
    op_lt(gen, "GF@tmp_ifj", "GF@tmp_l", "GF@tmp3");
    add_jumpifeq(gen, skip_label->data, "GF@tmp_ifj", "bool@false");
    ifj_getchar(gen, "GF@tmp_r", var1, "GF@tmp_l");
    op_concat(gen, result, result, "GF@tmp_r");
    op_add(gen, "GF@tmp_l", "GF@tmp_l", "int@1");
    jump(gen, loop_label->data);
    label(gen, skip_label->data);
    string_destroy(loop_label); string_destroy(skip_label);
}

// ifj.strcmp handling
void generate_strcmp(generator gen, char *result, char *left, char *right) {
    char tmp[20];
    snprintf(tmp, 20, "%u", gen->counter++);
    string loop_label = string_create(20);
    string_append_literal(loop_label, "LOOP_CMP_");
    string_append_literal(loop_label, tmp);
    string skip_label = string_create(20);
    string_append_literal(skip_label, "SKIP_CMP_");
    string_append_literal(skip_label, tmp);
    string same_label = string_create(20);
    string_append_literal(same_label, "SAME_CHAR_");
    string_append_literal(same_label, tmp);

    string_append_literal(gen->output, "\n#STRCMP START\n");
    move_var(gen, result, "int@0");
    move_var(gen, "GF@tmp_l", "int@0");
    ifj_strlen(gen, "GF@tmp2", left);
    ifj_strlen(gen, "GF@tmp3", right);
    move_var(gen, "GF@tmp_r", "GF@tmp3");
    op_sub(gen, result, "GF@tmp2", "GF@tmp3");
    op_lt(gen, "GF@tmp3", "GF@tmp2", "GF@tmp3");
    add_jumpifeq(gen, loop_label->data, "GF@tmp3", "bool@false");
    move_var(gen, "GF@tmp_r", "GF@tmp2");

    label(gen, loop_label->data);
    op_lt(gen, "GF@tmp_ifj", "GF@tmp_l", "GF@tmp_r");
    add_jumpifeq(gen, skip_label->data, "GF@tmp_ifj", "bool@false");
    ifj_getchar(gen, "GF@tmp2", left, "GF@tmp_l");
    ifj_getchar(gen, "GF@tmp3", right, "GF@tmp_l");
    op_eq(gen, "GF@tmp_ifj", "GF@tmp2", "GF@tmp3");
    add_jumpifeq(gen, same_label->data, "GF@tmp_ifj", "bool@true");
    op_sub(gen, result, result, "int@1");
    label(gen, same_label->data);
    op_add(gen, "GF@tmp_l", "GF@tmp_l", "int@1");
    jump(gen, loop_label->data);
    label(gen, skip_label->data);
    string_append_literal(gen->output, "\n#STRCMP END\n");
    string_destroy(loop_label); string_destroy(same_label); string_destroy(skip_label);
}

// All IFJ functions handling
void generate_ifjfunction(generator gen, char* name, ast_parameter params, char* output) {
    if(strcmp(name, "str") == 0) generate_ifj_str(gen, output, params);
    else if(strcmp(name, "chr") == 0) {
        move_var(gen, "GF@tmp1", ast_value_to_string(NULL, params));
        float_int_conversion(gen, "GF@tmp1");
        ifj_int2char(gen, output, "GF@tmp1");
    }
    else if(strcmp(name, "floor") == 0) {
        char tmp[20];
        snprintf(tmp, 20, "%u", gen->counter++);
        string is_float = string_create(20);
        string_append_literal(is_float, "IS_FLOAT_");
        string_append_literal(is_float, tmp);

        move_var(gen, "GF@tmp1", ast_value_to_string(NULL, params));
        ifj_type(gen, "GF@tmp_ifj", "GF@tmp1");
        op_eq(gen, "GF@tmp_ifj", "GF@tmp_ifj", "string@float");
        add_jumpifeq(gen, is_float->data, "GF@tmp_ifj", "bool@false");

        ifj_float2int(gen, output, "GF@tmp1");

        label(gen, is_float->data);
        string_destroy(is_float);
    }
    else if(strcmp(name, "length") == 0) ifj_strlen(gen, output, ast_value_to_string(NULL, params));
    else if(strcmp(name, "ord") == 0) {
        move_var(gen, "GF@tmp1", ast_value_to_string(NULL, params->next));
        float_int_conversion(gen, "GF@tmp1");
        ifj_stri2int(gen, output, ast_value_to_string(NULL, params), "GF@tmp1");
    }
    else if(strcmp(name, "read_num") == 0) {
        char tmp[20];
        snprintf(tmp, 20, "%u", gen->counter++);
        string is_float = string_create(20);
        string_append_literal(is_float, "IS_FLOAT_");
        string_append_literal(is_float, tmp);
        ifj_read(gen, output, "float");
        ifj_float2int(gen, "GF@tmp2", output);
        ifj_int2float(gen, "GF@tmp3", "GF@tmp2");
        op_eq(gen, "GF@tmp_ifj", "GF@tmp3", output);
        add_jumpifeq(gen, is_float->data, "GF@tmp_ifj", "bool@false");
        move_var(gen, output, "GF@tmp2");
        label(gen, is_float->data);
        string_destroy(is_float);
    }
    else if(strcmp(name, "read_str") == 0) ifj_read(gen, output, "string");
    else if(strcmp(name, "strcmp") == 0) generate_strcmp(gen, output, ast_value_to_string(NULL, params), ast_value_to_string(NULL, params->next));
    else if(strcmp(name, "substring") == 0) generate_substring(gen, output, ast_value_to_string(NULL, params), ast_value_to_string(NULL, params->next), ast_value_to_string(NULL, params->next->next));
    else if(strcmp(name, "write") == 0) {
        char tmp[20];
        snprintf(tmp, 20, "%u", gen->counter++);
        string is_float_label = string_create(20);
        string_append_literal(is_float_label, "IS_FLOAT_");
        string_append_literal(is_float_label, tmp);
        
        move_var(gen, "GF@tmp1", ast_value_to_string(NULL, params));

        ifj_type(gen, "GF@tmp_ifj", "GF@tmp1");
        op_eq(gen, "GF@tmp2", "GF@tmp_ifj", "string@float");
        add_jumpifeq(gen, is_float_label->data, "GF@tmp2", "bool@false");
        
        ifj_float2int(gen, "GF@tmp2", "GF@tmp1");
        ifj_int2float(gen, "GF@tmp3", "GF@tmp2");
        op_eq(gen, "GF@tmp_ifj", "GF@tmp3", "GF@tmp1");
        add_jumpifeq(gen, is_float_label->data, "GF@tmp_ifj", "bool@false");
        move_var(gen, "GF@tmp1", "GF@tmp2");

        label(gen, is_float_label->data);
        string_destroy(is_float_label);

        ifj_write(gen, "GF@tmp1");
    }
}

// Function generation with parameters handling
void generate_function_call(generator gen, ast_node node, ast_expression expr_node){
    stack stack;
    stack_init(&stack);
    ast_parameter param;
    char *name;
    if (node) {
        param = node->data.function_call->parameters;
        name = node->data.function_call->name;
    } else {
        param = expr_node->operands.function_call->parameters;
        name = expr_node->operands.function_call->name;
    }
    while(param != NULL){
        stack_push(&stack, ast_value_to_string(NULL, param));
        param = param->next;
    }
    char *param_name;
    while(!stack_is_empty(&stack)){
        param_name = stack_pop(&stack);
        push(gen, param_name);
    }
    fn_call(gen, name);
    stack_free(&stack);
}

// Return generation
void generate_function_return(generator gen, ast_node node){
    if(node->data.return_expr.output)
        generate_expression(gen, "GF@fn_ret", node->data.return_expr.output);
    popframe(gen);
    return_code(gen);
}

// Assignment generation
void generate_assignment(generator gen, ast_node node){
    if(node->data.assignment.value != NULL){
        generate_expression(gen, node->data.assignment.cg_name, node->data.assignment.value);
    }
}

// Declaration generation
void generate_declaration(generator gen, ast_node node){
    define_variable(gen, node->data.declaration.cg_name);
}

// Condition generation
void generate_if_statement(generator gen, ast_node node){
    char tmp[20];
    string end_label = string_create(20);
    string else_lable = string_create(20);
    ast_block body;

    string_append_literal(end_label, "conditionEnd");
    snprintf(tmp, 20, "%u", gen->counter++);
    string_append_literal(end_label, tmp);

    if(node->data.condition.else_branch == NULL)
        string_append_literal(else_lable, end_label->data);
    else{
        string_clear(else_lable);
        string_append_literal(else_lable, "ifEnd");
        snprintf(tmp, 20, "%u", gen->counter);
        string_append_literal(else_lable, tmp);
    }
    
    string_append_literal(gen->output, "\n# IF CONDITION\n");
    generate_expression(gen, "GF@tmp_if", node->data.condition.condition);
    add_jumpifeq(gen, else_lable->data, "GF@tmp_if", "bool@false");
    string_append_literal(gen->output, "# IF CONDITION END\n\n");

    if(node->data.condition.if_branch != NULL){
        string_append_literal(gen->output, "# IF BRANCH\n");
        body = node->data.condition.if_branch;
        generate_block(gen, body, true);
        jump(gen, end_label->data);
    }
    if(node->data.condition.else_branch != NULL){
        label(gen, else_lable->data);
        string_append_literal(gen->output, "\n# ELSE BRANCH\n");
        body = node->data.condition.else_branch;
        generate_block(gen, body, true);
    }

    label(gen, end_label->data);
    string_append_literal(gen->output, "\n");
    string_destroy(end_label); string_destroy(else_lable);
}

// While loop generation
void generate_while(generator gen, ast_node node){
    char tmp[20];
    string while_start = string_create(20);
    string while_end = string_create(20);
    string_append_literal(while_start, "whileStart");
    string_append_literal(while_end, "whileEnd");
    snprintf(tmp, 20, "%u", gen->counter++);
    string_append_literal(while_start, tmp);
    string_append_literal(while_end, tmp);

    loop_labels_t *new_labels = (loop_labels_t *)malloc(sizeof(loop_labels_t)); // Alocation for break and continue handling
    if (!new_labels) return;

    // Saving whileStart and whileEnd for break and continue handling
    new_labels->start_label = while_start->data; 
    new_labels->end_label = while_end->data;
    
    stack_push(&gen->loop_stack, new_labels);

    string_append_literal(gen->output, "\n# WHILE LOOP START\n");
    generate_expression(gen, "GF@tmp_while", node->data.while_loop.condition);
    add_jumpifeq(gen, while_end->data, "GF@tmp_while", "bool@false");

    string_append_literal(gen->output, "\n");
    
    ast_block body = node->data.while_loop.body;
    ast_node current = body->first;
    while (current) { //Declare before while for double declaration handling
        if (current->type == AST_VAR_DECLARATION) {
            define_variable(gen, current->data.declaration.cg_name);
        }
        current = current->next;
    }

    label(gen, while_start->data);

    generate_block(gen, node->data.while_loop.body, false);

    string_append_literal(gen->output, "\n");
    generate_expression(gen, "GF@tmp_while", node->data.while_loop.condition);
    add_jumpifneq(gen, while_start->data, "GF@tmp_while", "bool@false");

    label(gen, while_end->data);
    string_append_literal(gen->output, "# WHILE LOOP END\n\n");

    loop_labels_t *freed_labels = (loop_labels_t *)stack_pop(&gen->loop_stack); // Free for break and continue handling
    if (freed_labels) free(freed_labels);
    string_destroy(while_start); string_destroy(while_end);
}

// Generation of a node
void generate_node(ast_node node, generator gen, bool declare){
    switch(node->type){
        case AST_CONDITION: generate_if_statement(gen, node); break;
        case AST_VAR_DECLARATION: if(declare) generate_declaration(gen, node); break;
        case AST_ASSIGNMENT: generate_assignment(gen, node); break;
        case AST_IFJ_FUNCTION: generate_ifjfunction(gen, node->data.ifj_function->name, node->data.ifj_function->parameters, NULL); break;
        case AST_WHILE_LOOP: generate_while(gen, node); break;
        case AST_CALL_FUNCTION: generate_function_call(gen, node, NULL); break;
        case AST_RETURN: generate_function_return(gen, node); break;
        case AST_BLOCK: generate_block(gen, node->data.block, true); break;
        case AST_FUNCTION: case AST_GETTER: case AST_SETTER: generate_function(gen, node); break;
        case AST_BREAK: {
            loop_labels_t *current_labels = (loop_labels_t *)stack_top(&gen->loop_stack);
            jump(gen, current_labels->end_label);
            break;
        }
        case AST_CONTINUE: {
            loop_labels_t *current_labels = (loop_labels_t *)stack_top(&gen->loop_stack);
            jump(gen, current_labels->start_label);
            break;
        }
        default: break;
    }
}

// Generation of a block
void generate_block(generator gen, ast_block block, bool declare){
    ast_node node = block->first;
    while (node) {
        generate_node(node, gen, declare);
        node = node->next;
    }
}

// Start of code initiation
void init_code(generator gen, ast ast){
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
        define_variable(gen, "GF@tmp3");
        define_variable(gen, "GF@fn_ret");
        define_variable(gen, "GF@tmp_type_l");
        define_variable(gen, "GF@tmp_type_r");
        sem_def_globals(gen);
    }
}

// Main Code generation
void generate_code(generator gen, ast ast){
    if(ast != NULL && ast->class_list != NULL){
        ast_class program = ast->class_list; 
        ast_block program_body = program->current;
        ast_node function = program_body->first;
        while (function != NULL) { // Generate main function first
            if (function->type == AST_FUNCTION && strcmp(function->data.function->name, "main") == 0) {
                generate_main(gen, function);
                break;
            }
            function = function->next;
        }
        generate_block(gen, program_body, true); // Generate all other functions

        label(gen, "ERR26"); // Error label for runtime error handling
        string_append_literal(gen->output, "# ERROR: Incompatible types for binary operation.\n");
        exit_code(gen, "int@26");
        string_append_literal(gen->output, "\n#END OF FILE\n");
    }
}

// Function/Getter/Setter generation without Main function
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
    else return;

    if (strcmp(name, "main")) { // If not function Main
        string_append_literal(gen->output, "\n# START OF FUNCTION ---");
        string_append_literal(gen->output, name);
        string_append_literal(gen->output, "---\n");
        label(gen, name);
        createframe(gen);
        pushframe(gen);
        while(param != NULL){
            define_variable(gen, ast_value_to_string(NULL, param));
            pop(gen, ast_value_to_string(NULL, param));
            param = param->next;
        }
        if(node->type == AST_SETTER) {
            define_variable(gen, node->data.setter.param);
            pop(gen, node->data.setter.param);
        }
        generate_block(gen, fun_body, true);
        popframe(gen);
        string_append_literal(gen->output, "# END OF FUNCTION ---");
        string_append_literal(gen->output, name);
        string_append_literal(gen->output, "---\n");
        move_var(gen, "GF@fn_ret", "nil@nil");
        return_code(gen);
    }
}

// Main function generation
void generate_main(generator gen, ast_node node) {
    char *name = node->data.function->name;
    ast_parameter param = node->data.function->parameters;
    ast_block fun_body = node->data.function->code;

    string_append_literal(gen->output, "\n# START OF MAIN FUNCTION ---");
    string_append_literal(gen->output, name);
    string_append_literal(gen->output, "---\n");
    label(gen, name);
    createframe(gen);
    pushframe(gen);
    while(param != NULL){
        define_variable(gen, ast_value_to_string(NULL, param));
        pop(gen, ast_value_to_string(NULL, param));
        param = param->next;
    }
    generate_block(gen, fun_body, true);
    popframe(gen);
    string_append_literal(gen->output, "# END OF MAIN FUNCTION ---");
    string_append_literal(gen->output, name);
    string_append_literal(gen->output, "---\n");
    exit_code(gen, "int@0\n");
}