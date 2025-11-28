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
#include "semantic.h"


void generate_unary(generator gen, char * result, ast_expression node);
void generate_binary(generator gen, char * result, ast_expression node);
void generate_expression(generator gen, char * result, ast_expression node);
void generate_function_call(generator gen, ast_node node, ast_expression expr_node);
void generate_assignment(generator gen, ast_node node);
void generate_declaration(generator gen, ast_node node);
void generate_if_statement(generator gen, ast_node node);
void generate_node(ast_node node, generator gen);
void init_code(generator gen, ast syntree);
void generate_code(generator gen, ast syntree);
void generate_function(generator gen, ast_node node);
void generate_main(generator gen, ast_node node);
void generate_block(generator gen, ast_block block);
void generate_ifjfunction(generator gen, char* name, ast_parameter params, char* output);


const char *PREFIXES[] = {
    "int@", 
    "float@", 
    "string@",
    "GF@",
    "LF@",
    "nil@",
    "bool@",
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

static void sem_def_globals(generator gen) {
    char **globals = NULL;
    size_t count   = 0;

    int rc = semantic_get_magic_globals(&globals, &count);
    if (rc != SUCCESS) {
        return;
    }
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

char *ast_value_to_string(ast_expression expr_node, ast_parameter param_node) {
    char *result = NULL;
    ast_value_type type;
    int int_val;
    float float_val;
    char *char_val;
    if(expr_node) {
        if(expr_node->type == AST_IDENTIFIER) {
            char_val = expr_node->operands.identifier.value;
            type = AST_VALUE_IDENTIFIER;
        }
        else {
            type = expr_node->operands.identity.value_type;
            switch (type) {
            case AST_VALUE_INT:
                int_val = expr_node->operands.identity.value.int_value;
                break;
            case AST_VALUE_FLOAT:
                float_val = expr_node->operands.identity.value.double_value;
                break;
            case AST_VALUE_NULL:
                break;
            case AST_VALUE_IDENTIFIER:
                char_val = expr_node->operands.identity.value.string_value;
                break;
            default:
                char_val = expr_node->operands.identity.value.string_value;
                if (strcmp(char_val, "Num") == 0)
                    char_val = "string@int";
                else if (strcmp(char_val, "String") == 0)
                    char_val = "string@string";
                else if (strcmp(char_val, "Null") == 0)
                    char_val = "string@nil";
                break;
            }
        }
    }
    else{
        type = param_node->value_type;
        switch (type) {
        case AST_VALUE_INT:
            int_val = param_node->value.int_value;
            break;
        case AST_VALUE_FLOAT:
            float_val = param_node->value.double_value;
            break;
        case AST_VALUE_NULL:
            break;
        default:
            char_val = param_node->value.string_value;
            break;
        }
    }
    switch (type) {
        case AST_VALUE_INT:
            result = malloc((12 + 4) * sizeof(char));
            if (result) {
                sprintf(result, "int@%d", int_val);
            }
            break;
        
        case AST_VALUE_FLOAT:
            result = malloc((64 + 6) * sizeof(char));
            if (result) {
                sprintf(result, "float@%a", float_val);
            }
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
        case AST_IS:
        case AST_CONCAT:
            return ARITY_BINARY;
        case AST_NOT:
            return ARITY_UNARY;
        default:
            return ARITY_UNDEFINED;
    }
}

char* generate_temp_var(generator gen) {
    size_t size = 16 + 10; 
    char *tmp_name = (char*)malloc(size);

    if (tmp_name == NULL) {
        fprintf(stderr, "Chyba alokace paměti pro dočasnou proměnnou.\n");
        exit(ERR_INTERNAL); 
    }
    snprintf(tmp_name, size, "LF@expr_res_%u", gen->counter++);
    
    return tmp_name;
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
    char *nsymb1 = var_frame_parse(symb1);
    char *nsymb2 = var_frame_parse(symb2);
    string_append_literal(gen->output, "JUMPIFEQ ");
    string_append_literal(gen->output, label);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, nsymb1);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, nsymb2);
    string_append_literal(gen->output, "\n");
    free(nsymb1);
    free(nsymb2);
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
    string_append_literal(gen->output, "GETCHAR ");
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
void ifj_int2str(generator gen, char * output, char * input){
    char *ninput = var_frame_parse(input);
    char *noutput = var_frame_parse(output);
    string_append_literal(gen->output, "INT2STR ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, "\n");
    free(ninput);
    free(noutput);
}
void ifj_float2str(generator gen, char * output, char * input){
    char *ninput = var_frame_parse(input);
    char *noutput = var_frame_parse(output);
    string_append_literal(gen->output, "FLOAT2STR ");
    string_append_literal(gen->output, noutput);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, ninput);
    string_append_literal(gen->output, "\n");
    free(ninput);
    free(noutput);
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
    free(nvar1);
    free(nvar2);
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

    // Využití GF@tmp1 jako počítadla (POČET OPAKOVÁNÍ) a GF@tmp2 jako výsledného řetězce (VÝSLEDEK)
    move_var(gen, "GF@tmp2", "string@");
    move_var(gen, "GF@tmp1", right);

    string_append_literal(gen->output, "\n# REPETITION LOOP START\n");
        

    // počet opakování <= 0
    op_eq(gen, "GF@tmp_if", "GF@tmp1", "int@0");
    add_jumpifeq(gen, label_end, "GF@tmp_if", "bool@true"); 
    op_lt(gen, "GF@tmp_if", "GF@tmp1", "int@0");
    add_jumpifeq(gen, label_end, "GF@tmp_if", "bool@true"); 

    label(gen, label_start);

    // VÝSLEDEK = VÝSLEDEK + ŘETĚZEC_K_OPAKOVÁNÍ
    op_concat(gen, "GF@tmp2", "GF@tmp2", left);

    // POČÍTADLO = POČÍTADLO - 1
    move_var(gen, "GF@tmp3", "int@1");
    op_sub(gen, "GF@tmp1", "GF@tmp1", "GF@tmp3");
    
    // Dokud POČÍTADLO > 0
    op_gt(gen, "GF@tmp_op", "GF@tmp1", "int@0");
    add_jumpifeq(gen, label_start, "GF@tmp_op", "bool@true");

    label(gen, label_end);
    string_append_literal(gen->output, "# REPETITION LOOP END\n");

    move_var(gen, result, "GF@tmp2");

    string_destroy(start_label_str);
    string_destroy(end_label_str);
}

void generate_type_check(generator gen, char *symb1, char *symb2, char *error_label) {
    
    ifj_type(gen, "GF@tmp1", symb1);

    ifj_type(gen, "GF@tmp2", symb2);

    string_append_literal(gen->output, "\n# TYPE CHECK: If types of ");
    string_append_literal(gen->output, symb1);
    string_append_literal(gen->output, " and ");
    string_append_literal(gen->output, symb2);
    string_append_literal(gen->output, " are not equal, jump to error.\n");

    add_jumpifneq(gen, error_label, "GF@tmp1", "GF@tmp2");
    
    string_append_literal(gen->output, "# TYPE CHECK: OK\n");
}

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
    
    string_append_literal(gen->output, "\n# STR: Runtime type check\n");

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
    string_append_literal(gen->output, "# STR: End type check\n\n");

    string_destroy(label_int);
    string_destroy(label_string);
    string_destroy(label_end);
}

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

void generate_add_conversion(generator gen, char *result, char *left, char *right) {
    char tmp[20];
    snprintf(tmp, 20, "%u", gen->counter++);
    string skip_val1_conversion_label = string_create(20);
    string_append_literal(skip_val1_conversion_label, "SKIP_VAL1_COERCION_");
    string_append_literal(skip_val1_conversion_label, tmp);

    string skip_val2_conversion_label = string_create(20);
    string_append_literal(skip_val2_conversion_label, "SKIP_VAL2_COERCION_");
    string_append_literal(skip_val2_conversion_label, tmp);
    
    string skip_concat_label = string_create(20);
    string_append_literal(skip_concat_label, "SKIP_CONCAT_");
    string_append_literal(skip_concat_label, tmp);

    string skip_end_label = string_create(20);
    string_append_literal(skip_end_label, "SKIP_END_");
    string_append_literal(skip_end_label, tmp);

    string_append_literal(gen->output, "\n# START ADDITION/CONCAT CHECK\n");

    ifj_type(gen, "GF@tmp_l", left);
    ifj_type(gen, "GF@tmp_r", right);

    op_eq(gen, "GF@tmp_ifj", "GF@tmp_l", "string@string"); 
    add_jumpifneq(gen, skip_concat_label->data, "GF@tmp_ifj", "bool@true"); 
    
    op_eq(gen, "GF@tmp_ifj", "GF@tmp_r", "string@string"); 
    add_jumpifneq(gen, skip_concat_label->data, "GF@tmp_ifj", "bool@true"); 

    op_concat(gen, result, left, right); 
    jump(gen, skip_end_label->data); 
    
    label(gen, skip_concat_label->data);

    op_eq(gen, "GF@tmp_ifj", "GF@tmp_l", "string@float");
    add_jumpifeq(gen, skip_val1_conversion_label->data, "GF@tmp_ifj", "bool@false"); 
    generate_float_conversion(gen, right, "GF@tmp_r");
    label(gen, skip_val1_conversion_label->data);
    
    op_eq(gen, "GF@tmp_ifj", "GF@tmp_r", "string@float");
    add_jumpifneq(gen, skip_val2_conversion_label->data, "GF@tmp_ifj", "bool@true");
    generate_float_conversion(gen, left, "GF@tmp_l");
    label(gen, skip_val2_conversion_label->data);
    
    string_append_literal(gen->output, "# END ADDITION/CONCAT CHECK\n");
    generate_type_check(gen, left, right, "ERR26");
    op_add(gen, result, left, right);
    label(gen, skip_end_label->data);

    string_destroy(skip_val1_conversion_label);
    string_destroy(skip_val2_conversion_label);
    string_destroy(skip_concat_label); 
    string_destroy(skip_end_label);
}

void generate_mul_conversion(generator gen, char *result, char *left, char *right) {
    char tmp[20];
        snprintf(tmp, 20, "%u", gen->counter++);
        string skip_repetition_label = string_create(20);
        string_append_literal(skip_repetition_label, "SKIP_REPETITION_");
        string_append_literal(skip_repetition_label, tmp);

        string skip_end_label = string_create(20);
        string_append_literal(skip_end_label, "MUL_END_");
        string_append_literal(skip_end_label, tmp);

        string_append_literal(gen->output, "\n# START STRING REPETITION CHECK (STRING * INT)\n");

        ifj_type(gen, "GF@tmp_l", left);
        ifj_type(gen, "GF@tmp_r", right);

        op_eq(gen, "GF@tmp_ifj", "GF@tmp_l", "string@string"); 
        add_jumpifneq(gen, skip_repetition_label->data, "GF@tmp_ifj", "bool@true"); 
        
        generate_repetition(gen, result, left, right); 
        jump(gen, skip_end_label->data); 
        
        label(gen, skip_repetition_label->data);
        
        string_append_literal(gen->output, "# END STRING REPETITION CHECK\n");
        string_destroy(skip_repetition_label);

        string skip_val1_conversion_label = string_create(20);
        string_append_literal(skip_val1_conversion_label, "SKIP_VAL1_CORECION_");
        string_append_literal(skip_val1_conversion_label, tmp);

        string skip_val2_conversion_label = string_create(20);
        string_append_literal(skip_val2_conversion_label, "SKIP_VAL2_CORECION_");
        string_append_literal(skip_val2_conversion_label, tmp);
        
        string_append_literal(gen->output, "\n# START AUTO INT->FLOAT COERCION CHECK FOR MULTIPLICATION\n");

        op_eq(gen, "GF@tmp_ifj", "GF@tmp_l", "string@float");
        add_jumpifeq(gen, skip_val1_conversion_label->data, "GF@tmp_ifj", "bool@false"); 
        generate_float_conversion(gen, right, "GF@tmp_r");
        label(gen, skip_val1_conversion_label->data);
        
        op_eq(gen, "GF@tmp_ifj", "GF@tmp_r", "string@float");
        add_jumpifneq(gen, skip_val2_conversion_label->data, "GF@tmp_ifj", "bool@true");
        generate_float_conversion(gen, left, "GF@tmp_l");
        label(gen, skip_val2_conversion_label->data);
        
        string_append_literal(gen->output, "# END AUTO INT->FLOAT COERCION\n");
        
        generate_type_check(gen, left, right, "ERR26");
        op_mul(gen, result, left, right);
        label(gen, skip_end_label->data);
        
        string_destroy(skip_val1_conversion_label);
        string_destroy(skip_val2_conversion_label);
        string_destroy(skip_end_label);
}

void generate_div_conversion(generator gen, char *left, char *right) {    
    string_append_literal(gen->output, "\n# START AUTO INT->FLOAT COERCION CHECK FOR BINARY OP\n");

    ifj_type(gen, "GF@tmp_l", left);
    ifj_type(gen, "GF@tmp_r", right);

    generate_float_conversion(gen, right, "GF@tmp_r");
    generate_float_conversion(gen, left, "GF@tmp_l");
    
    string_append_literal(gen->output, "# END AUTO INT->FLOAT COERCION\n");
    generate_type_check(gen, left, right, "ERR26");
}

void generate_binary(generator gen, char * result, ast_expression node){
    ast_expression left = node->operands.binary_op.left;
    ast_expression right = node->operands.binary_op.right;
    ast_expression_type operation = node->type;
    
    char *left_temp = generate_temp_var(gen);
    char *right_temp = generate_temp_var(gen);
    char *value;

    define_variable(gen, left_temp);
    define_variable(gen, right_temp);

    generate_expression(gen, left_temp, left);
    if(operation != AST_IS)
        generate_expression(gen, right_temp, right);
    
    if (operation != AST_IS && operation != AST_ADD && operation != AST_MUL && operation != AST_DIV) {
        char tmp[20];
        snprintf(tmp, 20, "%u", gen->counter++);

        string skip_val1_conversion_label = string_create(20);
        string_append_literal(skip_val1_conversion_label, "SKIP_VAL1_COERCION_");
        string_append_literal(skip_val1_conversion_label, tmp);

        string skip_val2_conversion_label = string_create(20);
        string_append_literal(skip_val2_conversion_label, "SKIP_VAL2_COERCION_");
        string_append_literal(skip_val2_conversion_label, tmp);
        
        string_append_literal(gen->output, "\n# START AUTO INT->FLOAT COERCION CHECK FOR BINARY OP\n");

        ifj_type(gen, "GF@tmp_l", left_temp);
        ifj_type(gen, "GF@tmp_r", right_temp);

        op_eq(gen, "GF@tmp_ifj", "GF@tmp_l", "string@float");
        add_jumpifeq(gen, skip_val1_conversion_label->data, "GF@tmp_ifj", "bool@false"); 
        generate_float_conversion(gen, right_temp, "GF@tmp_r");
        label(gen, skip_val1_conversion_label->data);
        
        op_eq(gen, "GF@tmp_ifj", "GF@tmp_r", "string@float");
        add_jumpifneq(gen, skip_val2_conversion_label->data, "GF@tmp_ifj", "bool@true");
        generate_float_conversion(gen, left_temp, "GF@tmp_l");
        label(gen, skip_val2_conversion_label->data);
        
        string_append_literal(gen->output, "# END AUTO INT->FLOAT COERCION\n");
        string_destroy(skip_val1_conversion_label);
        string_destroy(skip_val2_conversion_label);
        generate_type_check(gen, left_temp, right_temp, "ERR26");
    }

    switch (operation) {
        case AST_ADD:
            generate_add_conversion(gen, result, left_temp, right_temp);
            break;
        case AST_SUB:
            op_sub(gen, result, left_temp, right_temp);
            break;
        case AST_MUL:
            generate_mul_conversion(gen, result, left_temp, right_temp);
            break;
        case AST_DIV: 
            generate_div_conversion(gen, left_temp, right_temp);
            op_div(gen, result, left_temp, right_temp);
            break;
        case AST_LT:
            op_lt(gen, result, left_temp, right_temp);
            break;
        case AST_GT:
            op_gt(gen, result, left_temp, right_temp);
            break;
        case AST_EQUALS:
            op_eq(gen, result, left_temp, right_temp);
            break;
        case AST_NOT_EQUAL:
            op_eq(gen, result, left_temp, right_temp);
            op_not(gen, result, result);
            break;
        case AST_AND:
            op_and(gen, result, left_temp, right_temp);
            break;
        case AST_OR:
            op_or(gen, result, left_temp, right_temp);
            break;
        case AST_LE:
            op_lt(gen, "GF@tmp1", left_temp, right_temp);
            op_eq(gen, "GF@tmp2", left_temp, right_temp);
            op_or(gen, result, "GF@tmp1", "GF@tmp2");
            break;
        case AST_GE:
            op_gt(gen, "GF@tmp1", left_temp, right_temp);
            op_eq(gen, "GF@tmp2", left_temp, right_temp);
            op_or(gen, result, "GF@tmp1", "GF@tmp2");
            break;
        case AST_IS:
            ifj_type(gen, left_temp, ast_value_to_string(left, NULL));
            if (strcmp(right->operands.identifier.value, "Num") == 0)
                value = "string@int";
            else if (strcmp(right->operands.identifier.value, "String") == 0)
                value = "string@string";
            else if (strcmp(right->operands.identifier.value, "Null") == 0)
                value = "string@nil";
            op_eq(gen, result, left_temp, value);
        default:
            break;
    }
    char tmp[20];
    snprintf(tmp, 20, "%u", gen->counter++);
    string is_float_label = string_create(20);
    string_append_literal(is_float_label, "IS_FLOAT_");
    string_append_literal(is_float_label, tmp);
    
    string_append_literal(gen->output, "\n# START FLOAT -> INT CONVERSION\n");
    ifj_type(gen, "GF@tmp_ifj", result);
    op_eq(gen, "GF@tmp1", "GF@tmp_ifj", "string@float");
    add_jumpifeq(gen, is_float_label->data, "GF@tmp1", "bool@false");
    ifj_float2int(gen, "GF@tmp1", result);
    ifj_int2float(gen, "GF@tmp2", "GF@tmp1");
    op_eq(gen, "GF@tmp_ifj", "GF@tmp2", result);
    add_jumpifeq(gen, is_float_label->data, "GF@tmp_ifj", "bool@false");
    move_var(gen, result, "GF@tmp1");
    label(gen, is_float_label->data);
    string_destroy(is_float_label);
    string_append_literal(gen->output, "\n# END FLOAT -> INT CONVERSION\n");

    free(left_temp);
    free(right_temp);
}

void generate_expression(generator gen, char * result, ast_expression node){
    
    if (node->type == AST_VALUE) {
        char *value = ast_value_to_string(node, NULL);
        move_var(gen, result, value);
    }
    else if (node->type == AST_IDENTIFIER){
        char *value = ast_value_to_string(node, NULL);
        move_var(gen, result, value); //move LF@var value
    }
    else if (node->type == AST_IFJ_FUNCTION_EXPR)
        generate_ifjfunction(gen, node->operands.ifj_function->name, node->operands.ifj_function->parameters, result);
    else if (node->type == AST_FUNCTION_CALL) {
        generate_function_call(gen, NULL, node);
        move_var(gen, result, "GF@fn_ret");
    }
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

void generate_substring(generator gen, char *result, char *var1, char *var2, char *var3) {
    char tmp[20];
    snprintf(tmp, 20, "%u", gen->counter++);
    string skip_substring_label = string_create(20);
    string_append_literal(skip_substring_label, "SKIP_SUBSTRING_CORECION_");
    string_append_literal(skip_substring_label, tmp);

    string loop_substring_label = string_create(20);
    string_append_literal(loop_substring_label, "LOOP_SUBSTRING_");
    string_append_literal(loop_substring_label, tmp);

    string_append_literal(gen->output, "\n# START SUBSTRING GENERATION\n");

    ifj_type(gen, "GF@tmp_l", var2);
    ifj_type(gen, "GF@tmp_r", var3);
    op_eq(gen, "GF@tmp_ifj", "GF@tmp_l", "string@int");
    add_jumpifeq(gen, "ERR26", "GF@tmp_ifj", "bool@false");
    
    op_eq(gen, "GF@tmp_ifj", "GF@tmp_r", "string@int");
    add_jumpifeq(gen, "ERR26", "GF@tmp_ifj", "bool@false");

    move_var(gen, result, "nil@nil");
    ifj_strlen(gen, "GF@tmp1", var1);
    op_lt(gen, "GF@tmp_ifj", var2, "int@0");
    add_jumpifeq(gen, skip_substring_label->data, "GF@tmp_ifj", "bool@true");
    op_lt(gen, "GF@tmp_ifj", var2, "GF@tmp1");
    add_jumpifeq(gen, skip_substring_label->data, "GF@tmp_ifj", "bool@false");

    op_lt(gen, "GF@tmp_ifj", var3, "int@0");
    add_jumpifeq(gen, skip_substring_label->data, "GF@tmp_ifj", "bool@true");
    op_lt(gen, "GF@tmp_ifj", var3, "GF@tmp1");
    add_jumpifeq(gen, skip_substring_label->data, "GF@tmp_ifj", "bool@false");

    move_var(gen, result, "string@");
    move_var(gen, "GF@tmp_l", var2);
    label(gen, loop_substring_label->data);
    
    op_lt(gen, "GF@tmp_ifj", "GF@tmp_l", var3);
    add_jumpifeq(gen, skip_substring_label->data, "GF@tmp_ifj", "bool@false");
    
    ifj_getchar(gen, "GF@tmp_r", var1, "GF@tmp_l");
    op_concat(gen, result, result, "GF@tmp_r");
    op_add(gen, "GF@tmp_l", "GF@tmp_l", "int@1");
    
    jump(gen, loop_substring_label->data);

    label(gen, skip_substring_label->data);

    string_append_literal(gen->output, "\n# END SUBSTRING GENERATION\n");

    string_destroy(loop_substring_label);
    string_destroy(skip_substring_label);
}

void generate_strcmp(generator gen, char *result, char *left, char *right) {
    char tmp[20];
    snprintf(tmp, 20, "%u", gen->counter++);
    string loop_strcmp_label = string_create(20);
    string_append_literal(loop_strcmp_label, "LOOP_STRCMP_");
    string_append_literal(loop_strcmp_label, tmp);

    string skip_strcmp_label = string_create(20);
    string_append_literal(skip_strcmp_label, "SKIP_STRCMP_");
    string_append_literal(skip_strcmp_label, tmp);

    string same_char_label = string_create(20);
    string_append_literal(same_char_label, "SAME_CHAR_");
    string_append_literal(same_char_label, tmp);

    string_append_literal(gen->output, "\n# START STRCMP GENERATION\n");

    move_var(gen, result, "int@0");
    move_var(gen, "GF@tmp_l", "int@0");

    ifj_strlen(gen, "GF@tmp1", left);
    ifj_strlen(gen, "GF@tmp2", right);

    move_var(gen, "GF@tmp_r", "GF@tmp2");
    op_sub(gen, result, "GF@tmp1", "GF@tmp2");

    op_lt(gen, "GF@tmp_r", "GF@tmp1", "GF@tmp2");
    add_jumpifeq(gen, loop_strcmp_label->data, "GF@tmp_r", "bool@true");
    move_var(gen, "GF@tmp_r", "GF@tmp1");

    label(gen, loop_strcmp_label->data);
    op_lt(gen, "GF@tmp_ifj", "GF@tmp_l", "GF@tmp_r");
    add_jumpifeq(gen, skip_strcmp_label->data, "GF@tmp_ifj", "bool@false");
    
    ifj_getchar(gen, "GF@tmp1", left, "GF@tmp_l");
    ifj_getchar(gen, "GF@tmp2", right, "GF@tmp_l");
    
    op_eq(gen, "GF@tmp_ifj", "GF@tmp1", "GF@tmp2");
    add_jumpifeq(gen, same_char_label->data, "GF@tmp_ifj", "bool@true");

    op_sub(gen, result, result, "int@1");
    
    label(gen, same_char_label->data);
    op_add(gen, "GF@tmp_l", "GF@tmp_l", "int@1");
    jump(gen, loop_strcmp_label->data);

    label(gen, skip_strcmp_label->data);

    string_append_literal(gen->output, "\n# END STRCMP GENERATION\n");

    string_destroy(loop_strcmp_label);
    string_destroy(same_char_label);
    string_destroy(skip_strcmp_label);
}

void generate_ifjfunction(generator gen, char* name, ast_parameter params, char* output) {
    if(strcmp(name, "str") == 0)
        generate_ifj_str(gen, output, params);
    else if(strcmp(name, "chr") == 0)
        ifj_int2char(gen, output, ast_value_to_string(NULL, params));
    else if(strcmp(name, "floor") == 0)
        ifj_float2int(gen, output, ast_value_to_string(NULL, params));
    else if(strcmp(name, "length") == 0)
        ifj_strlen(gen, output, ast_value_to_string(NULL, params));
    else if(strcmp(name, "ord") == 0)
        ifj_stri2int(gen, output, ast_value_to_string(NULL, params), ast_value_to_string(NULL, params->next));
    else if(strcmp(name, "read_num") == 0) {
        char tmp[20];
        snprintf(tmp, 20, "%u", gen->counter++);
        string is_float_label = string_create(20);
        string_append_literal(is_float_label, "IS_FLOAT_");
        string_append_literal(is_float_label, tmp);
        
        ifj_read(gen, output, "float");
        ifj_float2int(gen, "GF@tmp1", output);
        ifj_int2float(gen, "GF@tmp2", "GF@tmp1");
        op_eq(gen, "GF@tmp_ifj", "GF@tmp2", output);
        add_jumpifeq(gen, is_float_label->data, "GF@tmp_ifj", "bool@false");
        move_var(gen, output, "GF@tmp1");
        label(gen, is_float_label->data);
        string_destroy(is_float_label);
    }
    else if(strcmp(name, "read_str") == 0)
        ifj_read(gen, output, "string");
    else if(strcmp(name, "strcmp") == 0)
        generate_strcmp(gen, output, ast_value_to_string(NULL, params), ast_value_to_string(NULL, params->next));
    else if(strcmp(name, "substring") == 0)
        generate_substring(gen, output, ast_value_to_string(NULL, params), ast_value_to_string(NULL, params->next), ast_value_to_string(NULL, params->next->next));
    else if(strcmp(name, "write") == 0)
        ifj_write(gen, ast_value_to_string(NULL, params));
    else {
        string_append_literal(gen->output, "#IFJ FUN: ");
        string_append_literal(gen->output, name);
        string_append_literal(gen->output, " not implemented\n");
    }
}

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
    while(!stack_is_empty(&stack)){ //reverse push
        param_name = stack_pop(&stack);
        push(gen, param_name);
    }
    fn_call(gen, name); //call function*/

    stack_free(&stack);
}

void generate_function_return(generator gen, ast_node node){
    if(node->data.return_expr.output)
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
            generate_function_call(gen, node, NULL);
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
    ast_node node = block->first;
    while (node) {
        generate_node(node, gen);
        node = node->next;
    }
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
        define_variable(gen, "GF@tmp3");
        define_variable(gen, "GF@fn_ret");

        sem_def_globals(gen);
    }
}

void generate_code(generator gen, ast ast){ //go threw all nodes in AST
    if(ast != NULL && ast->class_list != NULL){
        ast_class program = ast->class_list; //opens 'program' class   
        ast_block program_body = program->current; // class block
        ast_node function = program_body->first;

        while (function != NULL) {
            if (function->type == AST_FUNCTION && strcmp(function->data.function->name, "main") == 0) {
                generate_main(gen, function);
                break;
            }
            function = function->next;
        }
        generate_block(gen, program_body);

        label(gen, "ERR26");
        string_append_literal(gen->output, "# ERROR: Incompatible types for binary operation.\n");
        exit_code(gen, "int@26");

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
    if (strcmp(name, "main")) {
        string_append_literal(gen->output, "\n# START OF FUNCTION ---");
        string_append_literal(gen->output, name);
        string_append_literal(gen->output, "---\n");
        label(gen, name);
        createframe(gen); //create function frame
        pushframe(gen); //use new frame
        while(param != NULL){
            define_variable(gen, ast_value_to_string(NULL, param));
            pop(gen, ast_value_to_string(NULL, param));
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
        move_var(gen, "GF@fn_ret", "nil@nil");
        return_code(gen);
    }
}

void generate_main(generator gen, ast_node node) {
    char *name;
    ast_block fun_body;
    ast_parameter param = NULL;
    name = node->data.function->name;
    param = node->data.function->parameters;
    fun_body = node->data.function->code;

    string_append_literal(gen->output, "\n# START OF MAIN FUNCTION ---");
    string_append_literal(gen->output, name);
    string_append_literal(gen->output, "---\n");
    label(gen, name);
    createframe(gen); //create function frame
    pushframe(gen); //use new frame
    while(param != NULL){
        define_variable(gen, ast_value_to_string(NULL, param));
        pop(gen, ast_value_to_string(NULL, param));
        param = param->next;
    }
    if(node->type == AST_SETTER) {
        define_variable(gen, node->data.setter.param);
        pop(gen, node->data.setter.param);
    }

    generate_block(gen, fun_body); //generate body

    popframe(gen); //pop frame from stack and use previous
    string_append_literal(gen->output, "# END OF MAIN FUNCTION ---");
    string_append_literal(gen->output, name);
    string_append_literal(gen->output, "---\n");
    exit_code(gen, "int@0\n");
}